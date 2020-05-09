/* ESPRESSIF MIT License
 * 
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 * 
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <time.h>
#include <string.h>
#include <esp_https_ota.h>
#include <esp_ota_ops.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_event.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "common.h"
#include "app_wifi.h"
#include "ymodem.h"
#include "spiffs_example_main.h"
#include "cat-eye-debug.h"

/* The examples use WiFi configuration that you can set via 'make menuconfig'.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define DEFAULT_OTA_BUF_SIZE 256

static const char *TAG = "camera wifi";
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

static int s_retry_num = 0;
unsigned char is_connect = FALSE;

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
//    printf("file:%s, line:%d, in event_handler, event->event_id = %d\r\n", __FILE__, __LINE__, event->event_id);
    switch(event->event_id) {
    case SYSTEM_EVENT_AP_STACONNECTED:
//        printf("file:%s, line:%d, SYSTEM_EVENT_AP_STACONNECTED\r\n", __FILE__, __LINE__);
        ESP_LOGI(TAG, "station:" MACSTR " join, AID=%d",
                 MAC2STR(event->event_info.sta_connected.mac),
                 event->event_info.sta_connected.aid);
        break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
//        printf("file:%s, line:%d, SYSTEM_EVENT_AP_STACONNECTED\r\n", __FILE__, __LINE__);
        ESP_LOGI(TAG, "station:" MACSTR "leave, AID=%d",
                 MAC2STR(event->event_info.sta_disconnected.mac),
                 event->event_info.sta_disconnected.aid);
        break;
    case SYSTEM_EVENT_STA_START:
//        printf("file:%s, line:%d, SYSTEM_EVENT_STA_START\r\n", __FILE__, __LINE__);
        esp_wifi_connect();
//        printf("file:%s, line:%d, end esp_wifi_connect\r\n", __FILE__, __LINE__);
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
//        printf("file:%s, line:%d, SYSTEM_EVENT_STA_GOT_IP\r\n", __FILE__, __LINE__);
        ESP_LOGI(TAG, "got ip:%s",
                 ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
        is_connect = TRUE;
        s_retry_num = 0;
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        {
//            printf("file:%s, line:%d, SYSTEM_EVENT_STA_DISCONNECTED\r\n", __FILE__, __LINE__);
            if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
            {
                esp_wifi_connect();
                //s_retry_num++;
                ESP_LOGI(TAG,"retry to connect to the AP, cur_time = %ld", time(NULL));
            }
            ESP_LOGI(TAG,"connect to the AP fail");
            break;
        }
    default:
//        printf("file:%s, line:%d, event->event_id = %d\r\n", __FILE__, __LINE__, event->event_id);
        break;
    }
    return ESP_OK;
}

void wifi_init_softap()
{
    if (strcmp(EXAMPLE_IP_ADDR, "192.168.4.1"))
    {
        int a, b, c, d;
        sscanf(EXAMPLE_IP_ADDR, "%d.%d.%d.%d", &a, &b, &c, &d);
        tcpip_adapter_ip_info_t ip_info;
        IP4_ADDR(&ip_info.ip, a, b, c, d);
        IP4_ADDR(&ip_info.gw, a, b, c, d);
        IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
        ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(WIFI_IF_AP));
        ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(WIFI_IF_AP, &ip_info));
        ESP_ERROR_CHECK(tcpip_adapter_dhcps_start(WIFI_IF_AP));
    }
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    snprintf((char*)wifi_config.ap.ssid, 32, "%s", g_init_data.config_data.wifi_ap_ssid);
    wifi_config.ap.ssid_len = strlen((char*)wifi_config.ap.ssid);
    snprintf((char*)wifi_config.ap.password, 64, "%s", g_init_data.config_data.wifi_ap_key);
    wifi_config.ap.max_connection = 1;
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    if (strlen(g_init_data.config_data.wifi_ap_key) == 0) 
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

//    printf("file:%s, line:%d, in wifi_init_softap\r\n", __FILE__, __LINE__);
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));

    ESP_LOGI(TAG, "wifi_init_softap finished.SSID:%s password:%s",
             g_init_data.config_data.wifi_ap_ssid, g_init_data.config_data.wifi_ap_key);
}

void wifi_init_sta()
{
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    snprintf((char*)wifi_config.sta.ssid, 32, "%s", g_init_data.config_data.wifi_ssid);
    snprintf((char*)wifi_config.sta.password, 64, "%s", g_init_data.config_data.wifi_key);

    printf("file:%s, line:%d, in wifi_init_sta\r\n", __FILE__, __LINE__);
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s",
             g_init_data.config_data.wifi_ssid, g_init_data.config_data.wifi_key);
}

/* add by liuwenjian 2020-3-4 begin */
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
//        printf("file:%s, line:%d, HTTP_EVENT_ERROR\r\n", __FILE__, __LINE__);
        break;
    case HTTP_EVENT_ON_CONNECTED:
//        printf("file:%s, line:%d, HTTP_EVENT_ON_CONNECTED\r\n", __FILE__, __LINE__);
        break;
    case HTTP_EVENT_HEADER_SENT:
//        printf("file:%s, line:%d, HTTP_EVENT_HEADER_SENT\r\n", __FILE__, __LINE__);
        break;
    case HTTP_EVENT_ON_HEADER:
//        printf("file:%s, line:%d, HTTP_EVENT_ON_HEADER, key=%s, value=%s\r\n", 
//            __FILE__, __LINE__, evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
//        printf("file:%s, line:%d, HTTP_EVENT_ON_DATA, len=%d\r\n", __FILE__, __LINE__, evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
//        printf("file:%s, line:%d, HTTP_EVENT_ON_FINISH\r\n", __FILE__, __LINE__);
        break;
    case HTTP_EVENT_DISCONNECTED:
//        printf("file:%s, line:%d, HTTP_EVENT_DISCONNECTED\r\n", __FILE__, __LINE__);
        break;
    }
    return ESP_OK;
}


const char *makeMcuUpgradeUrl(char *upgradeUrl) {
    strcpy(strrchr(upgradeUrl, '/')+1, "camera_mcu.bin");
    return upgradeUrl;
}

static void http_cleanup(esp_http_client_handle_t client)
{
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

static esp_err_t ymodemUpgradeMcu(unsigned int binary_file_len, const void *mcuUpgradeBuf) {
    char recvBuf[128];
    const char *upgradeShakehand = "~UPDATE_SMART_PEEPHOLE";
    ESP_LOGI(TAG, "ymodem upgrade start");
    //vTaskDelay(1000 / portTICK_PERIOD_MS);
    for(int i=0; i<3; i++) {
        int len = uart_read_bytes(ECHO_UART_NUM, (uint8_t *)recvBuf, sizeof(recvBuf), 1000 / portTICK_RATE_MS);
        recvBuf[len] = 0;
        printf("recv: %s\n", recvBuf);
    }
    uart_write_bytes(EX_UART_NUM, upgradeShakehand, strlen(upgradeShakehand));
    return Ymodem_Transmit("mcu.bin", binary_file_len, mcuUpgradeBuf);
}

static esp_err_t airbat_esp_https_ota(const esp_http_client_config_t *config)
{
    const int isMcuUpgrade = (strstr(config->url, "mcu")!=NULL);
    ESP_LOGI(TAG, "begin esp32/mcu upgrade");
    if (!config) {
        ESP_LOGE(TAG, "esp_http_client config not found");
        return ESP_ERR_INVALID_ARG;
    }

#if !CONFIG_OTA_ALLOW_HTTP
    if (!config->cert_pem && !config->use_global_ca_store) {
        ESP_LOGE(TAG, "Server certificate not found, either through configuration or global CA store");
        return ESP_ERR_INVALID_ARG;
    }
#endif

    esp_http_client_handle_t client = esp_http_client_init(config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise HTTP connection");
        return ESP_FAIL;
    }

#if !CONFIG_OTA_ALLOW_HTTP
    if (esp_http_client_get_transport_type(client) != HTTP_TRANSPORT_OVER_SSL) {
        ESP_LOGE(TAG, "Transport is not over HTTPS");
        return ESP_FAIL;
    }
#endif

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        return err;
    }
    esp_http_client_fetch_headers(client);

    esp_ota_handle_t update_handle = 0;
    const esp_partition_t *update_partition = NULL;
    ESP_LOGI(TAG, "Starting OTA...");

    if(!isMcuUpgrade) {
        update_partition = esp_ota_get_next_update_partition(NULL);
        if (update_partition == NULL) {
            ESP_LOGE(TAG, "Passive OTA partition not found");
            http_cleanup(client);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%x",
                 update_partition->subtype, update_partition->address);

        err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_begin failed, error=%d", err);
            http_cleanup(client);
            return err;
        }
        ESP_LOGI(TAG, "esp_ota_begin succeeded");
        ESP_LOGI(TAG, "Please Wait. This may take time");
    }
    esp_err_t ota_write_err = ESP_OK;
    const int alloc_size = (config->buffer_size > 0) ? config->buffer_size : DEFAULT_OTA_BUF_SIZE;
    char *upgrade_data_buf = (char *)malloc(alloc_size);
    if (!upgrade_data_buf) {
        ESP_LOGE(TAG, "Couldn't allocate memory to upgrade data buffer");
        return ESP_ERR_NO_MEM;
    }

    int binary_file_len = 0;

    /* 10k buffer for MCU bin file */
    char *mcuUpgradeBuf = NULL;
    if(isMcuUpgrade) {
        mcuUpgradeBuf = (char *)malloc(10*1024);
        if (!mcuUpgradeBuf) {
            ESP_LOGE(TAG, "Couldn't allocate memory to MCU upgrade data buffer");
            return ESP_ERR_NO_MEM;
        }
    }
    while (1) {
        int data_read = esp_http_client_read(client, upgrade_data_buf, alloc_size);
        if (data_read == 0) {
            ESP_LOGI(TAG, "Connection closed, all data received");
            break;
        }
        if (data_read < 0) {
            ESP_LOGE(TAG, "Error: SSL data read error");
            break;
        }
        if (data_read > 0) {
            if(isMcuUpgrade) {
                memcpy(mcuUpgradeBuf + binary_file_len, upgrade_data_buf, data_read);
            } else {
                ota_write_err = esp_ota_write(update_handle, (const void *) upgrade_data_buf, data_read);
                if (ota_write_err != ESP_OK) {
                    break;
                }
            }
            binary_file_len += data_read;
            ESP_LOGD(TAG, "Written image length %d", binary_file_len);
        }
    }
    ESP_LOGI(TAG, "Total binary data length writen: %d", binary_file_len);
    free(upgrade_data_buf);
    http_cleanup(client); 

    /* ymodem 升级 */
    if(mcuUpgradeBuf) {
        extern bool g_update_mcu;
        g_update_mcu = true;
        err = ymodemUpgradeMcu(binary_file_len, mcuUpgradeBuf);
        free(mcuUpgradeBuf);
        g_update_mcu = false;
        return err;
    }
    
    esp_err_t ota_end_err = esp_ota_end(update_handle);
    if (ota_write_err != ESP_OK) {
        ESP_LOGE(TAG, "Error: esp_ota_write failed! err=0x%d", err);
        return ota_write_err;
    } else if (ota_end_err != ESP_OK) {
        ESP_LOGE(TAG, "Error: esp_ota_end failed! err=0x%d. Image is invalid", ota_end_err);
        return ota_end_err;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed! err=0x%d", err);
        return err;
    }
    ESP_LOGI(TAG, "esp_ota_set_boot_partition succeeded"); 

    return ESP_OK;
}

void simple_ota_example_task(void *pvParameter)
{
    char upgradeUrl[80];
    const char *version = "camera_1";
//    ESP_LOGI(TAG, "Starting OTA example");

    versionToUpgradeUrl(version, upgradeUrl);
    printf("|---------------------ver: %s---------------------|\n", version);
//    printf("file:%s, line:%d, begin config\r\n", __FILE__, __LINE__);
    esp_http_client_config_t config = {
        .url = upgradeUrl,
        .cert_pem = (char *)server_cert_pem_start,
        .event_handler = _http_event_handler,
    };

    while (FALSE == is_connect)
    {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }

//    printf("file:%s, line:%d, begin esp_https_ota\r\n", __FILE__, __LINE__);
    g_update_flag = TRUE;
    printf("upgrade url: %s\n", upgradeUrl);
    esp_err_t ret = airbat_esp_https_ota(&config);
//    printf("file:%s, line:%d, begin esp_https_ota, ret = %d\r\n", __FILE__, __LINE__, ret);
    if (ret == ESP_OK)
    {
        esp_restart();
    }
    else 
    {
#if  0
        makeMcuUpgradeUrl(upgradeUrl);
        printf("upgrade url: %s\n", upgradeUrl);
        ret = airbat_esp_https_ota(&config);

        /* mcu升级是否成功都结�?*/
        if(ESP_OK == ret) {
            printf("Upgrade mcu OKAY~~~~~\n");
        } else {
            ESP_LOGE(TAG, "No Upgrade Executed or Upgrade Failed\n");
        }
#endif
        g_update_flag = FALSE;
    }
    vTaskDelete(NULL);
}
/* add by liuwenjian 2020-3-4 end */

void app_wifi_main()
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_mode_t mode = WIFI_MODE_NULL;

    printf("file:%s, line:%d, in app_wifi_main\r\n", __FILE__, __LINE__);

    if (strlen(g_init_data.config_data.wifi_ap_ssid) && strlen(g_init_data.config_data.wifi_ssid))
    {
        mode = WIFI_MODE_APSTA;
    }
    else if (strlen(g_init_data.config_data.wifi_ap_ssid))
    {
        mode = WIFI_MODE_AP;
    }
    else if (strlen(g_init_data.config_data.wifi_ssid))
    {
        mode = WIFI_MODE_STA;
    }

/*    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);*/

    if (mode == WIFI_MODE_NULL)
    {
        ESP_LOGW(TAG,"Neither AP or STA have been configured. WiFi will be off.");
        return;
    }

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(mode));

    if (mode & WIFI_MODE_AP)
    {
        wifi_init_softap();
    }

    if (mode & WIFI_MODE_STA)
    {
        wifi_init_sta();
    }
    ESP_ERROR_CHECK(esp_wifi_start());

//    printf("file:%s, line:%d, begin simple_ota_example_task\r\n", __FILE__, __LINE__);
    /* add by liuwenjian 2020-3-4 begin */
    /* ota ���� */
    xTaskCreate(&simple_ota_example_task, "ota_example_task", 8192, NULL, 5, NULL);
    /* add by liuwenjian 2020-3-4 end */
}

