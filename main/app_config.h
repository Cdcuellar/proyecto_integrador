#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ---------- microSD (SPI, bus VSPI) ---------- */
#define SD_MOSI_GPIO      23
#define SD_MISO_GPIO      19
#define SD_SCK_GPIO       18
#define SD_CS_GPIO        5
#define SD_MOUNT_POINT    "/sdcard"
#define SD_LOG_FILE_PATH  "/sdcard/temp.txt"

/* ---------- Sensor DS18B20 ---------- */
#define SENSOR_GPIO       4

/* ---------- ESP-NOW ---------- */
#define ESPNOW_CHANNEL    9
#define ESPNOW_MAC_RECEPTOR {0xB8, 0xD6, 0x1A, 0x42, 0x47, 0xB4}

/* ---------- Sensor de caudal (agregar a app_config.h) ---------- */
#define CAUDAL_GPIO       27      /* pin de senal del sensor */
#define CAUDAL_FACTOR     98.0f   /* Hz por L/min (YF-S401: F = 98 x Q) */

/* ---------- Sensor de turbidez (agregar a app_config.h) ---------- */
#define TURBIDEZ_GPIO       34      /* ADC1, solo entrada */
#define TURBIDEZ_MUESTRAS   50

/* Voltajes obtenidos experimentalmente */
#define TURBIDEZ_V_LIMPIA   1.021f
#define TURBIDEZ_V_MEDIA    0.957f
#define TURBIDEZ_V_TURBIA   0.904f

/* ---------- Reloj MH-Real-Time Clock / DS1302 (agregar a app_config.h) ---------- */
#define CLOCK_CLK_GPIO    25
#define CLOCK_DAT_GPIO    26
#define CLOCK_RST_GPIO    33

#endif /* APP_CONFIG_H */
