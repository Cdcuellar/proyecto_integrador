#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "tareas.h"
#include "app_config.h"
#include "clock.h"
#include "microsd.h"
#include "espnow.h"
#include "sensortemperatura.h"
#include "sensorcaudal.h"
#include "sensorturbidez.h"

static const char *TAG = "TAREAS";

#define PERIODO_MS 1000

/* Ultimos valores validos de los sensores (NAN = sin lectura valida). */
typedef struct {
    float             temperatura;       /* grados C */
    float             caudal;            /* L/min */
    uint32_t          pulsos;            /* pulsos contados en el ultimo periodo */
    float             turbidez_nivel;    /* 0 a 100 % (100 = agua limpia) */
    float             turbidez_voltaje;  /* voltios en la entrada del ADC */
    int               turbidez_adc;      /* lectura cruda 0-4095 (-1 = fallo) */
    turbidez_estado_t turbidez_estado;
} lecturas_t;

static lecturas_t lecturas = {
    .temperatura      = NAN,
    .caudal           = 0.0f,
    .pulsos           = 0,
    .turbidez_nivel   = NAN,
    .turbidez_voltaje = NAN,
    .turbidez_adc     = -1,
    .turbidez_estado  = TURBIDEZ_AGUA_TURBIA,
};

static SemaphoreHandle_t lecturas_mutex = NULL;

/* ------------------------------------------------------------
 * Tarea: temperatura (DS18B20)
 * La lectura bloquea ~800 ms por la conversion; por eso va sola.
 * El motivo de cualquier fallo lo imprime sensortemperatura.c.
 * ------------------------------------------------------------ */
static void tarea_temperatura(void *arg)
{
    TickType_t t0 = xTaskGetTickCount();

    while (true) {
        float t = NAN;
        if (!sensortemperatura_leer(&t)) {
            t = NAN;
        }

        xSemaphoreTake(lecturas_mutex, portMAX_DELAY);
        lecturas.temperatura = t;
        xSemaphoreGive(lecturas_mutex);

        vTaskDelayUntil(&t0, pdMS_TO_TICKS(PERIODO_MS));
    }
}

/* ------------------------------------------------------------
 * Tarea: caudal y turbidez
 * sensorcaudal_leer() calcula L/min con los pulsos desde la llamada
 * anterior, asi que se llama con periodo fijo (vTaskDelayUntil).
 * ------------------------------------------------------------ */
static void tarea_caudal_turbidez(void *arg)
{
    TickType_t t0 = xTaskGetTickCount();

    while (true) {
        uint32_t pulsos = 0;
        float caudal = 0.0f;
        sensorcaudal_leer(&pulsos, &caudal);

        turbidez_lectura_t tb;
        bool tb_ok = (sensorturbidez_leer(&tb) == ESP_OK);
        if (!tb_ok) {
            ESP_LOGW(TAG, "Lectura de turbidez fallida");
        }

        xSemaphoreTake(lecturas_mutex, portMAX_DELAY);
        lecturas.caudal = caudal;
        lecturas.pulsos = pulsos;
        if (tb_ok) {
            lecturas.turbidez_nivel   = tb.nivel;
            lecturas.turbidez_voltaje = tb.voltaje;
            lecturas.turbidez_adc     = tb.adc_promedio;
            lecturas.turbidez_estado  = tb.estado;
        } else {
            lecturas.turbidez_nivel   = NAN;
            lecturas.turbidez_voltaje = NAN;
            lecturas.turbidez_adc     = -1;
        }
        xSemaphoreGive(lecturas_mutex);

        vTaskDelayUntil(&t0, pdMS_TO_TICKS(PERIODO_MS));
    }
}

/* ------------------------------------------------------------
 * Tarea: envio por ESP-NOW y registro en la SD
 * ------------------------------------------------------------ */
static void tarea_envio(void *arg)
{
    uint32_t contador = 0;
    TickType_t t0 = xTaskGetTickCount();

    while (true) {
        /* 1. Copiar los ultimos valores de los sensores */
        lecturas_t copia;
        xSemaphoreTake(lecturas_mutex, portMAX_DELAY);
        copia = lecturas;
        xSemaphoreGive(lecturas_mutex);

        /* Resumen legible de todos los sensores (borrar si molesta) */
        ESP_LOGI(TAG, "T=%.2f C | Q=%.2f L/min (%lu pulsos) | Turb=%.1f %% (%.3f V, ADC=%d, %s)",
                 copia.temperatura, copia.caudal, (unsigned long)copia.pulsos,
                 copia.turbidez_nivel, copia.turbidez_voltaje, copia.turbidez_adc,
                 sensorturbidez_estado_texto(copia.turbidez_estado));

        /* 2. Armar y enviar el paquete por ESP-NOW */
        PaqueteEspNow_t paquete = { 0 };
        paquete.contador = contador++;
        paquete.temperatura = copia.temperatura;
        esp_err_t err = espnow_enviar(&paquete);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ESP-NOW: envio fallido (%s)", esp_err_to_name(err));
        }

        /* 3. Guardar en la SD con la fecha y hora */
        char ts[24];
        if (clock_texto(ts, sizeof(ts)) != ESP_OK) {
            snprintf(ts, sizeof(ts), "0000-00-00 00:00:00");
        }
        sd_logger_printf("%s,%.2f,%.2f,%.1f\n",
                         ts, copia.temperatura, copia.caudal, copia.turbidez_nivel);

        vTaskDelayUntil(&t0, pdMS_TO_TICKS(PERIODO_MS));
    }
}

/* ------------------------------------------------------------
 * API
 * ------------------------------------------------------------ */
void tareas_iniciar(void)
{
    lecturas_mutex = xSemaphoreCreateMutex();
    if (lecturas_mutex == NULL) {
        ESP_LOGE(TAG, "No se pudo crear el mutex de lecturas");
        return;
    }

    bool ok = true;
    ok &= (xTaskCreate(tarea_temperatura,     "temperatura", 4096, NULL, 5, NULL) == pdPASS);
    ok &= (xTaskCreate(tarea_caudal_turbidez, "caudal_turb", 4096, NULL, 5, NULL) == pdPASS);
    ok &= (xTaskCreate(tarea_envio,           "envio",       6144, NULL, 5, NULL) == pdPASS);

    if (!ok) {
        ESP_LOGE(TAG, "No se pudo crear alguna tarea (memoria insuficiente)");
    }
}