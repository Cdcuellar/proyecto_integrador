#ifndef SENSORTURBIDEZ_H
#define SENSORTURBIDEZ_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TURBIDEZ_AGUA_TURBIA = 0,
    TURBIDEZ_AGUA_MEDIA,
    TURBIDEZ_AGUA_LIMPIA
} turbidez_estado_t;

typedef struct {
    int               adc_promedio;  /* lectura cruda promedio (0-4095) */
    float             voltaje;       /* voltios, ya calibrado */
    float             nivel;         /* 0 a 100 % (100 = agua limpia) */
    turbidez_estado_t estado;
} turbidez_lectura_t;

/* Configura el ADC y la calibracion. */
esp_err_t sensorturbidez_inicializar(void);

/* Toma TURBIDEZ_MUESTRAS lecturas, promedia y calcula nivel y estado. */
esp_err_t sensorturbidez_leer(turbidez_lectura_t *lectura);

/* Texto del estado, por ejemplo para imprimir o guardar en la SD. */
const char *sensorturbidez_estado_texto(turbidez_estado_t estado);

#ifdef __cplusplus
}
#endif

#endif /* SENSORTURBIDEZ_H */