#include "sensorcaudal.h"
#include "app_config.h"

#include "esp_log.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "driver/gpio.h"

static const char *TAG_CAUDAL = "CAUDAL";

static volatile uint32_t contador_pulsos = 0;
static int64_t ultima_lectura_us = 0;

/* Interrupcion: se ejecuta en cada pulso del sensor */
static void IRAM_ATTR contar_pulsos_isr(void *arg)
{
    (void)arg;
    __atomic_fetch_add(&contador_pulsos, 1, __ATOMIC_RELAXED);
}

esp_err_t sensorcaudal_inicializar(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << CAUDAL_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_CAUDAL, "Error configurando GPIO: %s", esp_err_to_name(err));
        return err;
    }

    /* El servicio de ISR puede estar ya instalado por otro modulo */
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG_CAUDAL, "Error instalando servicio ISR: %s", esp_err_to_name(err));
        return err;
    }

    err = gpio_isr_handler_add(CAUDAL_GPIO, contar_pulsos_isr, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_CAUDAL, "Error agregando handler: %s", esp_err_to_name(err));
        return err;
    }

    contador_pulsos = 0;
    ultima_lectura_us = esp_timer_get_time();

    ESP_LOGI(TAG_CAUDAL, "Sensor de caudal listo en GPIO%d", CAUDAL_GPIO);
    return ESP_OK;
}

void sensorcaudal_leer(uint32_t *pulsos, float *caudal_lpm)
{
    /* Lee y reinicia el contador en una sola operacion atomica */
    uint32_t p = __atomic_exchange_n(&contador_pulsos, 0, __ATOMIC_RELAXED);

    int64_t ahora = esp_timer_get_time();
    float segundos = (float)(ahora - ultima_lectura_us) / 1000000.0f;
    ultima_lectura_us = ahora;

    /* Frecuencia (Hz) dividida por el factor del sensor = L/min */
    float frecuencia = (segundos > 0.0f) ? ((float)p / segundos) : 0.0f;

    if (pulsos != NULL)     *pulsos = p;
    if (caudal_lpm != NULL) *caudal_lpm = frecuencia / CAUDAL_FACTOR;
}