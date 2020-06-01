/* SD card and FAT filesystem example.
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_camera.h"
#include "app_camera.h"
#include "esp_sleep.h"
#include "sd_card_example_main.h"
#include "common.h"

static const char *TAG = "sdcard";
static const char *filename = "/sdcard/log";

static char *logbuf;
static const int logbuf_size = 1024;
static int logbuf_offset = 0;

static xSemaphoreHandle sd_log_mutex;
// This example can use SDMMC and SPI peripherals to communicate with SD card.
// By default, SDMMC peripheral is used.
// To enable SPI mode, uncomment the following line:

// #define USE_SPI_MODE

// When testing SD and SPI modes, keep in mind that once the card has been
// initialized in SPI mode, it can not be reinitialized in SD mode without
// toggling power to the card.

esp_err_t sdcard_init(void)
{
    /*------------------camera down-------------------*/
    esp_camera_deinit();
    cam_power_down();
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_reset_pin(2);
    gpio_reset_pin(4);
    gpio_reset_pin(12);
    gpio_reset_pin(13);
    gpio_reset_pin(14);
    gpio_reset_pin(15);

    /*------------------start init sdcard-------------------*/
    ESP_LOGI(TAG, "Initializing SD card");

    ESP_LOGI(TAG, "Using SDMMC peripheral");
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    // To use 1-line SD mode, uncomment the following line:
    // slot_config.width = 1;

    // GPIOs 15, 2, 4, 12, 13 should have external 10k pull-ups.
    // Internal pull-ups are not sufficient. However, enabling internal pull-ups
    // does make a difference some boards, so we do that here.
    gpio_set_pull_mode(15, GPIO_PULLUP_ONLY);   // CMD, needed in 4- and 1- line modes
    gpio_set_pull_mode(2, GPIO_PULLUP_ONLY);    // D0, needed in 4- and 1-line modes
    gpio_set_pull_mode(4, GPIO_PULLUP_ONLY);    // D1, needed in 4-line mode only
    gpio_set_pull_mode(12, GPIO_PULLUP_ONLY);   // D2, needed in 4-line mode only
    gpio_set_pull_mode(13, GPIO_PULLUP_ONLY);   // D3, needed in 4- and 1-line modes

    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 10,
        .allocation_unit_size = 16 * 1024
    };

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc_mount is an all-in-one convenience function.
    // Please check its source code and implement error recovery when developing
    // production applications.
    sdmmc_card_t* card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                "If you want the card to be formatted, set format_if_mount_failed = true.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return ret;
    }

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);
    return ESP_OK;

#if 0
    // Use POSIX and C standard library functions to work with files.
    // First create a file.
    ESP_LOGI(TAG, "Opening file");
    FILE* f = fopen("/sdcard/hello.txt", "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    fprintf(f, "Hello %s!\n", card->cid.name);
    fclose(f);
    ESP_LOGI(TAG, "File written");

    // Check if destination file exists before renaming
    struct stat st;
    if (stat("/sdcard/foo.txt", &st) == 0) {
        // Delete it if it exists
        unlink("/sdcard/foo.txt");
    }

    // Rename original file
    ESP_LOGI(TAG, "Renaming file");
    if (rename("/sdcard/hello.txt", "/sdcard/foo.txt") != 0) {
        ESP_LOGE(TAG, "Rename failed");
        return;
    }

    // Open renamed file for reading
    ESP_LOGI(TAG, "Reading file");
    f = fopen("/sdcard/foo.txt", "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading");
        return;
    }
    char line[64];
    fgets(line, sizeof(line), f);
    fclose(f);
    // strip newline
    char* pos = strchr(line, '\n');
    if (pos) {
        *pos = '\0';
    }
    ESP_LOGI(TAG, "Read from file: '%s'", line);
#endif

    // All done, unmount partition and disable SDMMC or SPI peripheral
    //esp_vfs_fat_sdmmc_unmount();
    //ESP_LOGI(TAG, "Card unmounted");
}

void sdcard_deinit(void) {
    // All done, unmount partition and disable SDMMC or SPI peripheral
    esp_vfs_fat_sdmmc_unmount();
    ESP_LOGI(TAG, "Card unmounted");
}

int __sdcard_test(void) {
    esp_camera_deinit();
    cam_power_down();
    vTaskDelay(100 / portTICK_PERIOD_MS);
    gpio_reset_pin(2);
    gpio_reset_pin(4);
    gpio_reset_pin(12);
    gpio_reset_pin(13);
    gpio_reset_pin(14);
    gpio_reset_pin(15);
    printf("-----------> init sdcard\n");
    sdcard_init_main();
    
    vTaskDelay(1000000 / portTICK_PERIOD_MS);
    return 0;
}

char *sdcard_log_init(void) {
    logbuf = (char *)malloc(logbuf_size);
    if(NULL == logbuf) {
        ESP_LOGE(TAG, "alloc logbuf failed");
        return NULL;
    }
    sd_log_mutex = xSemaphoreCreateMutex();
    if(NULL == sd_log_mutex) {
        ESP_LOGE(TAG, "sd_log_mutex");
        free(logbuf);
        logbuf = NULL;
        return NULL;
    }

#if 0
    struct tm tmv;
    time_t t = time(NULL) - xTaskGetTickCount()/configTICK_RATE_HZ;
    localtime_r(&t, &tmv);
    logbuf_offset += sprintf(logbuf, "[%d-%d-%d %d:%02d:%02d]\n", tmv.tm_year+1900, tmv.tm_mon+1, tmv.tm_mday, tmv.tm_hour,tmv.tm_min,tmv.tm_sec);
#endif
    return logbuf;
}


esp_err_t sdcard_log_write(void) {
    upgrade_block();
    xSemaphoreTake(sd_log_mutex, portMAX_DELAY);
    if(NULL == logbuf) {
        xSemaphoreGive(sd_log_mutex);
        return ESP_FAIL;
    }

    esp_err_t ret = sdcard_init();
    if(ret != ESP_OK) {
        goto fail2;
    }

    FILE* f = fopen(filename, "a");
    if (f == NULL) {
        ret = ESP_FAIL;
        ESP_LOGE(TAG, "Failed to open file for writing");
        goto fail1;
    }

    struct tm tmv;
    time_t t = time(NULL) - xTaskGetTickCount()/configTICK_RATE_HZ;
    localtime_r(&t, &tmv);
    char tmp[64];
    sprintf(tmp, "[%d-%d-%d %d:%02d:%02d]\n", tmv.tm_year+1900, tmv.tm_mon+1, tmv.tm_mday, tmv.tm_hour,tmv.tm_min,tmv.tm_sec);
    size_t sz = fwrite(tmp, 1, strlen(tmp), f);
    if(sz != strlen(tmp)) {
        ret = ESP_FAIL;
        ESP_LOGE(TAG, "Failed to write date(%d)", sz);
        goto fail1;
    }

    sz = fwrite(logbuf, 1, strlen(logbuf), f);
    if(sz != strlen(logbuf)) {
        ret = ESP_FAIL;
        ESP_LOGE(TAG, "Failed to write (%d)", sz);
        goto fail1;
    }

    ESP_LOGI(TAG, "%s: OKAY", __func__);
fail1:
    fclose(f);
fail2:
    sdcard_deinit();
    free(logbuf);
    logbuf = NULL;
    xSemaphoreGive(sd_log_mutex);
    return ret;
}

const char *log_make_uptime(char *s) {
    char tmp[32];
    uint32_t uptime = xTaskGetTickCount();
    sprintf(tmp, "%6d", uptime);
    tmp[6] = tmp[5];
    tmp[5] = tmp[4];
    tmp[4] = '.';
    tmp[7] = '\0';
    strcpy(s, tmp);
    return s;
}

int log_enum(enum log_type type) {
    xSemaphoreTake(sd_log_mutex, portMAX_DELAY);
    if(NULL == logbuf) {
        xSemaphoreGive(sd_log_mutex);
        return 0;
    }
    static const char * const str_list[] = {
        [LOG_CONNECT_WIFI] = "connect wifi",
        [LOG_CONNECT_SERVER] = "connect server",
        [LOG_CAMERA_OVER] = "camera capture over",
        [LOG_SEND_OVER] = "send data over",
        [LOG_SEND_FAIL] = "send data failed",
        [LOG_LOW_BATTERY] = "low battery",
    };
    char tmp[32];
    logbuf_offset += sprintf(logbuf+logbuf_offset, "[%s] %s\n", log_make_uptime(tmp), str_list[type]);
    xSemaphoreGive(sd_log_mutex);
    return 0;
}

