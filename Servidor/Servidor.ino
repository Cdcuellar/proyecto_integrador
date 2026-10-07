#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include <math.h>

#include <Wire.h>
#include <U8g2lib.h>

// ============================================================
// OLED SSD1306 128x32 con U8g2
// ============================================================

#define OLED_SDA 21
#define OLED_SCL 22

U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(
  U8G2_R0,
  U8X8_PIN_NONE,
  OLED_SCL,
  OLED_SDA
);

bool oledOK = false;

// ============================================================
// PINES LEDs DE ALERTA
// ============================================================

#define LED_PULSO       25
#define LED_SPO2        26
#define LED_MOVIMIENTO  27

// ============================================================
// WIFI + SERVIDOR
// ============================================================

const char* WIFI_SSID = "Pelu";
const char* WIFI_PASS = "peluchina";

const char* SERVER_URL_POST   = "https://ethanol-nutcase-overeater.ngrok-free.dev/api/datos";
const char* SERVER_URL_THRESH = "https://ethanol-nutcase-overeater.ngrok-free.dev/api/umbrales";

// ============================================================
// MACs DE LOS NODOS EMISORES
// ============================================================

uint8_t MAC_NODO_1[] = {0xB4, 0x3A, 0x45, 0x29, 0xA0, 0x78};
uint8_t MAC_NODO_2[] = {0xA4, 0xF0, 0x0F, 0x67, 0x69, 0xE8};

// ============================================================
// STRUCTS DE LOS PAQUETES
// ============================================================

typedef struct {
  int     bpm;
  float   spo2;
  float   ir_promedio;
  int     calidad_senal;
  float   ax, ay, az;
  float   gx, gy, gz;
  uint8_t contacto;
  uint8_t resultado_valido;
} PaqueteNodo1_t;

typedef struct __attribute__((packed)) {
  float   ax_g, ay_g, az_g;
  float   gx_dps, gy_dps, gz_dps;
  uint8_t gps_fix;
  int32_t latitude_e6;
  int32_t longitude_e6;
  float   altitude_m;
  float   speed_kmph;
} PaqueteNodo2_t;

// ============================================================
// DATOS RECIBIDOS
// ============================================================

PaqueteNodo1_t ultimoNodo1;
PaqueteNodo2_t ultimoNodo2;

bool nodo1Listo = false;
bool nodo2Listo = false;

// ============================================================
// ESTADOS GENERALES
// ============================================================

unsigned long ultimoN1Ms   = 0;
unsigned long ultimoN2Ms   = 0;
unsigned long ultimoPostMs = 0;

String estadoServidor    = "Esperando";
int    ultimoCodigoPOST  = 0;
bool   enviandoServidor  = false;

// ============================================================
// OLED - CONTROL
// ============================================================

// Mientras hay alertas activas la pantalla queda FIJA en modo alerta.
// Solo cuando NO hay ninguna alerta se muestra el estado normal
// o los mensajes de evento (RX, enviando, etc.).

unsigned long oledMensajeHasta = 0;
#define OLED_MSG_MS 2500UL

// ============================================================
// COLAS FREERTOS
// ============================================================

QueueHandle_t colaNodo1;
QueueHandle_t colaNodo2;

// ============================================================
// UMBRALES
// ============================================================

struct {
  float bpm_min    = 50.0;
  float bpm_max    = 120.0;
  float spo2_min   = 90.0;
  float spo2_max   = 100.0;
  float mov_umbral = 2.5;
} umbrales;

// ============================================================
// ESTADO DE ALERTAS
// Cada alerta es independiente.
// Pulso y SpO2: se activan tras N segundos fuera de rango.
// Movimiento:   se activa de inmediato y dura TIEMPO_LED_MOVIMIENTO_MS.
// ============================================================

struct {
  bool  mostrando      = false;
  bool  en_racha       = false;
  unsigned long inicio = 0;
  int   datos_bajo     = 0;
  float ultimo_valor   = 0;
} alertaPulso;

struct {
  bool  mostrando      = false;
  bool  en_racha       = false;
  unsigned long inicio = 0;
  int   datos_bajo     = 0;
  float ultimo_valor   = 0;
} alertaSpo2;

struct {
  bool  mostrando        = false;
  bool  en_racha         = false;
  unsigned long inicio   = 0;
  int   consec_bajo      = 0;
} alertaMov;

// ============================================================
// TEMPORIZACIÓN ALERTA MOVIMIENTO
// ============================================================

#define TIEMPO_LED_MOVIMIENTO_MS 5000UL
#define TIEMPO_REARME_MOV_MS     3000UL

unsigned long ultimoDisparoMov = 0;

// ============================================================
// TIMERS
// ============================================================

unsigned long ultimaConsultaUmb = 0;
unsigned long ultimoRefreshOLED = 0;

#define INTERVALO_UMBRAL_MS  30000UL
#define INTERVALO_OLED_MS     1000UL

// ============================================================
// PROTOTIPOS
// ============================================================

void conectarWiFi();
void consultarUmbrales();
String construirJsonNodo1();
String construirJsonNodo2();
bool enviarAlServidor(const String& payload, const String& origen);
float magnitudNodo1();
float magnitudNodo2();
void evaluarPulso(float bpm);
void evaluarSpo2(float spo2);
void evaluarMovimiento(float mag);
void actualizarLEDs();
void oledInicio();
void oledMensaje(const String& titulo,
                 const String& l1 = "",
                 const String& l2 = "",
                 const String& l3 = "");
void oledNormal();
void oledAlertas();          // NUEVO: dibuja todas las alertas activas juntas
void actualizarOLED();

// ============================================================
// HELPERS — hay alertas activas?
// ============================================================

bool hayAlertasActivas() {
  return alertaPulso.mostrando || alertaSpo2.mostrando || alertaMov.mostrando;
}

// ============================================================
// CALLBACK ESP-NOW
// Solo encola — nunca procesa aquí
// ============================================================

void onDataReceived(const esp_now_recv_info_t *recv_info,
                    const uint8_t *data,
                    int len) {
  const uint8_t *mac = recv_info->src_addr;

  if (memcmp(mac, MAC_NODO_1, 6) == 0 && len == sizeof(PaqueteNodo1_t)) {
    PaqueteNodo1_t tmp;
    memcpy(&tmp, data, sizeof(PaqueteNodo1_t));
    xQueueOverwriteFromISR(colaNodo1, &tmp, NULL);
  }
  else if (memcmp(mac, MAC_NODO_2, 6) == 0 && len == sizeof(PaqueteNodo2_t)) {
    PaqueteNodo2_t tmp;
    memcpy(&tmp, data, sizeof(PaqueteNodo2_t));
    xQueueOverwriteFromISR(colaNodo2, &tmp, NULL);
  }
}

// ============================================================
// WIFI
// ============================================================

void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("[WiFi] Conectando...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 20000) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] OK  IP:%s  Canal:%d\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.channel());
  } else {
    Serial.println("[WiFi] FALLO");
  }
}

// ============================================================
// CONSTRUIR JSON NODO 1
// ============================================================

String construirJsonNodo1() {
  StaticJsonDocument<600> doc;

  doc["origen"]       = "nodo1";
  doc["node_id"]      = 1;
  doc["timestamp_ms"] = millis();

  JsonObject n1 = doc.createNestedObject("nodo1");
  n1["bpm"]              = ultimoNodo1.bpm;
  n1["spo2"]             = round(ultimoNodo1.spo2 * 100.0) / 100.0;
  n1["ir_promedio"]      = round(ultimoNodo1.ir_promedio * 100.0) / 100.0;
  n1["calidad_senal"]    = ultimoNodo1.calidad_senal;
  n1["ax"]               = ultimoNodo1.ax;
  n1["ay"]               = ultimoNodo1.ay;
  n1["az"]               = ultimoNodo1.az;
  n1["gx"]               = ultimoNodo1.gx;
  n1["gy"]               = ultimoNodo1.gy;
  n1["gz"]               = ultimoNodo1.gz;
  n1["contacto"]         = ultimoNodo1.contacto;
  n1["resultado_valido"] = ultimoNodo1.resultado_valido;

  String out;
  serializeJson(doc, out);
  return out;
}

// ============================================================
// CONSTRUIR JSON NODO 2
// ============================================================

String construirJsonNodo2() {
  StaticJsonDocument<600> doc;

  double lat = ultimoNodo2.latitude_e6  / 1000000.0;
  double lon = ultimoNodo2.longitude_e6 / 1000000.0;

  doc["origen"]       = "nodo2";
  doc["node_id"]      = 2;
  doc["timestamp_ms"] = millis();

  JsonObject n2 = doc.createNestedObject("nodo2");
  n2["ax_g"]       = ultimoNodo2.ax_g;
  n2["ay_g"]       = ultimoNodo2.ay_g;
  n2["az_g"]       = ultimoNodo2.az_g;
  n2["gx_dps"]     = ultimoNodo2.gx_dps;
  n2["gy_dps"]     = ultimoNodo2.gy_dps;
  n2["gz_dps"]     = ultimoNodo2.gz_dps;
  n2["gps_fix"]    = ultimoNodo2.gps_fix;
  n2["latitude"]   = lat;
  n2["longitude"]  = lon;
  n2["altitude_m"] = ultimoNodo2.altitude_m;
  n2["speed_kmph"] = ultimoNodo2.speed_kmph;

  String out;
  serializeJson(doc, out);
  return out;
}

// ============================================================
// ENVIAR AL SERVIDOR
// MODIFICACIÓN: si hay alertas activas, NO muestra mensajes de
// "enviando" en la OLED — la pantalla queda fija en las alertas.
// ============================================================

bool enviarAlServidor(const String& payload, const String& origen) {
  conectarWiFi();

  if (WiFi.status() != WL_CONNECTED) {
    estadoServidor   = "WiFi FALLO";
    ultimoCodigoPOST = -1;
    ultimoPostMs     = millis();

    // Solo mostrar en OLED si no hay alertas activas
    if (!hayAlertasActivas()) {
      oledMensaje("SERVIDOR",
                  "WiFi no conectado",
                  "No se envio",
                  origen);
    }
    return false;
  }

  Serial.println("[POST " + origen + "] " + payload);

  enviandoServidor = true;
  estadoServidor   = "Enviando";

  // Solo mostrar "enviando" si la pantalla no está bloqueada por alertas
  if (!hayAlertasActivas()) {
    oledMensaje("ENVIANDO",
                "Origen: " + origen,
                "POST servidor",
                "");
  }

  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient http;
  String url = SERVER_URL_POST;

  if (url.startsWith("https://")) http.begin(secureClient, url);
  else                             http.begin(url);

  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("ngrok-skip-browser-warning", "true");

  int code = http.POST(payload);
  bool ok  = (code >= 200 && code < 300);

  ultimoCodigoPOST = code;
  ultimoPostMs     = millis();
  enviandoServidor = false;
  estadoServidor   = ok ? "POST OK" : "POST ERROR";

  Serial.printf("[POST %s] HTTP:%d OK:%s\n",
                origen.c_str(), code, ok ? "SI" : "NO");

  // Solo mostrar resultado si no hay alertas activas
  if (!hayAlertasActivas()) {
    oledMensaje("SERVIDOR " + origen,
                ok ? "Envio OK" : "Fallo envio",
                "HTTP:" + String(code),
                "");
  }

  http.end();
  return ok;
}

// ============================================================
// CONSULTAR UMBRALES
// ============================================================

void consultarUmbrales() {
  conectarWiFi();
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient http;
  String url = String(SERVER_URL_THRESH) + "?esp32=1";

  if (url.startsWith("https://")) http.begin(secureClient, url);
  else                             http.begin(url);

  http.setTimeout(5000);
  http.addHeader("ngrok-skip-browser-warning", "true");

  int code = http.GET();

  if (code == 200) {
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, http.getString());

    if (!err) {
      umbrales.bpm_min    = doc["bpm_min"]    | umbrales.bpm_min;
      umbrales.bpm_max    = doc["bpm_max"]    | umbrales.bpm_max;
      umbrales.spo2_min   = doc["spo2_min"]   | umbrales.spo2_min;
      umbrales.spo2_max   = doc["spo2_max"]   | umbrales.spo2_max;
      umbrales.mov_umbral = doc["mov_umbral"] | umbrales.mov_umbral;

      Serial.printf("[Umbrales] BPM:%.0f-%.0f  SpO2:%.1f-%.1f  Mov:%.2f\n",
                    umbrales.bpm_min, umbrales.bpm_max,
                    umbrales.spo2_min, umbrales.spo2_max,
                    umbrales.mov_umbral);
    }
  }
  http.end();
}

// ============================================================
// MAGNITUD MOVIMIENTO
// ============================================================

float magnitudNodo1() {
  return sqrtf(
    ultimoNodo1.ax * ultimoNodo1.ax +
    ultimoNodo1.ay * ultimoNodo1.ay +
    ultimoNodo1.az * ultimoNodo1.az
  );
}

float magnitudNodo2() {
  return sqrtf(
    ultimoNodo2.ax_g * ultimoNodo2.ax_g +
    ultimoNodo2.ay_g * ultimoNodo2.ay_g +
    ultimoNodo2.az_g * ultimoNodo2.az_g
  );
}

// ============================================================
// ALERTA PULSO
// ============================================================

void evaluarPulso(float bpm) {
  bool fuera = (bpm < umbrales.bpm_min || bpm > umbrales.bpm_max);
  alertaPulso.ultimo_valor = bpm;

  if (!alertaPulso.mostrando) {
    if (fuera) {
      if (!alertaPulso.en_racha) {
        alertaPulso.en_racha = true;
        alertaPulso.inicio   = millis();
      } else if (millis() - alertaPulso.inicio >= 10000UL) {
        alertaPulso.mostrando  = true;
        alertaPulso.datos_bajo = 0;
        Serial.println("[ALERTA] PULSO ACTIVA");
      }
    } else {
      alertaPulso.en_racha = false;
    }
  } else {
    if (!fuera) {
      alertaPulso.datos_bajo++;
      if (alertaPulso.datos_bajo >= 3) {
        alertaPulso.mostrando = false;
        alertaPulso.en_racha  = false;
        Serial.println("[ALERTA] PULSO APAGADA");
      }
    } else {
      alertaPulso.datos_bajo = 0;
    }
  }
}

// ============================================================
// ALERTA SPO2
// ============================================================

void evaluarSpo2(float spo2) {
  bool fuera = (spo2 < umbrales.spo2_min || spo2 > umbrales.spo2_max);
  alertaSpo2.ultimo_valor = spo2;

  if (!alertaSpo2.mostrando) {
    if (fuera) {
      if (!alertaSpo2.en_racha) {
        alertaSpo2.en_racha = true;
        alertaSpo2.inicio   = millis();
      } else if (millis() - alertaSpo2.inicio >= 10000UL) {
        alertaSpo2.mostrando  = true;
        alertaSpo2.datos_bajo = 0;
        Serial.println("[ALERTA] SPO2 ACTIVA");
      }
    } else {
      alertaSpo2.en_racha = false;
    }
  } else {
    if (!fuera) {
      alertaSpo2.datos_bajo++;
      if (alertaSpo2.datos_bajo >= 3) {
        alertaSpo2.mostrando = false;
        alertaSpo2.en_racha  = false;
        Serial.println("[ALERTA] SPO2 APAGADA");
      }
    } else {
      alertaSpo2.datos_bajo = 0;
    }
  }
}

// ============================================================
// ALERTA MOVIMIENTO
// ============================================================

void evaluarMovimiento(float mag) {
  bool alto     = (mag > umbrales.mov_umbral);
  unsigned long ahora = millis();

  Serial.printf("[MOV] Magnitud: %.2f g | Umbral: %.2f g | Estado: %s\n",
                mag, umbrales.mov_umbral, alto ? "SUPERADO" : "NORMAL");

  if (alto) {
    if (!alertaMov.mostrando &&
        (ahora - ultimoDisparoMov > TIEMPO_REARME_MOV_MS)) {
      alertaMov.mostrando    = true;
      alertaMov.inicio       = ahora;
      ultimoDisparoMov       = ahora;
      Serial.println("[ALERTA] MOVIMIENTO ACTIVA");
    }
  }

  if (alertaMov.mostrando) {
    if (ahora - alertaMov.inicio >= TIEMPO_LED_MOVIMIENTO_MS) {
      alertaMov.mostrando  = false;
      alertaMov.en_racha   = false;
      alertaMov.consec_bajo = 0;
      Serial.println("[ALERTA] MOVIMIENTO APAGADA POR TIEMPO");
    }
  }
}

// ============================================================
// ACTUALIZAR LEDS
// ============================================================

void actualizarLEDs() {
  digitalWrite(LED_PULSO,      alertaPulso.mostrando ? HIGH : LOW);
  digitalWrite(LED_SPO2,       alertaSpo2.mostrando  ? HIGH : LOW);
  digitalWrite(LED_MOVIMIENTO, alertaMov.mostrando   ? HIGH : LOW);
}

// ============================================================
// OLED — funciones de dibujo
// ============================================================

void oledInicio() {
  if (!oledOK) return;

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 9,  "NODO 3");
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(0, 20, "Gateway IoT");
  u8g2.drawStr(0, 30, "Iniciando...");
  u8g2.sendBuffer();
}

// Muestra un mensaje de evento (RX, enviando, etc.)
// Solo se llama cuando NO hay alertas activas.
void oledMensaje(const String& titulo,
                 const String& l1,
                 const String& l2,
                 const String& l3) {
  if (!oledOK) return;

  // Si hay alertas activas, ignorar cualquier mensaje de evento
  if (hayAlertasActivas()) return;

  oledMensajeHasta = millis() + OLED_MSG_MS;

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 8, titulo.c_str());
  u8g2.drawHLine(0, 10, 128);
  u8g2.setFont(u8g2_font_5x8_tf);
  if (l1.length() > 0) u8g2.drawStr(0, 20, l1.c_str());
  if (l2.length() > 0) u8g2.drawStr(0, 30, l2.c_str());
  u8g2.sendBuffer();
}

// Estado normal del gateway (sin alertas)
void oledNormal() {
  if (!oledOK) return;

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 8, "NODO 3 - GATEWAY");
  u8g2.drawHLine(0, 10, 128);
  u8g2.setFont(u8g2_font_5x8_tf);

  String l1 = "WiFi:";
  l1 += (WiFi.status() == WL_CONNECTED ? "OK" : "NO");
  l1 += " HTTP:";
  l1 += (ultimoCodigoPOST == 0 ? "---" : String(ultimoCodigoPOST));

  String l2 = "N1:";
  l2 += (ultimoN1Ms > 0 ? "RX" : "--");
  l2 += " N2:";
  l2 += (ultimoN2Ms > 0 ? "RX" : "--");
  l2 += " ";
  l2 += estadoServidor;

  u8g2.drawStr(0, 20, l1.c_str());
  u8g2.drawStr(0, 30, l2.c_str());
  u8g2.sendBuffer();
}

// ============================================================
// OLED — ALERTAS COMBINADAS (MODIFICACIÓN PRINCIPAL)
//
// Esta función es nueva. Muestra en pantalla TODAS las alertas
// activas al mismo tiempo, comprimiendo el layout según cuántas
// haya (1, 2 o 3 simultáneas).
//
// Pantalla 128x32 px disponibles:
//   - Fila título (y=8): "!! ALERTA !!"
//   - Línea separadora (y=10)
//   - Zona de datos: y=18 a y=31  →  ~14 px = 2 filas de 7px
//
// Con 1 alerta: fuente 5x8, texto completo con valor
// Con 2 alertas: fuente 5x7, una por fila
// Con 3 alertas: fuente 5x7, tres filas de 7px (ajustadas)
// ============================================================

void oledAlertas() {
  if (!oledOK) return;

  // Contar cuántas alertas hay activas
  int n = 0;
  if (alertaPulso.mostrando) n++;
  if (alertaSpo2.mostrando)  n++;
  if (alertaMov.mostrando)   n++;

  if (n == 0) return;

  u8g2.clearBuffer();

  // Título fijo siempre visible
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 8, "!! ALERTA !!");
  u8g2.drawHLine(0, 10, 128);

  // Construir las líneas de alerta activas
  String lineas[3];
  int idx = 0;

  if (alertaPulso.mostrando) {
    lineas[idx]  = "PULSO ";
    lineas[idx] += String((int)alertaPulso.ultimo_valor);
    lineas[idx] += "bpm";
    idx++;
  }
  if (alertaSpo2.mostrando) {
    lineas[idx]  = "SpO2 ";
    lineas[idx] += String(alertaSpo2.ultimo_valor, 1);
    lineas[idx] += "%";
    idx++;
  }
  if (alertaMov.mostrando) {
    lineas[idx] = "MOVIMIENTO alto";
    idx++;
  }

  // Layout vertical según cantidad de alertas activas
  u8g2.setFont(u8g2_font_5x7_tf);

  if (n == 1) {
    // Una sola alerta: centrar verticalmente, fuente un poco mayor
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 24, lineas[0].c_str());

  } else if (n == 2) {
    // Dos alertas: una en y=19 y otra en y=30
    u8g2.drawStr(0, 19, lineas[0].c_str());
    u8g2.drawStr(0, 30, lineas[1].c_str());

  } else {
    // Tres alertas: y=14, y=22, y=30  (separadas 8px)
    u8g2.drawStr(0, 14, lineas[0].c_str());
    u8g2.drawStr(0, 22, lineas[1].c_str());
    u8g2.drawStr(0, 30, lineas[2].c_str());
  }

  u8g2.sendBuffer();
}

// ============================================================
// ACTUALIZAR OLED — lógica de decisión de qué mostrar
//
// MODIFICACIÓN: hay dos estados mutuamente excluyentes:
//
//   ESTADO ALERTA  → hayAlertasActivas() == true
//     La pantalla queda ESTÁTICA en oledAlertas().
//     No se interrumpe con mensajes de RX, enviando, etc.
//     Si aparece una segunda o tercera alerta mientras se
//     muestra la primera, oledAlertas() las incluye todas.
//
//   ESTADO NORMAL  → hayAlertasActivas() == false
//     Se muestran mensajes de evento (RX, enviando) por
//     OLED_MSG_MS ms y luego vuelve a oledNormal().
// ============================================================

void actualizarOLED() {
  if (!oledOK) return;

  if (millis() - ultimoRefreshOLED < INTERVALO_OLED_MS) return;
  ultimoRefreshOLED = millis();

  if (hayAlertasActivas()) {
    // ESTADO ALERTA: siempre redibujar con todas las activas
    oledAlertas();
    return;
  }

  // ESTADO NORMAL: respetar mensajes de evento o mostrar estado
  if (millis() < oledMensajeHasta) return;
  oledNormal();
}

// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== NODO 3 CONCENTRADOR ===");

  pinMode(LED_PULSO,      OUTPUT);
  pinMode(LED_SPO2,       OUTPUT);
  pinMode(LED_MOVIMIENTO, OUTPUT);
  digitalWrite(LED_PULSO,      LOW);
  digitalWrite(LED_SPO2,       LOW);
  digitalWrite(LED_MOVIMIENTO, LOW);

  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();
  oledOK = true;
  oledInicio();
  Serial.println("[OLED] Inicializada con U8g2");

  WiFi.mode(WIFI_STA);
  conectarWiFi();

  Serial.printf("[INFO] MAC Nodo3: %s\n", WiFi.macAddress().c_str());
  Serial.printf("[INFO] Canal WiFi: %d\n", WiFi.channel());
  Serial.println("[INFO] Nodo 1 y Nodo 2 deben estar en este mismo canal.");

  colaNodo1 = xQueueCreate(1, sizeof(PaqueteNodo1_t));
  colaNodo2 = xQueueCreate(1, sizeof(PaqueteNodo2_t));

  if (colaNodo1 == NULL || colaNodo2 == NULL) {
    Serial.println("[ERROR] No se pudieron crear las colas");
    oledMensaje("ERROR", "No hay colas", "Reiniciar nodo", "");
    return;
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] ERROR al inicializar");
    oledMensaje("ERROR", "ESP-NOW fallo", "Reiniciar nodo", "");
    return;
  }

  esp_now_register_recv_cb(onDataReceived);
  Serial.println("[ESP-NOW] OK");
  Serial.println("[SISTEMA] Esperando paquetes de Nodo 1 o Nodo 2...");

  oledMensaje("NODO 3 LISTO",
              "WiFi:" + String(WiFi.status() == WL_CONNECTED ? "OK" : "NO"),
              "ESP-NOW OK",
              "");

  consultarUmbrales();
  Serial.println("[SISTEMA] Listo.");
}

// ============================================================
// LOOP
// ============================================================

void loop() {

  // ── Procesar Nodo 1 ──────────────────────────────────────
  PaqueteNodo1_t tmpN1;
  if (xQueueReceive(colaNodo1, &tmpN1, 0) == pdTRUE) {
    ultimoNodo1 = tmpN1;
    nodo1Listo  = true;
    ultimoN1Ms  = millis();
    estadoServidor = "RX N1";

    Serial.printf("[N1] BPM:%d SpO2:%.1f Valido:%d\n",
                  ultimoNodo1.bpm,
                  ultimoNodo1.spo2,
                  ultimoNodo1.resultado_valido);

    // Mensaje de recepción solo si no hay alertas activas
    if (!hayAlertasActivas()) {
      oledMensaje("RX NODO 1",
                  "BPM:" + String(ultimoNodo1.bpm) +
                  " SpO2:" + String(ultimoNodo1.spo2, 1),
                  ultimoNodo1.resultado_valido ? "Valido" : "No valido",
                  "");
    }

    if (ultimoNodo1.resultado_valido) {
      evaluarPulso(ultimoNodo1.bpm);
      evaluarSpo2(ultimoNodo1.spo2);
    }

    evaluarMovimiento(magnitudNodo1());
    enviarAlServidor(construirJsonNodo1(), "N1");
  }

  // ── Procesar Nodo 2 ──────────────────────────────────────
  PaqueteNodo2_t tmpN2;
  if (xQueueReceive(colaNodo2, &tmpN2, 0) == pdTRUE) {
    ultimoNodo2 = tmpN2;
    nodo2Listo  = true;
    ultimoN2Ms  = millis();
    estadoServidor = "RX N2";

    float movN2 = magnitudNodo2();

    Serial.printf("[N2] Ax:%.2f Ay:%.2f Az:%.2f GPS:%d\n",
                  ultimoNodo2.ax_g,
                  ultimoNodo2.ay_g,
                  ultimoNodo2.az_g,
                  ultimoNodo2.gps_fix);

    // Mensaje de recepción solo si no hay alertas activas
    if (!hayAlertasActivas()) {
      oledMensaje("RX NODO 2",
                  "Mov:" + String(movN2, 2) + "g GPS:" +
                  String(ultimoNodo2.gps_fix ? "SI" : "NO"),
                  movN2 > umbrales.mov_umbral ? "Mov alto" : "Mov normal",
                  "");
    }

    evaluarMovimiento(movN2);
    enviarAlServidor(construirJsonNodo2(), "N2");
  }

  // ── Consultar umbrales periódicamente ────────────────────
  if (millis() - ultimaConsultaUmb >= INTERVALO_UMBRAL_MS) {
    ultimaConsultaUmb = millis();
    consultarUmbrales();
  }

  // ── Actualizar OLED y LEDs ────────────────────────────────
  actualizarOLED();
  actualizarLEDs();

  yield();
}
