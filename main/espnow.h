#ifndef ESPNOW_H
#define ESPNOW_H

#include "esp_err.h"
#include "app_types.h"      /* define PaqueteEspNow_t */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Inicializa NVS, WiFi en modo STA, fija el canal, arranca ESP-NOW
 * y registra al receptor como peer (MAC y canal en app_config.h).
 */
esp_err_t espnow_iniciar(void);

/* Envia un paquete al receptor. Devuelve ESP_OK si se encolo el envio. */
esp_err_t espnow_enviar(const PaqueteEspNow_t *paquete);

#ifdef __cplusplus
}
#endif

#endif /* ESPNOW_H */