#include "clock.h"
#include "app_config.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/*
 * Modulo "MH-Real-Time Clock": chip DS1302 (comunicacion de 3 hilos).
 * Pines: VCC, GND, CLK, DAT, RST (RST es el CE del DS1302).
 */

static const char *TAG_CLK = "CLOCK";

/* Direcciones de registros (el comando es 0x80 | dir<<1 | lectura) */
#define REG_CONTROL   7     /* bit 7 = proteccion contra escritura */
#define REG_CARGA     8     /* cargador de carga lenta (trickle) */
#define CMD_BURST_LEER    0xBF
#define CMD_BURST_ESCRIBIR 0xBE

static SemaphoreHandle_t mutex_clock = NULL;

/* ------------------------------------------------------------
 * Utilidades
 * ------------------------------------------------------------ */
static inline uint8_t bcd_a_bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static inline uint8_t bin_a_bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static inline void espera(void) { esp_rom_delay_us(2); }

static void dat_salida(void)  { gpio_set_direction(CLOCK_DAT_GPIO, GPIO_MODE_OUTPUT); }
static void dat_entrada(void) { gpio_set_direction(CLOCK_DAT_GPIO, GPIO_MODE_INPUT); }

/* ------------------------------------------------------------
 * Protocolo de 3 hilos (bit-banging, bit menos significativo primero)
 * ------------------------------------------------------------ */
static void ds_inicio(void)
{
    gpio_set_level(CLOCK_CLK_GPIO, 0);
    gpio_set_level(CLOCK_RST_GPIO, 1);
    esp_rom_delay_us(4);
}

static void ds_fin(void)
{
    gpio_set_level(CLOCK_RST_GPIO, 0);
    gpio_set_level(CLOCK_CLK_GPIO, 0);
    esp_rom_delay_us(4);
}

/* Si leer_despues es true, suelta la linea DAT justo antes del ultimo
 * flanco de bajada para que el DS1302 pueda responder. */
static void ds_escribir_byte(uint8_t b, bool leer_despues)
{
    dat_salida();
    for (int i = 0; i < 8; i++) {
        gpio_set_level(CLOCK_DAT_GPIO, (b >> i) & 0x01);
        espera();
        gpio_set_level(CLOCK_CLK_GPIO, 1);
        espera();
        if (i == 7 && leer_despues) {
            dat_entrada();
        }
        gpio_set_level(CLOCK_CLK_GPIO, 0);
        espera();
    }
}

/* primero = true solo para el primer byte tras el comando: su bit 0 ya
 * esta disponible despues del flanco de bajada del comando. */
static uint8_t ds_leer_byte(bool primero)
{
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) {
        if (!(primero && i == 0)) {
            gpio_set_level(CLOCK_CLK_GPIO, 1);
            espera();
            gpio_set_level(CLOCK_CLK_GPIO, 0);
            espera();
        }
        if (gpio_get_level(CLOCK_DAT_GPIO)) {
            v |= (uint8_t)(1 << i);
        }
    }
    return v;
}

static void ds_escribir_reg(uint8_t dir, uint8_t valor)
{
    ds_inicio();
    ds_escribir_byte((uint8_t)(0x80 | (dir << 1)), false);
    ds_escribir_byte(valor, false);
    ds_fin();
}

static void ds_leer_burst(uint8_t out[8])
{
    ds_inicio();
    ds_escribir_byte(CMD_BURST_LEER, true);
    for (int i = 0; i < 8; i++) {
        out[i] = ds_leer_byte(i == 0);
    }
    ds_fin();
    dat_salida();
    gpio_set_level(CLOCK_DAT_GPIO, 0);
}

static void ds_escribir_burst(const uint8_t datos[8])
{
    ds_inicio();
    ds_escribir_byte(CMD_BURST_ESCRIBIR, false);
    for (int i = 0; i < 8; i++) {
        ds_escribir_byte(datos[i], false);
    }
    ds_fin();
}

/* ------------------------------------------------------------
 * Validacion y conversion
 * ------------------------------------------------------------ */
static bool hora_es_valida(const clock_hora_t *h)
{
    return h->anio >= 2000 && h->anio <= 2099 &&
           h->mes >= 1 && h->mes <= 12 &&
           h->dia >= 1 && h->dia <= 31 &&
           h->hora <= 23 && h->minuto <= 59 && h->segundo <= 59;
}

static bool bcd_valido(uint8_t v)
{
    return (v & 0x0F) <= 9 && (v >> 4) <= 9;
}

static void a_tm(const clock_hora_t *h, struct tm *t)
{
    memset(t, 0, sizeof(*t));
    t->tm_year = h->anio - 1900;
    t->tm_mon  = h->mes - 1;
    t->tm_mday = h->dia;
    t->tm_hour = h->hora;
    t->tm_min  = h->minuto;
    t->tm_sec  = h->segundo;
}

/* Devuelve ESP_OK si el reloj corre y la hora es valida */
static esp_err_t leer_interno(clock_hora_t *h, bool *detenido)
{
    uint8_t r[8];
    ds_leer_burst(r);

    if (detenido != NULL) {
        *detenido = (r[0] & 0x80) != 0;     /* bit CH: oscilador detenido */
    }

    /* Un bus sin modulo suele leer todo 0x00 o todo 0xFF */
    if (!bcd_valido(r[0] & 0x7F) || !bcd_valido(r[1] & 0x7F) ||
        !bcd_valido(r[3]) || !bcd_valido(r[4]) || !bcd_valido(r[6])) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    h->segundo = bcd_a_bin(r[0] & 0x7F);
    h->minuto  = bcd_a_bin(r[1] & 0x7F);

    if (r[2] & 0x80) {                          /* modo 12 horas */
        uint8_t hh = bcd_a_bin(r[2] & 0x1F) % 12;
        h->hora = (r[2] & 0x20) ? hh + 12 : hh;
    } else {                                    /* modo 24 horas */
        h->hora = bcd_a_bin(r[2] & 0x3F);
    }

    h->dia        = bcd_a_bin(r[3] & 0x3F);
    h->mes        = bcd_a_bin(r[4] & 0x1F);
    h->dia_semana = r[5] & 0x07;
    h->anio       = 2000 + bcd_a_bin(r[6]);

    return hora_es_valida(h) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

/* ------------------------------------------------------------
 * API publica
 * ------------------------------------------------------------ */
esp_err_t clock_leer(clock_hora_t *hora)
{
    if (hora == NULL || mutex_clock == NULL) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(mutex_clock, pdMS_TO_TICKS(500)) != pdTRUE) return ESP_ERR_TIMEOUT;

    bool detenido = false;
    esp_err_t err = leer_interno(hora, &detenido);
    if (err == ESP_OK && detenido) err = ESP_ERR_INVALID_STATE;

    xSemaphoreGive(mutex_clock);
    return err;
}

esp_err_t clock_ajustar(const clock_hora_t *hora)
{
    if (hora == NULL || mutex_clock == NULL) return ESP_ERR_INVALID_STATE;
    if (!hora_es_valida(hora)) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(mutex_clock, pdMS_TO_TICKS(500)) != pdTRUE) return ESP_ERR_TIMEOUT;

    /* Calcula el dia de la semana (tm_wday: 0 = domingo) */
    struct tm t;
    a_tm(hora, &t);
    mktime(&t);
    uint8_t dow = (uint8_t)(t.tm_wday + 1);

    ds_escribir_reg(REG_CONTROL, 0x00);         /* quita proteccion de escritura */
    ds_escribir_reg(REG_CARGA, 0x00);           /* sin carga: pila CR2032 normal */

    uint8_t d[8] = {
        (uint8_t)(bin_a_bcd(hora->segundo) & 0x7F),   /* CH = 0: oscilador en marcha */
        bin_a_bcd(hora->minuto),
        bin_a_bcd(hora->hora),                        /* bit 7 = 0: modo 24 horas */
        bin_a_bcd(hora->dia),
        bin_a_bcd(hora->mes),
        dow,
        bin_a_bcd((uint8_t)(hora->anio - 2000)),
        0x80                                          /* vuelve a proteger */
    };
    ds_escribir_burst(d);

    xSemaphoreGive(mutex_clock);
    return ESP_OK;
}

esp_err_t clock_ajustar_desde_compilacion(void)
{
    static const char *meses = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mes_txt[4] = {0};
    int dia = 0, anio = 0, hh = 0, mm = 0, ss = 0;

    if (sscanf(__DATE__, "%3s %d %d", mes_txt, &dia, &anio) != 3 ||
        sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss) != 3) {
        return ESP_FAIL;
    }

    const char *p = strstr(meses, mes_txt);
    if (p == NULL) return ESP_FAIL;

    clock_hora_t h = {
        .anio = (uint16_t)anio,
        .mes = (uint8_t)(((p - meses) / 3) + 1),
        .dia = (uint8_t)dia,
        .hora = (uint8_t)hh,
        .minuto = (uint8_t)mm,
        .segundo = (uint8_t)ss,
        .dia_semana = 1,
    };
    return clock_ajustar(&h);
}

esp_err_t clock_inicializar(void)
{
    if (mutex_clock == NULL) {
        mutex_clock = xSemaphoreCreateMutex();
        if (mutex_clock == NULL) return ESP_ERR_NO_MEM;
    }

    gpio_reset_pin(CLOCK_CLK_GPIO);
    gpio_reset_pin(CLOCK_DAT_GPIO);
    gpio_reset_pin(CLOCK_RST_GPIO);
    gpio_set_direction(CLOCK_CLK_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(CLOCK_RST_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(CLOCK_CLK_GPIO, 0);
    gpio_set_level(CLOCK_RST_GPIO, 0);
    dat_salida();
    gpio_set_level(CLOCK_DAT_GPIO, 0);

    clock_hora_t h;
    bool detenido = false;

    xSemaphoreTake(mutex_clock, portMAX_DELAY);
    esp_err_t err = leer_interno(&h, &detenido);
    xSemaphoreGive(mutex_clock);

    if (err != ESP_OK || detenido) {
        ESP_LOGW(TAG_CLK, "Reloj detenido o con hora invalida: se ajusta con la hora de compilacion");
        if (clock_ajustar_desde_compilacion() != ESP_OK) {
            ESP_LOGE(TAG_CLK, "No se pudo ajustar el reloj");
            return ESP_FAIL;
        }
    }

    if (clock_leer(&h) != ESP_OK) {
        ESP_LOGE(TAG_CLK, "El modulo no responde: revisa VCC, GND, CLK, DAT y RST");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG_CLK, "Reloj listo: %04u-%02u-%02u %02u:%02u:%02u (CLK=%d DAT=%d RST=%d)",
             h.anio, h.mes, h.dia, h.hora, h.minuto, h.segundo,
             CLOCK_CLK_GPIO, CLOCK_DAT_GPIO, CLOCK_RST_GPIO);
    return ESP_OK;
}

esp_err_t clock_texto(char *buffer, size_t tam)
{
    if (buffer == NULL || tam < 20) return ESP_ERR_INVALID_ARG;

    clock_hora_t h;
    esp_err_t err = clock_leer(&h);
    if (err != ESP_OK) {
        snprintf(buffer, tam, "0000-00-00 00:00:00");
        return err;
    }
    snprintf(buffer, tam, "%04u-%02u-%02u %02u:%02u:%02u",
             h.anio, h.mes, h.dia, h.hora, h.minuto, h.segundo);
    return ESP_OK;
}

esp_err_t clock_epoch(uint32_t *segundos)
{
    if (segundos == NULL) return ESP_ERR_INVALID_ARG;

    clock_hora_t h;
    esp_err_t err = clock_leer(&h);
    if (err != ESP_OK) return err;

    struct tm t;
    a_tm(&h, &t);
    *segundos = (uint32_t)mktime(&t);
    return ESP_OK;
}

esp_err_t clock_sincronizar_sistema(void)
{
    uint32_t s;
    esp_err_t err = clock_epoch(&s);
    if (err != ESP_OK) return err;

    struct timeval tv = { .tv_sec = (time_t)s, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) != 0) return ESP_FAIL;

    ESP_LOGI(TAG_CLK, "Reloj del sistema sincronizado con el modulo");
    return ESP_OK;
}