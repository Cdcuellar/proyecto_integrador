#ifndef MICROSD_H
#define MICROSD_H

#include <stdbool.h>
#include "esp_err.h"

esp_err_t sd_logger_inicializar(void);
void sd_logger_cerrar(void);
bool sd_logger_esta_disponible(void);
void sd_logger_printf(const char *formato, ...);

#endif