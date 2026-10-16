#include "microsd.h"
#include "app_config.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

static const char *TAG_SD = "SD_LOGGER";

static bool sd_disponible = false;
static sdmmc_card_t *tarjeta_sd = NULL;

esp_err_t sd_logger_inicializar(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG_SD, "Iniciando microSD...");
    ESP_LOGI(TAG_SD, "MOSI=%d MISO=%d SCK=%d CS=%d",
             SD_MOSI_GPIO, SD_MISO_GPIO, SD_SCK_GPIO, SD_CS_GPIO);

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SD_MOSI_GPIO,
        .miso_io_num = SD_MISO_GPIO,
        .sclk_io_num = SD_SCK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000
    };

    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG_SD, "Error iniciando bus SPI: %s", esp_err_to_name(ret));
        return ret;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS_GPIO;
    slot_config.host_id = host.slot;

    ret = esp_vfs_fat_sdspi_mount(
        SD_MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &tarjeta_sd
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SD, "No se pudo montar la microSD: %s", esp_err_to_name(ret));
        sd_disponible = false;
        return ret;
    }

    sd_disponible = true;

    ESP_LOGI(TAG_SD, "MicroSD montada correctamente en %s", SD_MOUNT_POINT);
    sdmmc_card_print_info(stdout, tarjeta_sd);

    /* Prueba inmediata de escritura */
    FILE *f = fopen(SD_LOG_FILE_PATH, "a");
    if (f == NULL) {
        ESP_LOGE(TAG_SD, "ERROR: no se pudo abrir el archivo de log: %s", SD_LOG_FILE_PATH);
        sd_disponible = false;
        return ESP_FAIL;
    }

    fprintf(f, "\n========== INICIO DE REGISTRO ==========\n");
    fprintf(f, "Prueba de escritura inicial OK\n");
    fclose(f);

    ESP_LOGI(TAG_SD, "Archivo de log listo: %s", SD_LOG_FILE_PATH);

    /* Verificar que realmente exista */
    struct stat st;
    if (stat(SD_LOG_FILE_PATH, &st) == 0) {
        ESP_LOGI(TAG_SD, "Archivo verificado. Tamano actual: %ld bytes", (long)st.st_size);
    } else {
        ESP_LOGE(TAG_SD, "El archivo no pudo verificarse despues de crearlo");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void sd_logger_cerrar(void)
{
    if (sd_disponible) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, tarjeta_sd);
        sd_disponible = false;
        ESP_LOGI(TAG_SD, "MicroSD desmontada");
    }
}

bool sd_logger_esta_disponible(void)
{
    return sd_disponible;
}

void sd_logger_printf(const char *formato, ...)
{
    va_list args;
    char buffer[256];

    va_start(args, formato);
    vsnprintf(buffer, sizeof(buffer), formato, args);
    va_end(args);

    /* 1) serial */
    printf("%s", buffer);

    /* 2) SD */
    if (!sd_disponible) {
        ESP_LOGW(TAG_SD, "SD no disponible, no se escribe: %s", buffer);
        return;
    }

    FILE *f = fopen(SD_LOG_FILE_PATH, "a");
    if (f == NULL) {
        ESP_LOGE(TAG_SD, "No se pudo abrir archivo de log para append");
        return;
    }

    fprintf(f, "%s", buffer);
    fflush(f);
    fclose(f);
}