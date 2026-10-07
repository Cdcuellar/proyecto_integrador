#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "espnow_comun.h"

static uint8_t macReceptor[6] = ESPNOW_MAC_RECEPTOR;

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
    Serial.printf("Enviado #%lu: \"%s\"\n",
                  (unsigned long)paquete->contador, paquete->mensaje);
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

  PaqueteEspNow_t paquete;
  memset(&paquete, 0, sizeof(paquete));
  paquete.contador = contador++;
  snprintf(paquete.mensaje, sizeof(paquete.mensaje), "Hola desde el emisor");

  /* Aquí luego llenarás los campos con los datos de los sensores */
  espnowEnviar(&paquete);

  delay(1000);
}