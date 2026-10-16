#ifndef TAREAS_H
#define TAREAS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Crea las tareas de FreeRTOS del emisor:
 *   - temperatura:      lee el DS18B20 cada 1 s
 *   - caudal_turb:      lee caudal y turbidez cada 1 s
 *   - envio:            cada 1 s manda el paquete por ESP-NOW y
 *                       guarda una linea en la microSD con fecha y hora
 *
 * Llamar una sola vez, despues de inicializar los modulos.
 */
void tareas_iniciar(void);

#ifdef __cplusplus
}
#endif

#endif /* TAREAS_H */