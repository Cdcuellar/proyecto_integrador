#ifndef SENSORTEMPERATURA_H
#define SENSORTEMPERATURA_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configura el pin del bus OneWire. */
esp_err_t sensortemperatura_inicializar(void);

/*
 * Lee la temperatura en grados C (bloquea ~750 ms por la conversion).
 * Devuelve false si el sensor no responde o el CRC es incorrecto.
 * Asume UN solo DS18B20 en el bus.
 */
bool sensortemperatura_leer(float *temperatura_c);

#ifdef __cplusplus
}
#endif

#endif /* SENSORTEMPERATURA_H */