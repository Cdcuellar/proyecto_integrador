#include "sensorturbidez.h"
#include "app_config.h"

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG_TURB = "TURBIDEZ";

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t         cali_handle = NULL;
static adc_channel_t             canal_adc;

esp_err_t sensorturbidez_inicializar(void)
{
    adc_unit_t unidad;
    esp_err_t err = adc_oneshot_io_to_channel(TURBIDEZ_GPIO, &unidad, &canal_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_TURB, "GPIO%d no es un pin ADC valido", TURBIDEZ_GPIO);
        return err;
    }

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = unidad,
    };
    err = adc_oneshot_new_unit(&init_cfg, &adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_TURB, "Error iniciando ADC: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,         /* rango hasta ~3.1 V */
        .bitwidth = ADC_BITWIDTH_12,
    };
    err = adc_oneshot_config_channel(adc_handle, canal_adc, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_TURB, "Error configurando canal: %s", esp_err_to_name(err));
        return err;
    }

    /* Calibracion para convertir a milivoltios (equivale a analogReadMilliVolts) */
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = unidad,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    err = adc_cali_create_scheme_line_fitting(&cali_cfg, &cali_handle);
#elif ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unidad,
        .chan = canal_adc,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali_handle);
#else
    err = ESP_ERR_NOT_SUPPORTED;
#endif

    if (err != ESP_OK) {
        cali_handle = NULL;
        ESP_LOGW(TAG_TURB, "Sin calibracion de ADC: el voltaje sera aproximado");
    }

    ESP_LOGI(TAG_TURB, "Sensor de turbidez listo en GPIO%d", TURBIDEZ_GPIO);
    return ESP_OK;
}

esp_err_t sensorturbidez_leer(turbidez_lectura_t *lectura)
{
    if (lectura == NULL || adc_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    long suma_raw = 0;
    long suma_mv  = 0;

    for (int i = 0; i < TURBIDEZ_MUESTRAS; i++) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(adc_handle, canal_adc, &raw);
        if (err != ESP_OK) {
            return err;
        }
        suma_raw += raw;

        int mv = 0;
        if (cali_handle != NULL) {
            adc_cali_raw_to_voltage(cali_handle, raw, &mv);
        } else {
            mv = (raw * 3100) / 4095;     /* aproximacion sin calibrar */
        }
        suma_mv += mv;

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    lectura->adc_promedio = (int)(suma_raw / TURBIDEZ_MUESTRAS);
    lectura->voltaje      = (float)(suma_mv / TURBIDEZ_MUESTRAS) / 1000.0f;

    /* Calibracion por tramos (valores experimentales) */
    float v = lectura->voltaje;
    float nivel;

    if (v <= TURBIDEZ_V_TURBIA) {
        nivel = 0.0f;
    } else if (v < TURBIDEZ_V_MEDIA) {
        nivel = (v - TURBIDEZ_V_TURBIA) * 50.0f /
                (TURBIDEZ_V_MEDIA - TURBIDEZ_V_TURBIA);
    } else if (v < TURBIDEZ_V_LIMPIA) {
        nivel = 50.0f + (v - TURBIDEZ_V_MEDIA) * 50.0f /
                (TURBIDEZ_V_LIMPIA - TURBIDEZ_V_MEDIA);
    } else {
        nivel = 100.0f;
    }
    lectura->nivel = nivel;

    if (nivel < 30.0f) {
        lectura->estado = TURBIDEZ_AGUA_TURBIA;
    } else if (nivel < 70.0f) {
        lectura->estado = TURBIDEZ_AGUA_MEDIA;
    } else {
        lectura->estado = TURBIDEZ_AGUA_LIMPIA;
    }

    return ESP_OK;
}

const char *sensorturbidez_estado_texto(turbidez_estado_t estado)
{
    switch (estado) {
        case TURBIDEZ_AGUA_TURBIA: return "AGUA TURBIA";
        case TURBIDEZ_AGUA_MEDIA:  return "AGUA MEDIA";
        default:                   return "AGUA LIMPIA";
    }
}