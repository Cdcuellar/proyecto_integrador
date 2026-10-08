#ifndef ESPNOW_COMUN_H
#define ESPNOW_COMUN_H

#include <stdint.h>

/* Ambos nodos DEBEN usar el mismo canal */
#define ESPNOW_CHANNEL 9

/*
 * MAC del RECEPTOR. Se imprime en el monitor serial al arrancar
 * el receptor ("MAC propia: ..."). Cámbiala por la tuya. ------------ MAC: B8:D6:1A:42:4D:30
 */
#define ESPNOW_MAC_RECEPTOR {0xB8, 0xD6, 0x1A, 0x42, 0x47, 0xB4}

/*
 * Paquete que viaja por ESP-NOW (máx. 250 bytes).
 * Por ahora solo un mensaje; después reemplaza/añade campos de sensores,
 * por ejemplo: int16_t bpm; float spo2; float ax, ay, az;
 */
typedef struct __attribute__((packed)) {
  uint32_t contador;      /* número de secuencia */
  char     mensaje[32];   /* texto de prueba */
  float    temperatura;   /* °C del sensor DS18B20 */
} PaqueteEspNow_t;

#endif /* ESPNOW_COMUN_H */