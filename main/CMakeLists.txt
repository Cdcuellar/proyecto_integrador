idf_component_register(
    SRCS "main.c"
         "tareas.c"
         "clock.c"
         "microsd.c"
         "espnow.c"
         "sensortemperatura.c"
         "sensorcaudal.c"
         "sensorturbidez.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event nvs_flash
             esp_driver_gpio esp_driver_spi esp_driver_sdspi
             fatfs sdmmc esp_timer esp_adc esp_rom freertos)