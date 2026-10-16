#ifndef SENSORCAUDAL_H
#define SENSORCAUDAL_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configura el pin del sensor y la interrupcion por flanco de subida. */
esp_err_t sensorcaudal_inicializar(void);

/*
 * Devuelve los pulsos contados desde la lectura anterior y el caudal
 * calculado en L/min. Reinicia el contador. Llamala periodicamente
 * (por ejemplo cada 1 segundo).
 */
void sensorcaudal_leer(uint32_t *pulsos, float *caudal_lpm);

#ifdef __cplusplus
}
#endif

#endif /* SENSORCAUDAL_H */