#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdint.h>

/*
 * Paquete que viaja por ESP-NOW (max. 250 bytes).
 * Debe ser IDENTICO en el emisor y en el receptor (Gateway).
 * Un sensor sin lectura valida se envia como NaN (turbidez_estado = 255).
 */
typedef struct __attribute__((packed)) {
    uint32_t contador;          /* numero de secuencia */
    uint32_t epoch;             /* segundos desde 1970 segun el reloj (0 si fallo) */
    float    temperatura;       /* grados C (DS18B20) */
    float    caudal;            /* L/min */
    float    turbidez_nivel;    /* 0 a 100 % (100 = agua limpia) */
    uint8_t  turbidez_estado;   /* 0 turbia, 1 media, 2 limpia, 255 sin dato */
} PaqueteEspNow_t;

#endif /* APP_TYPES_H */