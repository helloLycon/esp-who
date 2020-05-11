/* SPIFFS filesystem example.
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "spiffs_example_main.h"
#include "common.h"

static const char *TAG = "example";

RunLog run_log = {0};
static const char *filename = "/spiffs/log";

static bool is_reading = false;

void spiffs_exam_app_main(void)
{
    ESP_LOGI(TAG, "Initializing SPIFFS");
    
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 5,
      .format_if_mount_failed = true
    };
    
    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }
    
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    // Use POSIX and C standard library functions to work with files.
    // First create a file.
    ESP_LOGI(TAG, "Opening file");
    FILE* f = fopen("/spiffs/hello.txt", "w");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    fprintf(f, "Hello World!\n");
    fclose(f);
    ESP_LOGI(TAG, "File written");

    // Check if destination file exists before renaming
    struct stat st;
    if (stat("/spiffs/foo.txt", &st) == 0) {
        // Delete it if it exists
        unlink("/spiffs/foo.txt");
    }

    // Rename original file
    ESP_LOGI(TAG, "Renaming file");
    if (rename("/spiffs/hello.txt", "/spiffs/foo.txt") != 0) {
        ESP_LOGE(TAG, "Rename failed");
        return;
    }

    // Open renamed file for reading
    ESP_LOGI(TAG, "Reading file");
    f = fopen("/spiffs/foo.txt", "r");
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

    // All done, unmount partition and disable SPIFFS
    esp_vfs_spiffs_unregister(NULL);
    ESP_LOGI(TAG, "SPIFFS unmounted");
}

int spiffs_init(void) {
    ESP_LOGI(TAG, "Initializing SPIFFS");
    
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = NULL,
      .max_files = 3,
      .format_if_mount_failed = true
    };
    
    // Use settings defined above to initialize and mount SPIFFS filesystem.
    // Note: esp_vfs_spiffs_register is an all-in-one convenience function.
    esp_err_t ret = esp_vfs_spiffs_register(&conf);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }
    
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
        return ESP_FAIL;
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
        
        // Check if destination file exists before renaming
        struct stat st;
        if (stat(filename, &st) == 0) {
            printf("filesize of %s: %ld\n", filename, st.st_size);
        }
        return ESP_OK;
    }
}

void spiffs_deinit(void) {
    // All done, unmount partition and disable SPIFFS
    esp_vfs_spiffs_unregister(NULL);
    ESP_LOGI(TAG, "SPIFFS unmounted");
}

int run_log_write(void) {
    extern unsigned char g_pic_send_over;

    /* record boot time */
    run_log.boot = time(NULL) - xTaskGetTickCount()/configTICK_RATE_HZ;
    if(is_reading) {
        return 0;
    }

    /* send failed */
    portENTER_CRITICAL(&g_pic_send_over_spinlock);
    if(!g_pic_send_over) {
        SET_LOG(send_fail);
    }
    portEXIT_CRITICAL(&g_pic_send_over_spinlock);

    int ret = spiffs_init();
    if(ret != ESP_OK) {
        goto fail2;
    }

    FILE* f = fopen(filename, "a+");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing");
        goto fail1;
    }

    size_t sz = fwrite(&run_log, 1, sizeof(RunLog), f);
    if(sz != sizeof(RunLog)) {
        ESP_LOGE(TAG, "Failed to writing %d", sz);
        goto fail1;
    }

    ESP_LOGI(TAG, "%s: OKAY", __func__);
fail1:
    fclose(f);
fail2:
    spiffs_deinit();
    return ESP_OK;
}

void run_log_dump(int f, const RunLog *p) {
    struct tm tmv;
    localtime_r(&p->boot, &tmv);
    dprintf(f, "=-> %d-%d %d:%d:%d\n", tmv.tm_mon+1, tmv.tm_mday, tmv.tm_hour,tmv.tm_min,tmv.tm_sec);
    if(p->connect_wifi) {
        dprintf(f, "%d: connect wifi\n", p->connect_wifi);
    }
    if(p->connect_server) {
        dprintf(f, "%d: connect server\n",p->connect_server);
    }
    if(p->camera_over) {
        dprintf(f, "%d: camera over\n", p->camera_over);
    }
    if(p->send_over) {
        dprintf(f, "%d: send over\n",p->send_over);
    }
    if(p->send_fail) {
        dprintf(f, "%d: send failed\n",p->send_fail);
    }
}

int run_log_read(int sock) {
    is_reading = true;
    int ret = spiffs_init();
    if(ret != ESP_OK) {
        goto rfail2;
    }

    FILE* f = fopen(filename, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file for read");
        goto rfail1;
    }
    
    for(;;) {
        RunLog buf;
        size_t sz = fread(&buf, 1, sizeof(RunLog), f);
        if(sz != sizeof(RunLog)) {
            dprintf(sock, "over~~~\n");
            goto rfail1;
        }
        run_log_dump(sock, &buf);
    }
    dprintf(sock, "over...\n");
    ESP_LOGI(TAG, "%s: OKAY", __func__);
rfail1:
    fclose(f);
    unlink(filename);
rfail2:
    spiffs_deinit();
    return ESP_OK;
}
