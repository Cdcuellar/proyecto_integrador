#include "esp_log.h"

#include "app_config.h"
#include "clock.h"
#include "microsd.h"
#include "sensortemperatura.h"
#include "sensorcaudal.h"
#include "sensorturbidez.h"
#include "espnow.h"
#include "tareas.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    /* Reloj primero: asi todos los registros llevan fecha y hora */
    if (clock_inicializar() == ESP_OK) {
        clock_sincronizar_sistema();
    } else {
        ESP_LOGW(TAG, "Reloj no disponible: las marcas de tiempo seran 0");
    }

    if (sd_logger_inicializar() != ESP_OK) {
        ESP_LOGW(TAG, "Sin microSD: se continua sin registro");
    }

    sensortemperatura_inicializar();
    sensorcaudal_inicializar();
    sensorturbidez_inicializar();

    if (espnow_iniciar() != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW no inicio");
        return;
    }

    tareas_iniciar();   /* app_main termina, las tareas siguen corriendo */
}