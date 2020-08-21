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
#include "esp_wifi.h"
#include "esp_camera.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "common.h"
#include "i2c_example_main.h"
#include "adc1_example_main.h"
#include "sd_card_example_main.h"
#include "esp_log.h"
#include "spiffs_example_main.h"
#include "softap_example_main_tcpserver.h"
#include "gatts_table_creat_demo.h"

#define ECHO_TEST_TXD   (GPIO_NUM_1)
#define ECHO_TEST_RXD   (GPIO_NUM_35)
#define ECHO_TEST_RTS   (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS   (UART_PIN_NO_CHANGE)
#define BUF_SIZE        (256)

/*-------------------------- 触发参数配置 --------------------------*/
#define MAX_CAMERA_VIDEO_TIME_SECS     10

/* 多次触发时上升沿时间间隔需要在这个时间内 */
#define MIN_INTERVAL_OF_MULTI_CAPTURE_TICKS  (3000/*ms*//portTICK_PERIOD_MS)

/* 第一个视频需要的第三个上升沿，要在t1时间内来到，否则丢弃视频 */
#define MAX_RISING_EDGE_TIME_OF_FIRST_VALID_VIDEO_TICKS  (3000/*ms*//portTICK_PERIOD_MS) 

/* 无人时低电平持续时间 */
#define MIN_NO_PEOPLE_LOW_LEVEL_TIME_SECS    3

/* 最长单个视频发送时间 */
#define MAX_SINGLE_VIDEO_SEND_TIME_SECS   60

/* 最长运行时间 */
#define MAX_RUN_TIME_SECS   (10*60) /*10min*/


static const char *TAG = "main";
static const char *SEMTAG = "semaphore";
bool g_update_mcu = false;

portMUX_TYPE g_pic_send_over_spinlock = portMUX_INITIALIZER_UNLOCKED;
unsigned char g_pic_send_over = FALSE;

xSemaphoreHandle g_update_over;

xSemaphoreHandle g_data_mutex;
init_info g_init_data;

portMUX_TYPE time_var_spinlock = portMUX_INITIALIZER_UNLOCKED;
int max_sleep_uptime = DEF_MAX_SLEEP_TIME;
bool ble_config_mode = false;
bool shut_down_status = false;

xSemaphoreHandle vpercent_ready;
bool rtc_set_magic_match, wake_up_flag;

portMUX_TYPE cam_ctrl_spinlock = portMUX_INITIALIZER_UNLOCKED;
struct cam_ctrl_block cam_ctrl = {
    .status = CAM_CAPTURE,
    .first_capture_determined = false,
};

int semaphoreInit(void) {
    g_update_over = xSemaphoreCreateCounting(100, 0);
    if(NULL == g_update_over) {
        ESP_LOGE(SEMTAG, "g_update_over");
        return ESP_FAIL;
    }

    vpercent_ready = xSemaphoreCreateCounting(1, 0);
    if(NULL == vpercent_ready) {
        ESP_LOGE(SEMTAG, "vpercent_ready");
        return ESP_FAIL;
    }

    g_data_mutex = xSemaphoreCreateMutex();
    if(NULL == g_data_mutex) {
        ESP_LOGE(SEMTAG, "g_data_mutex");
        return ESP_FAIL;
    }
    ESP_LOGI(SEMTAG, "%s OKAY", __func__);
    return ESP_OK;
}

void nop(void) {
    while(1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void upgrade_block(void) {
    if(is_connect) {
        xSemaphoreTake(g_update_over, portMAX_DELAY);
    }
}

esp_err_t store_init_data(void)
{
    nvs_handle my_handle;
    esp_err_t err;

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "file:%s, line:%d, nvs_open err = %d\r\n", __FILE__, __LINE__, err);
        return err;
    }
    err = nvs_set_blob(my_handle, "device_info", &(g_init_data.config_data), sizeof(config_para));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "file:%s, line:%d, nvs_set_blob err = %d\r\n", __FILE__, __LINE__, err);
        nvs_close(my_handle);
        return err;
    }

    // Commit
    err = nvs_commit(my_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "file:%s, line:%d, nvs_commit err = %d\r\n", __FILE__, __LINE__, err);
        nvs_close(my_handle);
        return err;
    }

    printf("=-> g_init_data write into flash OKAY\n");
    // Close
    nvs_close(my_handle);
    return ESP_OK;
}


/* 如果config_para结构发生改变，先 "make erase" */
void init_para(bool erase_all)
{
    bool fix = false;
    nvs_handle my_handle;
    esp_err_t err;
    
    memset(&g_init_data, 0, sizeof(init_info));

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
        g_init_data.config_data.rtc_set = 0;
        g_init_data.config_data.last_btry_percent = LAST_BTRY_PERCENT_UNSET;
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
    if(g_init_data.config_data.ir_voltage <= 0 || g_init_data.config_data.ir_voltage > 10000) {
        fix = true;
        g_init_data.config_data.ir_voltage = IR_VOL_UNSET;
    }
    if(g_init_data.config_data.rtc_set != RTC_SET_MAGIC) {
        rtc_set_magic_match = false;
    } else {
        rtc_set_magic_match = true;
    }
    if(g_init_data.config_data.last_btry_percent>100) {
        g_init_data.config_data.last_btry_percent = LAST_BTRY_PERCENT_UNSET;
    }

#if  0
    /* 电量百分比 */
    adc_app_main_init();
    vPercent = adc_read_battery_percent();
    fix_battery_percent(&vPercent, &g_init_data.config_data.last_btry_percent);
    printf("vPercent = %d%%\n", vPercent);
    if(g_init_data.config_data.last_btry_percent != vPercent) {
        g_init_data.config_data.last_btry_percent = vPercent;
        fix = true;
    }
#endif

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

void cam_status_enter_idle(void) {
    printf("+++ (%s)\n", __func__);
    portENTER_CRITICAL(&cam_ctrl_spinlock);
    memset(&cam_ctrl, 0, sizeof(cam_ctrl));
    cam_ctrl.status = CAM_IDLE;
    cam_ctrl.first_capture_determined = true;
    portEXIT_CRITICAL(&cam_ctrl_spinlock);
}

int cam_status_handler(void) {
    portENTER_CRITICAL(&time_var_spinlock);
    bool is_ble_config = ble_config_mode;
    portEXIT_CRITICAL(&time_var_spinlock);
    if(is_ble_config) {
        return 0;
    }
    portENTER_CRITICAL(&cam_ctrl_spinlock);
    enum CamStatus status = cam_ctrl.status;
    portEXIT_CRITICAL(&cam_ctrl_spinlock);
    switch(status) {
#if  0
        case CAM_IDLE:
            if((cam_ctrl.last_rising_edge - cam_ctrl.prev_rising_edge)<MIN_INTERVAL_OF_MULTI_CAPTURE ) {
                camera_start_capture();
            }
            break;
#endif
        case CAM_CAPTURE:
            /* 没人了，持续低电平 */
            if(cam_ctrl.last_falling && cam_ctrl.last_rising_edge && (cam_ctrl.last_rising_edge < cam_ctrl.last_falling) &&( xTaskGetTickCount()-cam_ctrl.last_falling > sec2tick(MIN_NO_PEOPLE_LOW_LEVEL_TIME_SECS))) {
                log_printf("持续低电平-结束拍摄");
                printf("%d => 持续%d秒低电平，结束\n", xTaskGetTickCount(), MIN_NO_PEOPLE_LOW_LEVEL_TIME_SECS);

                portENTER_CRITICAL(&cam_ctrl_spinlock);
                bool b_start_ticks = !!cam_ctrl.start_ticks;
                portEXIT_CRITICAL(&cam_ctrl_spinlock);
                if(b_start_ticks) {
                    /* 已经开始拍摄 */
                    camera_finish_capture();
                }
                cam_status_enter_idle();
                break;
            }
            /* 最长拍摄时长 */
            portENTER_CRITICAL(&cam_ctrl_spinlock);
            bool b_start_ticks = !!cam_ctrl.start_ticks;
            portEXIT_CRITICAL(&cam_ctrl_spinlock);
            if(b_start_ticks &&(xTaskGetTickCount()-cam_ctrl.start_ticks > sec2tick(MAX_CAMERA_VIDEO_TIME_SECS))) {
                log_printf("最长拍摄结束");
                printf("结束：最长拍摄(%d s)\n", MAX_CAMERA_VIDEO_TIME_SECS);
                camera_finish_capture();
                cam_status_enter_idle();
                break;
            }
            /* 判断第一次拍摄一定时间内是否有上升沿到来 */
            if(cam_ctrl.first_capture_determined==false && xTaskGetTickCount()>MAX_RISING_EDGE_TIME_OF_FIRST_VALID_VIDEO_TICKS) {
                log_printf("丢弃首次拍摄");
                printf("+++ 丢弃首次拍摄\n");
                camera_drop_capture();
                cam_status_enter_idle();
                break;
            }
            break;
        default:
            break;
    }
    return 0;
}

int enter_ble_config_mode(void) {
    /* ota */
    vTaskDelete(simple_ota_example_task_handle);
    for(int i=0; i<20; i++) {
        xSemaphoreGive(g_update_over);
    }
    vTaskDelete(get_camera_data_task_handle);
    //vTaskDelete(save_video_into_sdcard_task_handle);
    vTaskDelete(send_queue_pic_task_handle);
    /* stop wifi */
    esp_err_t err = esp_wifi_stop();
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_stop failed");
    }
    err = esp_wifi_deinit();
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_deinit failed");
    }
    is_connect = false;
    /* camera */
    err = esp_camera_deinit();
    if(err != ESP_OK) {
        ESP_LOGE(TAG, "esp_camera_deinit failed");
    }
    /* start bt */
    gatts_init();
    log_enum(LOG_CONFIGURATION);
    
    portENTER_CRITICAL(&time_var_spinlock);
    ble_config_mode = true;
    max_sleep_uptime = BLE_CONFIG_MAX_SLEEP_TIME;
    portEXIT_CRITICAL(&time_var_spinlock);

    /* 避免死锁 */
    unlock_vq();
    unlock_vq();
    unlock_vq();
    return 0;
}

int cam_edge_handler(bool rising) {
    portENTER_CRITICAL(&time_var_spinlock);
    bool is_ble_config = ble_config_mode;
    portEXIT_CRITICAL(&time_var_spinlock);
    if(is_ble_config) {
        return 0;
    }
    if(rising) {
        cam_ctrl.prev_rising_edge = cam_ctrl.last_rising_edge;
        cam_ctrl.last_rising_edge = xTaskGetTickCount();

        portENTER_CRITICAL(&cam_ctrl_spinlock);
        enum CamStatus status = cam_ctrl.status;
        portEXIT_CRITICAL(&cam_ctrl_spinlock);

        if(status == CAM_REACH_MAX_TRIGGER_TIMES) {
            /* 已经达到最大触发次数 */
            return 0;
        } else if(cam_ctrl.first_capture_determined == false && CAM_CAPTURE == status && xTaskGetTickCount()<MAX_RISING_EDGE_TIME_OF_FIRST_VALID_VIDEO_TICKS) {
            /* valid video, do nothing */
            cam_ctrl.first_capture_determined = true;
        } else if(CAM_IDLE == status) {
            if(!cam_ctrl.prev_rising_edge) {
                return 0;
            }
            if((cam_ctrl.last_rising_edge - cam_ctrl.prev_rising_edge)<MIN_INTERVAL_OF_MULTI_CAPTURE_TICKS ) {
                camera_start_capture();
                portENTER_CRITICAL(&cam_ctrl_spinlock);
                cam_ctrl.status = CAM_CAPTURE;
                portEXIT_CRITICAL(&cam_ctrl_spinlock);
            }
        }
    } else {
        cam_ctrl.last_falling = xTaskGetTickCount();
    }
    return 0;
}

int cmd_cmp(const char *src, const char *cmd, int *plen) {
    int str_len = strlen(cmd);
    int ret = strncmp(src, cmd, str_len);
    *plen = (ret?0:str_len);
    return ret;
}

static void echo_task(void *arg)
{
    extern unsigned char is_connect;
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
    char *uartbuf = (char *) malloc(BUF_SIZE);

    uart_write_bytes(ECHO_UART_NUM, GET_STATUS, strlen(GET_STATUS)+1);

    int tot_len = 0;
    while (false == g_update_mcu)
    {
        char tmp[BUF_SIZE];
        // Read data from the UART
        int len = uart_read_bytes(ECHO_UART_NUM, (uint8_t *)uartbuf+tot_len, BUF_SIZE, 20 / portTICK_RATE_MS);

        cam_status_handler();

        //printf("%d\n", xTaskGetTickCount());
        if(len <= 0) {
            continue;
        }

        /* 收到数据 */
        tot_len += len;
        uartbuf[tot_len] = '\0';
        while(true) {
            char *command = strchr(uartbuf, '~');
            if(NULL == command) {
                break;
            }
            /* 存在hdr */
            if(command != uartbuf) {
                strcpy(tmp, command);
                strcpy(uartbuf, tmp);
                tot_len = strlen(uartbuf);
                command = uartbuf;
            }

            int cmd_len = 0;
            if( !cmd_cmp(command, CORE_SHUT_DOWN, &cmd_len) ) {
                sdcard_log_write();
                printf("=> core shut down recvd, call esp_deep_sleep_start()\n");
                uart_write_bytes(ECHO_UART_NUM, CORE_SHUT_DOWN_OK, strlen(CORE_SHUT_DOWN_OK)+1);
                /* 进入深度休眠 */
                esp_deep_sleep_start();
            }
            else if( !cmd_cmp(command, IR_WKUP_PIN_FALLING, &cmd_len) ) {
                /*
                // 上次是下降沿的话不用更新
                if( 0 == fallingTickCount) {
                    fallingTickCount = xTaskGetTickCount();
                }*/
                printf("[%d] ↓\n", xTaskGetTickCount());
                cam_edge_handler(0);
            }
            else if( !cmd_cmp(command, IR_WKUP_PIN_RISING, &cmd_len) ) {
                //fallingTickCount = 0;
                printf("[%d] ↑\n", xTaskGetTickCount());
                cam_edge_handler(1);
            }
#if  1    /* 模拟多次触发 */
            else if( !cmd_cmp(command, KEY_WKUP_PIN_RISING, &cmd_len) ) {
                cam_edge_handler(1);
                cam_edge_handler(1);
            }
#endif
            else if(!cmd_cmp(command, REC_STATUS, &cmd_len)) {
                /* key/ir */
                if( 'k' == command[strlen(REC_STATUS)] ) {
                    /* key */
                    printf("STATUS: key, enter BT-CONFIGURATION mode...\n");
                    enter_ble_config_mode();
                } else {
                    printf("STATUS: ir\n");
                }

                /* current high,low */
                if( 'h' == strchr(command, ',')[1] ) {
                    printf("STATUS: ir-high\n");
                } else {
                    printf("STATUS: ir-low\n");
                    /* 上次是下降沿的话不用更新 */
                    if( 0 == cam_ctrl.last_falling) {
                        cam_ctrl.last_falling = xTaskGetTickCount();
                    }
                    printf("=> ir low level(act as falling edge)\n");
                }
                /* initial high,low */
                if( 'h' == strchr(strchr(command, ',')+1, ',')[1] ) {
                    printf("STATUS: ir-init-high\n");
                } else {
                    printf("STATUS: ir-init-low\n");
#if  0
                    /* 上次是下降沿的话不用更新 */
                    if( 0 == fallingTickCount) {
                        fallingTickCount = xTaskGetTickCount();
                    }
                    printf("=> ir low level(act as falling edge)\n");
#endif
                }
                if('w' == strrchr(command, ',')[1]) {
                    printf("STATUS: wuf set\n");
                    wake_up_flag = true;
                } else {
                    printf("STATUS: wuf unset\n");
                    wake_up_flag = false;
                }

                /* adc(battery percent) routine */

                /* mutex protected */
                xSemaphoreTake(g_data_mutex, portMAX_DELAY);
                fix_battery_percent(&vPercent, &g_init_data.config_data.last_btry_percent);
                printf("=-> vPercent = %d%%\n", vPercent);
                if(g_init_data.config_data.last_btry_percent != vPercent) {
                    g_init_data.config_data.last_btry_percent = vPercent;
                    store_init_data();
                }
                xSemaphoreGive(g_data_mutex);
                if(0 == vPercent) {
                    /* 防止电池过放，低压关机 */
                    log_enum(LOG_LOW_BATTERY);
                    printf("=-> low battery, send shutdown request\n");
                    sdcard_log_write();
                    shut_down_status = true;
                    uart_write_bytes(ECHO_UART_NUM, CORE_SHUT_DOWN_REQ, strlen(CORE_SHUT_DOWN_REQ)+1);
                    break;
                }
                xSemaphoreGive(vpercent_ready);
            }
            else if(!cmd_cmp(command, CAMERA_POWER_DOWN_OK, &cmd_len)) {
                extern bool g_camera_power;
                g_camera_power = false;
                printf("camera power down OKAY\n");
            }

            /* 查看是否匹配命令 */
            if(cmd_len) {
                strcpy(tmp, command + cmd_len);
                strcpy(uartbuf, tmp);
                tot_len = strlen(uartbuf);
                continue;
            } else {
                char *search_next_til = strchr(command + 1, '~');
                if(search_next_til) {
                    strcpy(tmp, search_next_til);
                    strcpy(uartbuf, tmp);
                    tot_len = strlen(uartbuf);
                } else {
                    break;
                }
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

const char *mk_time_hex_id(const time_t t,char *str) {
    //struct tm tmv;
    //localtime_r(&t, &tmv);
    //sprintf(str, "f%d%d%d%d%02d%02d", tmv.tm_year+1900, tmv.tm_mon+1, tmv.tm_mday, tmv.tm_hour,tmv.tm_min,tmv.tm_sec);
    sprintf(str, "%08X", (unsigned int)t);
    return str;
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

    semaphoreInit();
    /* 设备信息初始化 */
    init_para(false);

    /* 日志记录模块初始化 */
    sdcard_log_init();

    i2c_app_init();
    rtc_read_time(false);

    /* adc采样 */
    adc_app_main_init();
    vPercent = adc_read_battery_percent();
    /* 检查是否需要时间同步 */
    if(rtc_sntp_needed()) {
        app_wifi_main(NULL);
        sntp_rtc_routine();
        app_camera_main();
    } else {
        app_camera_main();
        app_wifi_main(NULL);
    }

    //app_httpd_main();
    //xTaskCreate(tcp_server_task, "tcp_server", 3072, NULL, 5, NULL);

    /* add by liuwenjian 2020-3-4 begin */
    /* 创建任务接收系统消息 */
    sdcard_init();
    xTaskCreate(save_video_into_sdcard_task, "save_video", 2048, NULL, 10, &save_video_into_sdcard_task_handle);
    xTaskCreate(echo_task, "uart_echo_task", 3072, NULL, 20, NULL);

    /* 等待超时 */
    for (int no_video_cnter = 0;;)
    {
        portENTER_CRITICAL(&time_var_spinlock);
        bool timeout = count > max_sleep_uptime;
        bool is_ble_config = ble_config_mode;
        uint32_t send_v_start_time = send_video_start_time;
        portEXIT_CRITICAL(&time_var_spinlock);

        //printf("ble_config_mode = %d\n", is_ble_config);
        if(is_ble_config) {
            if(timeout) {
                /* 到达最长配置时间 */
                log_printf("蓝牙配置结束");
                break;
            } else {
                count++;
                vTaskDelay(1000 / portTICK_PERIOD_MS);
                continue;
            }
        }
        /* 发送超时 */
        if(send_v_start_time&&(xTaskGetTickCount() - send_v_start_time > sec2tick(MAX_SINGLE_VIDEO_SEND_TIME_SECS))) {
            ESP_LOGI(TAG, "发送超时(%d s)", MAX_SINGLE_VIDEO_SEND_TIME_SECS);
            log_printf("发送超时(%d s)", MAX_SINGLE_VIDEO_SEND_TIME_SECS);
            break;
        }

        /* 没有任务一段时间 */
        lock_vq();
        void *vq = vq_head;
        //dump_vq();
        unlock_vq();
        if(NULL == vq) {
            no_video_cnter++;
            const int timeout_in_sec = 3;
            if(no_video_cnter > timeout_in_sec) {
                ESP_LOGI(TAG, "空队列持续: %d秒", timeout_in_sec);
                log_printf("空队列持续: %d秒", timeout_in_sec);
                break;
            }
        } else {
            no_video_cnter = 0;
        }

        /* 单次最长运行时间 */
        if(xTaskGetTickCount() > sec2tick(MAX_RUN_TIME_SECS)) {
            log_printf("最长运行: %d秒", MAX_RUN_TIME_SECS);
            ESP_LOGI(TAG, "到达最长运行时间: %d秒", MAX_RUN_TIME_SECS);
            break;
        }
        /* 检查wifi连接 */
        if( xTaskGetTickCount() >= sec2tick(10) && false == is_connect_server) {
            log_printf("连不上wifi或服务器");
            ESP_LOGE(TAG, "=-> NO WIFI/SERVER CONNECTED, send shutdown request\n");
            break;
        }

        /* 没有要break/continue的 */
        if(shut_down_status) {
            printf("shut down status, send shut down req\n");
            uart_write_bytes(ECHO_UART_NUM, CORE_SHUT_DOWN_REQ, strlen(CORE_SHUT_DOWN_REQ)+1);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    /* exceed max uptime, timeout */

    //portENTER_CRITICAL(&time_var_spinlock);
    //int runtime = max_sleep_uptime;
    //portEXIT_CRITICAL(&time_var_spinlock);
    //ESP_LOGI(TAG, "run timed out(%d s)", runtime);
    //log_printf("运行超时 (%d 秒)", runtime);
    /*----------write log-----------*/
    sdcard_log_write();
    printf("=-> break loop, send shutdown request\n");
    for(;;) {
        uart_write_bytes(ECHO_UART_NUM, CORE_SHUT_DOWN_REQ, strlen(CORE_SHUT_DOWN_REQ)+1);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

#if  0
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
    esp_deep_sleep_start();
    /* add by liuwenjian 2020-3-4 end */
#endif
}

