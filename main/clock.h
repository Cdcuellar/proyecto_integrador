#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t anio;         /* 2000 a 2099 */
    uint8_t  mes;          /* 1 a 12 */
    uint8_t  dia;          /* 1 a 31 */
    uint8_t  hora;         /* 0 a 23 */
    uint8_t  minuto;       /* 0 a 59 */
    uint8_t  segundo;      /* 0 a 59 */
    uint8_t  dia_semana;   /* 1 = domingo ... 7 = sabado */
} clock_hora_t;

/*
 * Configura los pines, comprueba el reloj y, si esta detenido o con una
 * hora invalida, lo ajusta con la fecha/hora de compilacion.
 * Devuelve ESP_OK si el modulo responde y la hora es valida.
 */
esp_err_t clock_inicializar(void);

/* Lee la fecha y hora del modulo. */
esp_err_t clock_leer(clock_hora_t *hora);

/* Ajusta la fecha y hora del modulo. */
esp_err_t clock_ajustar(const clock_hora_t *hora);

/* Ajusta el modulo con la fecha/hora en que se compilo el programa. */
esp_err_t clock_ajustar_desde_compilacion(void);

/* Escribe "AAAA-MM-DD HH:MM:SS" en buffer (minimo 20 bytes). */
esp_err_t clock_texto(char *buffer, size_t tam);

/* Segundos desde 1970-01-01 (la hora del modulo se trata como UTC). */
esp_err_t clock_epoch(uint32_t *segundos);

/*
 * Copia la hora del modulo al reloj interno del ESP32, para que time(),
 * localtime() y gettimeofday() den la hora correcta en todo el programa.
 */
esp_err_t clock_sincronizar_sistema(void);

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_H */