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
#include "video-queue.h"

static const char *TAG = "sdcard";
static const char *tag = "sdcard";
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
    esp_err_t ret;
    upgrade_block();
    xSemaphoreTake(sd_log_mutex, portMAX_DELAY);
    if(NULL == logbuf) {
        xSemaphoreGive(sd_log_mutex);
        return ESP_FAIL;
    }

#if 0
    esp_err_t ret = sdcard_init();
    if(ret != ESP_OK) {
        goto fail2;
    }
#endif

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
        [LOG_CONNECT_WIFI] = "连上wifi",
        [LOG_CONNECT_SERVER] = "连上服务器",
        [LOG_CAMERA_OVER] = "拍摄结束",
        [LOG_SEND_OVER] = "发送数据完成",
        [LOG_SEND_FAIL] = "发送数据失败",
        [LOG_LOW_BATTERY] = "低电量",
        [LOG_CONFIGURATION] = "蓝牙配置模式",
    };
    if(type == LOG_CONFIGURATION) {
        logbuf_offset = 0;
    }
    char tmp[32];
    logbuf_offset += sprintf(logbuf+logbuf_offset, "[%s] %s\n", log_make_uptime(tmp), str_list[type]);
    xSemaphoreGive(sd_log_mutex);
    return 0;
}

int log_printf(const char *format, ...) {
    xSemaphoreTake(sd_log_mutex, portMAX_DELAY);
    if(NULL == logbuf) {
        xSemaphoreGive(sd_log_mutex);
        return 0;
    }
    char tmp[32];
    logbuf_offset += sprintf(logbuf+logbuf_offset, "[%s] ", log_make_uptime(tmp));

    va_list vlist;
    va_start(vlist, format);
    logbuf_offset += vsprintf(logbuf+logbuf_offset, format, vlist);
    va_end(vlist);

    logbuf_offset += sprintf(logbuf+logbuf_offset, "\n");
    xSemaphoreGive(sd_log_mutex);
    return 0;
}


pic_queue *save_pic_pointer = NULL;
FILE *sdfp = NULL;
FILE *rd_sdfp = NULL;

int wr_sdcard_fp_open(bool force_reinit, time_t t) {
    if(force_reinit) {
        /* 重新打开文件 */
        if(sdfp) {
            fclose(sdfp);
            sdfp = NULL;
        }
    }
    if(NULL == sdfp) {
        char tmp[64];
        //video_queue *v = pic->video;
        sdfp = fopen(mk_sd_time_fname(t, tmp), "w");
        if(NULL == sdfp) {
            ESP_LOGE(TAG, "fopen failed in %s", __func__);
            vTaskDelete(NULL);
        }
        return ESP_OK;
    }
    return ESP_OK;
}

void wr_sdcard_fp_close(void) {
    if(sdfp) {
        fclose(sdfp);
        sdfp = NULL;
    }
}

int rd_sdcard_fp_open(bool force_reinit, time_t t) {
    if(force_reinit) {
        /* 重新打开文件 */
        if(rd_sdfp) {
            fclose(rd_sdfp);
            rd_sdfp = NULL;
        }
    }
    if(NULL == rd_sdfp) {
        char tmp[64];
        //video_queue *v = pic->video;
        rd_sdfp = fopen(mk_sd_time_fname(t, tmp), "r");
        if(NULL == rd_sdfp) {
            ESP_LOGE(TAG, "fopen failed in %s", __func__);
            vTaskDelete(NULL);
        }
        return ESP_OK;
    }
    return ESP_OK;
}

void rd_sdcard_fp_close(void) {
    if(rd_sdfp) {
        fclose(rd_sdfp);
        rd_sdfp = NULL;
    }
}

const char *mk_sd_fname(const char *name, char *tmp) {
    sprintf(tmp, "/sdcard/%s", name);
    return tmp;
}

const char *mk_sd_time_fname(time_t t, char *tmp) {
    char timestr[32];
    mk_win_time_str(t, timestr);
    return mk_sd_fname(timestr, tmp);
}

int read_one_pic_from_sdcard(pic_queue *pic) {
    video_queue *vid = pic->video;
    rd_sdcard_fp_open(false, vid->time);
    if(fseek(rd_sdfp, pic->offset, SEEK_SET)<0) {
        ESP_LOGE(tag, "fseek failed in %s", __func__);
        return ESP_FAIL;
    }
    if(fread(pic->pic_info, 1, pic->pic_len, rd_sdfp) != pic->pic_len) {
        ESP_LOGE(tag, "fread failed in %s", __func__);
        return ESP_FAIL;
    }
    return ESP_OK;
}

int save_one_pic_into_sdcard(pic_queue *pic) {
    video_queue *video = pic->video;
    wr_sdcard_fp_open(false, video->time);

    //sdfp = fopen(mk_sd_time_fname(v->time, tmp), "w");
    size_t writesz = PIC_DATA_OFFSET + pic->pic_len;
    size_t sz = fwrite(pic, 1, writesz, sdfp);
    if(sz != writesz) {
        ESP_LOGE(tag, "fwrite failed in %s", __func__);
        return ESP_FAIL;
    }
    /* 马上flush分担负载 */
    fflush(sdfp);
    return ESP_OK;
}

void sd_complete_video(void) {
    wr_sdcard_fp_close();
    mv_video2sdcard(vq_tail);
}

void abort_sdcard_task(void) {
    //sd_complete_video();
    vTaskDelete(NULL);
}

void sd_handle_pic(pic_queue *pic) {
    if(pic->pic_len) {
        int err = save_one_pic_into_sdcard(pic);
        if(err != ESP_OK) {
            /* end */
            abort_sdcard_task();
        }
    } else {
        sd_complete_video();
    }
}


void save_video_into_sdcard_task(void *arg) {
    sdcard_init();
    for(;;) {
        /* wait for condition */
        xSemaphoreTake(vq_save_trigger, portMAX_DELAY);
        for(;;) {
            /* 保存图片 */
            if(save_pic_pointer) {
                sd_handle_pic(save_pic_pointer);
            } else {
                //sd_complete_video();
                /* 不会到这里 */
                break;
            }
            /* 尝试下一张图片 */
            video_queue *video = save_pic_pointer->video;
            if(save_pic_pointer->next) {
                /* next picture in same video */
                save_pic_pointer = save_pic_pointer->next;
            } else if(video->next && video->next->head_pic) {
                /* new video */
                save_pic_pointer = video->next->head_pic;
                wr_sdcard_fp_open(true, video->next->time);
            } else {
                /* nothing else */
                save_pic_pointer = NULL;
                break;
            }
        }


    }
}

