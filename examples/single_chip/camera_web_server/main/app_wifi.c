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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

/* The examples use WiFi configuration that you can set via 'make menuconfig'.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/

static const char *TAG = "camera wifi";
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

static int s_retry_num = 0;
static unsigned char is_connect = FALSE;

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

void simple_ota_example_task(void *pvParameter)
{
//    ESP_LOGI(TAG, "Starting OTA example");

//    printf("file:%s, line:%d, begin config\r\n", __FILE__, __LINE__);
    esp_http_client_config_t config = {
        .url = CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL,
        .cert_pem = (char *)server_cert_pem_start,
        .event_handler = _http_event_handler,
    };

    while (FALSE == is_connect)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

//    printf("file:%s, line:%d, begin esp_https_ota\r\n", __FILE__, __LINE__);
    g_update_flag = TRUE;
    esp_err_t ret = esp_https_ota(&config);
//    printf("file:%s, line:%d, begin esp_https_ota, ret = %d\r\n", __FILE__, __LINE__, ret);
    if (ret == ESP_OK)
    {
        esp_restart();
    }
    else 
    {
        g_update_flag = FALSE;
        ESP_LOGE(TAG, "Firmware upgrade failed");
    }
    
    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
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
    /* ota Éý¼¶ */
    xTaskCreate(&simple_ota_example_task, "ota_example_task", 8192, NULL, 5, NULL);
    /* add by liuwenjian 2020-3-4 end */
}

