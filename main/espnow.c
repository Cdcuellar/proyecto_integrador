#include "espnow.h"
#include "app_config.h"

#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "nvs_flash.h"

static const char *TAG_NOW = "ESPNOW";
static uint8_t mac_receptor[6] = ESPNOW_MAC_RECEPTOR;

/* ============================================================
 * CALLBACK DE ENVIO
 * El primer parametro cambia entre versiones de ESP-IDF y aqui no
 * se usa, por eso se declara generico y se castea al registrarlo.
 * ============================================================ */
static void espnow_on_data_sent(const void *info, esp_now_send_status_t status)
{
    (void)info;
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGI(TAG_NOW, "Entrega exitosa");
    } else {
        ESP_LOGW(TAG_NOW, "Fallo en la entrega");
    }
}

/* ============================================================
 * INICIALIZACION
 * ============================================================ */
esp_err_t espnow_iniciar(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());

    /* Puede existir ya si otro modulo lo creo */
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG_NOW, "Error creando bucle de eventos: %s", esp_err_to_name(err));
        return err;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    ESP_LOGI(TAG_NOW, "MAC propia: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG_NOW, "Error inicializando ESP-NOW: %s", esp_err_to_name(err));
        return err;
    }

    esp_now_register_send_cb((esp_now_send_cb_t)espnow_on_data_sent);

    esp_now_peer_info_t peer;
    memset(&peer, 0, sizeof(peer));
    memcpy(peer.peer_addr, mac_receptor, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;

    err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_NOW, "Error registrando peer receptor: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG_NOW, "ESP-NOW listo - canal %d", ESPNOW_CHANNEL);
    return ESP_OK;
}

/* ============================================================
 * ENVIO
 * ============================================================ */
esp_err_t espnow_enviar(const PaqueteEspNow_t *paquete)
{
    if (paquete == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = esp_now_send(mac_receptor,
                                 (const uint8_t *)paquete,
                                 sizeof(PaqueteEspNow_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG_NOW, "Error enviando: %s", esp_err_to_name(err));
    }
    return err;
}