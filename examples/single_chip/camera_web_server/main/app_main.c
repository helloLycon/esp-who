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

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "app_camera.h"
#include "app_wifi.h"
#include "app_httpd.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
//#include "esp_spiffs.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "common.h"
#include "i2c_example_main.h"
#include "adc1_example_main.h"

#define ECHO_TEST_TXD   (GPIO_NUM_1)
#define ECHO_TEST_RXD   (GPIO_NUM_3)
#define ECHO_TEST_RTS   (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS   (UART_PIN_NO_CHANGE)
#define BUF_SIZE        (256)

bool g_camera_over = false;
unsigned char g_pic_send_over = FALSE;
unsigned char g_update_flag = FALSE;
init_info g_init_data;

int max_sleep_uptime = DEF_MAX_SLEEP_TIME;

void nop(void) {
    while(1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void upgrade_block(void) {
    if(TRUE == g_update_flag) {
        nop();
    }
}


/* add by liuwenjian 2020-3-4 begin */
void init_para(bool erase_all)
{
    bool fix = false;
    nvs_handle my_handle;
    esp_err_t err;
    
    memset(&g_init_data, 0, sizeof(config_para));

    /* 打开nvs 文件系统 */
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK)
    {
        g_init_data.config_data.service_port = TCP_PORT;
        strcpy(g_init_data.config_data.service_ip_str, TCP_SERVER_ADRESS);
        strcpy(g_init_data.config_data.device_id, DEVICE_INFO);
        return ;
    }

    if(erase_all) {
        nvs_erase_all(my_handle);
        nvs_commit(my_handle);
    }

    /* 获取设备基本配置 */
    // Read run time blob
    size_t required_size = sizeof(config_para);  // value will default to 0, if not set yet in NVS
    // obtain required memory space to store blob being read from NVS
    err = nvs_get_blob(my_handle, "device_info", &(g_init_data.config_data), &required_size);
    if (err != ESP_OK )
    {
        printf("=-> reset all settings\n");
        fix = true;
        g_init_data.config_data.service_port = TCP_PORT;
        strcpy(g_init_data.config_data.service_ip_str, TCP_SERVER_ADRESS);
        strcpy(g_init_data.config_data.device_id, DEVICE_INFO);
        strcpy(g_init_data.config_data.wifi_ssid, EXAMPLE_ESP_WIFI_SSID);
        strcpy(g_init_data.config_data.wifi_key, EXAMPLE_ESP_WIFI_PASS);
        strcpy(g_init_data.config_data.wifi_ap_ssid, EXAMPLE_ESP_WIFI_AP_SSID);
        strcpy(g_init_data.config_data.wifi_ap_key, EXAMPLE_ESP_WIFI_AP_PASS);
        g_init_data.config_data.ir_voltage = IR_VOL_UNSET;
    }

    if (0 == g_init_data.config_data.service_port)
    {
        fix = true;
        g_init_data.config_data.service_port = TCP_PORT;
    }
    
    if (0 == g_init_data.config_data.service_ip_str[0])
    {
        fix = true;
        strcpy(g_init_data.config_data.service_ip_str, TCP_SERVER_ADRESS);
    }

    if (0 == g_init_data.config_data.device_id[0])
    {
        fix = true;
        strcpy(g_init_data.config_data.device_id, DEVICE_INFO);
    }
    if( memchr(g_init_data.config_data.wifi_ssid, 0, sizeof(g_init_data.config_data.wifi_ssid)) == NULL ) {
        fix = true;
        strcpy(g_init_data.config_data.wifi_ssid, EXAMPLE_ESP_WIFI_SSID);
    }
    if( memchr(g_init_data.config_data.wifi_key, 0, sizeof(g_init_data.config_data.wifi_key)) == NULL ) {
        fix = true;
        strcpy(g_init_data.config_data.wifi_key, EXAMPLE_ESP_WIFI_PASS);
    }
    if( memchr(g_init_data.config_data.wifi_ap_ssid, 0, sizeof(g_init_data.config_data.wifi_ap_ssid)) == NULL ) {
        printf("=-> reset wifi_ap_ssid\n");
        fix = true;
        strcpy(g_init_data.config_data.wifi_ap_ssid, EXAMPLE_ESP_WIFI_AP_SSID);
    }
    if( memchr(g_init_data.config_data.wifi_ap_key, 0, sizeof(g_init_data.config_data.wifi_ap_key)) == NULL ) {
        fix = true;
        strcpy(g_init_data.config_data.wifi_ap_key, EXAMPLE_ESP_WIFI_AP_PASS);
    }
    if(g_init_data.config_data.ir_voltage <= 0 || g_init_data.config_data.ir_voltage > 3000) {
        fix = true;
        g_init_data.config_data.ir_voltage = IR_VOL_UNSET;
    }

    printf("file:%s, line:%d, DevId = %s, g_init_data.service_ip_str = %s, g_init_data.service_ip_str[0] = %d\r\n", 
        __FILE__, __LINE__, g_init_data.config_data.device_id, g_init_data.config_data.service_ip_str, g_init_data.config_data.service_ip_str[0]);

    if(fix) {
        /* write default settings into nvs */
        err = nvs_set_blob(my_handle, "device_info", &(g_init_data.config_data), sizeof(config_para));
        if (err == ESP_OK)
        {
            // Commit
            err = nvs_commit(my_handle);
        }
    }
    nvs_close(my_handle);
    return ;
}

static void echo_task(void *arg)
{
    TickType_t fallingTickCount = 0;
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(ECHO_UART_NUM, &uart_config);
    uart_set_pin(ECHO_UART_NUM, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS);
    uart_driver_install(ECHO_UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);

    // Configure a temporary buffer for the incoming data
    char *data = (char *) malloc(BUF_SIZE);

    uart_write_bytes(ECHO_UART_NUM, GET_STATUS, strlen(GET_STATUS)+1);

    while (1)
    {
        // Read data from the UART
        int len = uart_read_bytes(ECHO_UART_NUM, (uint8_t *)data, BUF_SIZE, 20 / portTICK_RATE_MS);

        /* 观察是否超时(无人) */
        if( g_camera_over!=true && fallingTickCount && ( (xTaskGetTickCount() - fallingTickCount) > (3*configTICK_RATE_HZ))) {
            printf("=> falling edge time out\n");
            g_camera_over = true;
        }
        
        //printf("%d\n", xTaskGetTickCount());
        if((len <= 0) || (data[0] != '~')) {
            continue;
        }
        //printf("len = %d\n", len);
        data[len] = '\0';
        if( strstr(data, CORE_SHUT_DOWN) ) {
            printf("=> core shut down recvd, call esp_deep_sleep_start()\n");
            /* 进入深度休眠 */
            uart_write_bytes(ECHO_UART_NUM, CORE_SHUT_DOWN_OK, strlen(CORE_SHUT_DOWN_OK)+1);
#if  DBG_NO_SLEEP_MODE
#else
            upgrade_block();
            esp_deep_sleep_start();
#endif
        }
        else if( strstr(data, IR_WKUP_PIN_FALLING) ) {
            /* 上次是下降沿的话不用更新 */
            if( 0 == fallingTickCount) {
                fallingTickCount = xTaskGetTickCount();
            }
            printf("=> falling edge\n");
        }
        else if( strstr(data, IR_WKUP_PIN_RISING) ) {
            fallingTickCount = 0;
            printf("=> rising edge\n");
        }
        else if( strstr(data, KEY_WKUP_PIN_RISING) ) {
            printf("=> user key\n");
            max_sleep_uptime = 60*2;
        }
        else if(strstr(data, REC_STATUS)) {
            /* key/ir */
            if( 'k' == data[strlen(REC_STATUS)] ) {
                /* key */
                printf("STATUS: key\n");
                max_sleep_uptime = 60*2;
            } else {
                printf("STATUS: ir\n");
            }

            /* high,low */
            if( 'h' == strchr(data, ',')[1] ) {
                printf("STATUS: high\n");
            } else {
                printf("STATUS: low\n");
                /* 上次是下降沿的话不用更新 */
                if( 0 == fallingTickCount) {
                    fallingTickCount = xTaskGetTickCount();
                }
                printf("=> ir low level(act as falling edge)\n");
            }
        }
    }

    vTaskDelete(NULL);
}
/* add by liuwenjian 2020-3-4 end */

int led_gpio_init(void) {
    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = (1ULL<<15);
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);
    gpio_set_level(15, 0);
    return 0;
}

void app_main()
{
    int count = 0;
//    char *buff;
//    size_t total = 0, used = 0;

#if  0
    /* add by liuwenjian 2020-3-4 begin */
    /* 深度睡眠唤醒 */
    switch (esp_sleep_get_wakeup_cause())
    {
        case ESP_SLEEP_WAKEUP_EXT1:
        {
            uint64_t wakeup_pin_mask = esp_sleep_get_ext1_wakeup_status();
            if (wakeup_pin_mask != 0) 
            {
                int pin = __builtin_ffsll(wakeup_pin_mask) - 1;
                printf("file:%s, line:%d, Wake up from GPIO %d\n", __FILE__, __LINE__, pin);
            }
            else
            {
                printf("file:%s, line:%d, Wake up from GPIO\n", __FILE__, __LINE__);
            }
            
            break;
        }

/*        case ESP_SLEEP_WAKEUP_TIMER: 
        {
            printf("file:%s, line:%d, Wake up from timer. Time spent in deep sleep\n", __FILE__, __LINE__);
            break;
        }*/
        
        default:
            printf("file:%s, line:%d, Not a deep sleep reset\n", __FILE__, __LINE__);
    }
    /* add by liuwenjian 2020-3-4 end */
#endif
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    /* 设备信息初始化 */
    init_para(false);

    /* 提前拍摄 */
    app_camera_main();


/*    buff = (char *)heap_caps_malloc(3 * 1024 * 1024, MALLOC_CAP_SPIRAM);
    if (NULL == buff)
    {
        printf("file:%s, line:%d, heap_caps_malloc failed!\r\n", __FILE__, __LINE__);
    }
    else
    {
        free(buff);
        printf("file:%s, line:%d, heap_caps_malloc suc!\r\n", __FILE__, __LINE__);
    }*/
    
    app_wifi_main();

/*    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }*/
    
    app_httpd_main();

    /* add by liuwenjian 2020-3-4 begin */
    /* 创建任务接收系统消息 */
    xTaskCreate(echo_task, "uart_echo_task", 1024*2, NULL, 10, NULL);

    /* 等待摄像图片传送结束 */
    while (count < max_sleep_uptime)
    {
        count++;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    /* exceed max uptime, timeout */

    /* 等待升级结束 */
    while (TRUE == g_update_flag)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    
    /*
    const int wakeup_time_sec = 200;
    printf("Enabling timer wakeup, %ds\n", wakeup_time_sec);
    esp_sleep_enable_timer_wakeup(wakeup_time_sec * 1000000);
    */

    /* 设置pin 脚作为唤醒条件 */
    const int ext_wakeup_pin_1 = 33;
    const uint64_t ext_wakeup_pin_1_mask = 1ULL << ext_wakeup_pin_1;

    printf("Enabling EXT1 wakeup on pins GPIO%d\n", ext_wakeup_pin_1);
    esp_sleep_enable_ext1_wakeup(ext_wakeup_pin_1_mask, ESP_EXT1_WAKEUP_ANY_HIGH);

    // Isolate GPIO12 pin from external circuits. This is needed for modules
    // which have an external pull-up resistor on GPIO12 (such as ESP32-WROVER)
    // to minimize current consumption.
//    rtc_gpio_isolate(GPIO_NUM_12);

    printf("file:%s, line:%d, begin esp_deep_sleep_start\n", 
        __FILE__, __LINE__);
    /* 进入深度休眠 */
    uart_write_bytes(ECHO_UART_NUM, CORE_SHUT_DOWN_OK, strlen(CORE_SHUT_DOWN_OK)+1);
#if  DBG_NO_SLEEP_MODE
    nop();
#endif
    upgrade_block();
    esp_deep_sleep_start();
    /* add by liuwenjian 2020-3-4 end */

}

