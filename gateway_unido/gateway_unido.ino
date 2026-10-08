#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <U8g2lib.h>

#include "espnow_comun.h"

// ============================================================
// MODO PRUEBA
// 1 = además de lo que llegue por ESP-NOW, envía un dato falso
//     al servidor cada 10 s (para probar solo la parte de internet)
// 0 = funcionamiento normal
// ============================================================
#define MODO_PRUEBA 0
#define INTERVALO_PRUEBA_MS 10000UL

// ============================================================
// OLED SSD1306 128x32 (quita esta parte si no usas pantalla)
// ============================================================
#define OLED_SDA 21
#define OLED_SCL 22
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

// ============================================================
// WIFI + SERVIDOR
// ============================================================
const char* WIFI_SSID = "Pelu";
const char* WIFI_PASS = "peluchina";

const char* SERVER_URL_POST = "https://catapult-ethics-device.ngrok-free.dev/api/datos";

// ============================================================
// RECEPCIÓN ESP-NOW (código del receptor, sin cambios)
// ============================================================
typedef struct {
  uint8_t         macOrigen[6];
  PaqueteEspNow_t paquete;
} EventoRx_t;

static QueueHandle_t colaRx = NULL;

/* CALLBACK DE RECEPCIÓN (corre en la tarea WiFi: solo encolar)
 * (la firma cambió en el core ESP32 3.0.0) */
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
#else
void onDataRecv(const uint8_t *mac, const uint8_t *data, int len)
#endif
{
  if (data == NULL || len != sizeof(PaqueteEspNow_t)) {
    return;
  }

  EventoRx_t evento;
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  memcpy(evento.macOrigen, info->src_addr, 6);
#else
  memcpy(evento.macOrigen, mac, 6);
#endif
  memcpy(&evento.paquete, data, sizeof(PaqueteEspNow_t));

  xQueueSend(colaRx, &evento, 0);
}

// ============================================================
// ESTADO
// ============================================================
int           ultimoCodigoPOST  = 0;
unsigned long paquetesRx        = 0;
unsigned long ultimoRefreshOLED = 0;
unsigned long ultimaPruebaMs    = 0;
unsigned long contadorPrueba    = 0;

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
                  WiFi.localIP().toString().c_str(), WiFi.channel());
  } else {
    Serial.println("[WiFi] FALLO");
  }
}

// ============================================================
// JSON + ENVÍO AL SERVIDOR
// ============================================================
String construirJson(const EventoRx_t& ev) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           ev.macOrigen[0], ev.macOrigen[1], ev.macOrigen[2],
           ev.macOrigen[3], ev.macOrigen[4], ev.macOrigen[5]);

  // El mensaje viaja como char[32]; se copia asegurando el fin de cadena
  char mensaje[33];
  memcpy(mensaje, ev.paquete.mensaje, 32);
  mensaje[32] = '\0';

  StaticJsonDocument<256> doc;
  doc["mac"]          = macStr;
  doc["contador"]     = ev.paquete.contador;
  doc["mensaje"]      = mensaje;
  doc["temperatura"]  = ev.paquete.temperatura;
  doc["timestamp_ms"] = millis();

  String out;
  serializeJson(doc, out);
  return out;
}

bool enviarAlServidor(const String& payload) {
  conectarWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    ultimoCodigoPOST = -1;
    return false;
  }

  Serial.println("[POST] " + payload);

  // El cliente seguro debe vivir hasta que termine el POST
  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient http;
  String url = SERVER_URL_POST;

  if (url.startsWith("https://")) {
    http.begin(secureClient, url);
  } else {
    http.begin(url);
  }

  http.setTimeout(8000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("ngrok-skip-browser-warning", "true");

  int code = http.POST(payload);
  ultimoCodigoPOST = code;

  if (code > 0) {
    Serial.printf("[POST] HTTP:%d\n", code);
  } else {
    Serial.printf("[POST] ERROR:%d (%s)\n", code, http.errorToString(code).c_str());
  }

  http.end();
  return (code >= 200 && code < 300);
}

// ============================================================
// OLED
// ============================================================
void actualizarOLED() {
  if (millis() - ultimoRefreshOLED < 1000) return;
  ultimoRefreshOLED = millis();

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 8, "GATEWAY");
  u8g2.drawHLine(0, 10, 128);
  u8g2.setFont(u8g2_font_5x8_tf);

  String l1 = String("WiFi:") + (WiFi.status() == WL_CONNECTED ? "OK" : "NO") +
              " HTTP:" + (ultimoCodigoPOST == 0 ? "---" : String(ultimoCodigoPOST));
  String l2 = "Paquetes RX: " + String(paquetesRx);

  u8g2.drawStr(0, 20, l1.c_str());
  u8g2.drawStr(0, 30, l2.c_str());
  u8g2.sendBuffer();
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== GATEWAY ===");

  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();

  colaRx = xQueueCreate(10, sizeof(EventoRx_t));
  if (colaRx == NULL) {
    Serial.println("No se pudo crear la cola");
    return;
  }

  // Aquí el canal lo define el router al conectarse (no se fuerza)
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);   // radio siempre despierta: evita perder paquetes ESP-NOW
  conectarWiFi();

  Serial.print("MAC propia: ");
  Serial.print(WiFi.macAddress());
  Serial.println("  <-- copiar al emisor");

  Serial.printf("[INFO] Canal WiFi actual: %d | Canal del emisor (ESPNOW_CHANNEL): %d\n",
                WiFi.channel(), ESPNOW_CHANNEL);
  if (WiFi.channel() != ESPNOW_CHANNEL) {
    Serial.println("[AVISO] Los canales NO coinciden: el emisor no se va a recibir.");
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error inicializando ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(onDataRecv);

  Serial.printf("ESP-NOW listo - canal %d - esperando paquetes\n", WiFi.channel());
}

// ============================================================
// LOOP: procesa lo que llega y lo manda al servidor
// ============================================================
void loop() {
  EventoRx_t evento;

  if (xQueueReceive(colaRx, &evento, 0) == pdTRUE) {
    paquetesRx++;

    Serial.printf("De %02X:%02X:%02X:%02X:%02X:%02X | #%lu | \"%s\" | %.2f C\n",
                  evento.macOrigen[0], evento.macOrigen[1],
                  evento.macOrigen[2], evento.macOrigen[3],
                  evento.macOrigen[4], evento.macOrigen[5],
                  (unsigned long)evento.paquete.contador,
                  evento.paquete.mensaje,
                  evento.paquete.temperatura);

    enviarAlServidor(construirJson(evento));
  }

#if MODO_PRUEBA
  if (millis() - ultimaPruebaMs >= INTERVALO_PRUEBA_MS) {
    ultimaPruebaMs = millis();
    contadorPrueba++;

    EventoRx_t prueba = {};
    prueba.paquete.contador = contadorPrueba;
    strcpy(prueba.paquete.mensaje, "prueba desde gateway");
    enviarAlServidor(construirJson(prueba));
  }
#endif

  actualizarOLED();
  yield();
}
