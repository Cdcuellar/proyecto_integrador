#include "sensortemperatura.h"
#include "app_config.h"

#include <stdint.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Si app_config.h solo define SENSOR_GPIO, se usa ese valor. */
#ifndef TEMPERATURA_GPIO
#  ifdef SENSOR_GPIO
#    define TEMPERATURA_GPIO SENSOR_GPIO
#  else
#    error "Define TEMPERATURA_GPIO (o SENSOR_GPIO) en app_config.h"
#  endif
#endif

static const char *TAG_TEMP = "TEMPERATURA";

#define PIN ((gpio_num_t)TEMPERATURA_GPIO)

static portMUX_TYPE ow_mux = portMUX_INITIALIZER_UNLOCKED;

typedef enum {
    OW_OK = 0,
    OW_BUS_BAJO,        /* la linea esta pegada a GND antes del reset */
    OW_SIN_PRESENCIA    /* la linea esta alta pero nadie responde */
} ow_estado_t;

/* ------------------------------------------------------------
 * Driver OneWire minimo
 * ------------------------------------------------------------ */
static ow_estado_t ow_reset(void)
{
    /* 1) El bus en reposo debe estar en alto (por la resistencia). */
    gpio_set_level(PIN, 1);
    esp_rom_delay_us(10);
    if (gpio_get_level(PIN) == 0) {
        return OW_BUS_BAJO;
    }

    /* 2) Pulso de reset. */
    gpio_set_level(PIN, 0);
    esp_rom_delay_us(480);

    /* 3) Soltar la linea y buscar el pulso de presencia.
     *    El DS18B20 lo genera entre 15 y 60 us despues de soltar. */
    bool presente = false;
    portENTER_CRITICAL(&ow_mux);
    gpio_set_level(PIN, 1);
    esp_rom_delay_us(15);
    for (int i = 0; i < 60; i++) {
        if (gpio_get_level(PIN) == 0) {
            presente = true;
            break;
        }
        esp_rom_delay_us(1);
    }
    portEXIT_CRITICAL(&ow_mux);

    /* 4) Completar el slot de reset (>= 480 us tras soltar). */
    esp_rom_delay_us(470);
    return presente ? OW_OK : OW_SIN_PRESENCIA;
}

static void ow_write_bit(int bit)
{
    portENTER_CRITICAL(&ow_mux);
    gpio_set_level(PIN, 0);
    esp_rom_delay_us(bit ? 6 : 60);
    gpio_set_level(PIN, 1);
    esp_rom_delay_us(bit ? 64 : 10);
    portEXIT_CRITICAL(&ow_mux);
}

static int ow_read_bit(void)
{
    portENTER_CRITICAL(&ow_mux);
    gpio_set_level(PIN, 0);
    esp_rom_delay_us(3);
    gpio_set_level(PIN, 1);
    esp_rom_delay_us(10);
    int v = gpio_get_level(PIN);
    portEXIT_CRITICAL(&ow_mux);
    esp_rom_delay_us(53);
    return v;
}

static void ow_write_byte(uint8_t b)
{
    for (int i = 0; i < 8; i++) {
        ow_write_bit(b & 0x01);
        b >>= 1;
    }
}

static uint8_t ow_read_byte(void)
{
    uint8_t b = 0;
    for (int i = 0; i < 8; i++) {
        b |= (uint8_t)(ow_read_bit() << i);
    }
    return b;
}

static uint8_t ow_crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0;
    for (int i = 0; i < len; i++) {
        uint8_t b = data[i];
        for (int j = 0; j < 8; j++) {
            uint8_t mix = (crc ^ b) & 0x01;
            crc >>= 1;
            if (mix) crc ^= 0x8C;
            b >>= 1;
        }
    }
    return crc;
}

static bool reset_con_log(const char *fase)
{
    ow_estado_t st = ow_reset();
    if (st == OW_BUS_BAJO) {
        ESP_LOGW(TAG_TEMP,
                 "[%s] Bus DATA en BAJO (GPIO%d): corto a GND, DATA en pin equivocado o sensor danado",
                 fase, TEMPERATURA_GPIO);
        return false;
    }
    if (st == OW_SIN_PRESENCIA) {
        ESP_LOGW(TAG_TEMP,
                 "[%s] Sin pulso de presencia (GPIO%d): sensor sin alimentar, DATA sin conectar o resistencia mal",
                 fase, TEMPERATURA_GPIO);
        return false;
    }
    return true;
}

/* ------------------------------------------------------------
 * API
 * ------------------------------------------------------------ */
esp_err_t sensortemperatura_inicializar(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << TEMPERATURA_GPIO),
        .mode         = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en   = GPIO_PULLUP_ENABLE,     /* ayuda a la resistencia externa de 4.7k */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_TEMP, "Error configurando GPIO%d", TEMPERATURA_GPIO);
        return err;
    }
    gpio_set_level(PIN, 1);
    ESP_LOGI(TAG_TEMP, "DS18B20 listo en GPIO%d", TEMPERATURA_GPIO);
    return ESP_OK;
}

bool sensortemperatura_leer(float *temperatura_c)
{
    if (temperatura_c == NULL) return false;

    if (!reset_con_log("inicio")) return false;
    ow_write_byte(0xCC);               /* Skip ROM */
    ow_write_byte(0x44);               /* Convert T */
    vTaskDelay(pdMS_TO_TICKS(800));    /* 12 bits: ~750 ms max */

    if (!reset_con_log("lectura")) return false;
    ow_write_byte(0xCC);
    ow_write_byte(0xBE);               /* Read Scratchpad */

    uint8_t sp[9];
    for (int i = 0; i < 9; i++) sp[i] = ow_read_byte();

    if (ow_crc8(sp, 8) != sp[8]) {
        ESP_LOGW(TAG_TEMP,
                 "CRC incorrecto. Scratchpad: %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 sp[0], sp[1], sp[2], sp[3], sp[4], sp[5], sp[6], sp[7], sp[8]);
        return false;
    }

    int16_t raw = (int16_t)((sp[1] << 8) | sp[0]);
    float t = raw / 16.0f;

    if (t < -55.0f || t > 125.0f) {
        ESP_LOGW(TAG_TEMP, "Temperatura fuera de rango: %.2f", t);
        return false;
    }
    if (raw == 0x0550) {
        ESP_LOGW(TAG_TEMP, "Lectura 85.0 C = valor de arranque; la conversion pudo no completarse (alimentacion?)");
    }

    *temperatura_c = t;
    return true;
}