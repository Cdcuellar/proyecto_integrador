#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "espnow_comun.h"

static uint8_t macReceptor[6] = ESPNOW_MAC_RECEPTOR;

/* ============================================================
 * SENSOR DE TEMPERATURA DS18B20
 * DATA en GPIO17 (con resistencia de 4.7k entre DATA y 3.3V)
 * ============================================================ */
#define SENSOR_PIN 18

OneWire oneWire(SENSOR_PIN);
DallasTemperature DS18B20(&oneWire);

/* ============================================================
 * CALLBACK DE ENVÍO
 * (la firma cambió en el core ESP32 3.3.0)
 * ============================================================ */
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 0)
void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status)
#else
void onDataSent(const uint8_t *mac, esp_now_send_status_t status)
#endif
{
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("Entrega exitosa");
  } else {
    Serial.println("Fallo en la entrega");
  }
}

/* ============================================================
 * ENVÍO DE PAQUETE
 * ============================================================ */
void espnowEnviar(const PaqueteEspNow_t *paquete) {
  if (paquete == NULL) {
    return;
  }

  esp_err_t r = esp_now_send(macReceptor,
                             (const uint8_t *)paquete,
                             sizeof(PaqueteEspNow_t));
  if (r == ESP_OK) {
    Serial.printf("Enviado #%lu: \"%s\"  Temp: %.2f C\n",
                  (unsigned long)paquete->contador,
                  paquete->mensaje,
                  paquete->temperatura);
  } else {
    Serial.printf("Error enviando: %s\n", esp_err_to_name(r));
  }
}

/* ============================================================
 * SETUP
 * ============================================================ */
void setup() {
  Serial.begin(115200);
  delay(1000);

  DS18B20.begin();   /* inicia el sensor de temperatura */

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  Serial.print("MAC propia: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error inicializando ESP-NOW");
    return;
  }
  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, macReceptor, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.ifidx   = WIFI_IF_STA;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Error registrando peer receptor");
    return;
  }

  Serial.printf("ESP-NOW listo - canal %d\n", ESPNOW_CHANNEL);
}

/* ============================================================
 * LOOP
 * ============================================================ */
void loop() {
  static uint32_t contador = 0;

  /* Leer temperatura */
  DS18B20.requestTemperatures();
  float tempC = DS18B20.getTempCByIndex(0);

  /* Si el sensor no responde, no se envía un dato falso */
  if (tempC == DEVICE_DISCONNECTED_C) {
    Serial.println("Sensor de temperatura no detectado: revisa cables y resistencia");
    delay(1000);
    return;
  }

  PaqueteEspNow_t paquete;
  memset(&paquete, 0, sizeof(paquete));
  paquete.contador    = contador++;
  paquete.temperatura = tempC;
  snprintf(paquete.mensaje, sizeof(paquete.mensaje), "Sensor de temperatura");

  espnowEnviar(&paquete);

  delay(1000);
}
