#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "driver/uart.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "gatts_table_creat_demo.h"
#include "esp_gatt_common_api.h"
#include "gatts_config_param.h"
#include "sd_card_example_main.h"
#include "app_wifi.h"
#include "common.h"

/* Service */
const uint16_t GATTS_SERVICE_UUID_TEST      = 0x00FF;
const uint16_t GATTS_CHAR_UUID_DEV_ID     = 0xff01  ;
const uint16_t GATTS_CHAR_UUID_SERVER_IP  = 0xff02     ;
const uint16_t GATTS_CHAR_UUID_SERVER_PORT    = 0xff03   ;
const uint16_t GATTS_CHAR_UUID_WIFI_SSID    = 0xff04   ;
const uint16_t GATTS_CHAR_UUID_WIFI_KEY    = 0xff05   ;
const uint16_t GATTS_CHAR_UUID_IR_VOLTAGE    = 0xff06   ;

static const char *TAG = "GATTS_CONFIG_PARAM";

esp_gatt_status_t gatts_config_param_read_handler(esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    esp_gatt_rsp_t rsp;
    memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
    rsp.attr_value.handle = param->read.handle;

    xSemaphoreTake(g_data_mutex, portMAX_DELAY);
    config_para cfg = g_init_data.config_data;
    xSemaphoreGive(g_data_mutex);
    switch(param->read.handle - service_handle) {
        case IDX_CHAR_DEV_ID_VAL:
            strcpy((char *)rsp.attr_value.value, cfg.device_id);
            rsp.attr_value.len = strlen(cfg.device_id);
            break;
        case IDX_CHAR_SERVER_IP_VAL:
            strcpy((char *)rsp.attr_value.value, cfg.service_ip_str);
            rsp.attr_value.len = strlen(cfg.service_ip_str);
            break;
        case IDX_CHAR_SERVER_PORT_VAL:
            sprintf((char *)rsp.attr_value.value, "%d", cfg.service_port);
            rsp.attr_value.len = strlen((char *)rsp.attr_value.value);
            break;
        case IDX_CHAR_WIFI_SSID_VAL:
            strcpy((char *)rsp.attr_value.value, cfg.wifi_ssid);
            rsp.attr_value.len = strlen(cfg.wifi_ssid);
            break;
        case IDX_CHAR_WIFI_KEY_VAL:
            strcpy((char *)rsp.attr_value.value, cfg.wifi_key);
            rsp.attr_value.len = strlen(cfg.wifi_key);
            break;
        case IDX_CHAR_IR_VOLTAGE_VAL:
            sprintf((char *)rsp.attr_value.value, "%d", cfg.ir_voltage);
            rsp.attr_value.len = strlen((char *)rsp.attr_value.value);
            break;
        default:
            ESP_LOGE(TAG, "unknown char uuid");
    }
    esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                ESP_GATT_OK, &rsp);
    return ESP_GATT_OK;
}

esp_gatt_status_t gatts_config_param_write_handler(esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
    esp_gatt_status_t status = ESP_GATT_OK;
    char cfg[128];
    memcpy(cfg, param->write.value, param->write.len);
    cfg[param->write.len] = '\0';

    if((param->read.handle - service_handle)!=IDX_CHAR_WIFI_KEY_VAL && '\0' == cfg[0]) {
        status = ESP_GATT_INTERNAL_ERROR;
        goto end;
    }
    xSemaphoreTake(g_data_mutex, portMAX_DELAY);
    switch(param->read.handle - service_handle) {
        case IDX_CHAR_DEV_ID_VAL:
            strcpy(g_init_data.config_data.device_id, cfg);
            log_printf("蓝牙配置: dev_id = %s", cfg);
            break;
        case IDX_CHAR_SERVER_IP_VAL:
            strcpy(g_init_data.config_data.service_ip_str, cfg);
            log_printf("蓝牙配置: server_ip = %s", cfg);
            break;
        case IDX_CHAR_SERVER_PORT_VAL:
            if(atoi(cfg)==0 || atoi(cfg)>65535) {
                status = ESP_GATT_INTERNAL_ERROR;
                break;
            }
            //strcpy(g_init_data.config_data.service_ip_str, cfg);
            g_init_data.config_data.service_port = (uint16_t)atoi(cfg);
            log_printf("蓝牙配置: server_port = %s", cfg);
            break;
        case IDX_CHAR_WIFI_SSID_VAL:
            strcpy(g_init_data.config_data.wifi_ssid, cfg);
            log_printf("蓝牙配置: wifi ssid = %s", cfg);
            break;
        case IDX_CHAR_WIFI_KEY_VAL:
            if(cfg[0] && strlen(cfg)<8) {
                status = ESP_GATT_INTERNAL_ERROR;
                break;
            }
            /* 尝试连接 */
            const char *arg[2] = {g_init_data.config_data.wifi_ssid, cfg};
            is_connect = FALSE;
            s_retry_num = 0;
            app_wifi_main(arg);

            /* 验证wifi连接 */
            int cnter = 0;
            for(; cnter<5; cnter++) {
                if(is_connect == TRUE) {
                    break;
                } else if(s_retry_num > 0) {
                    /* 出现失败 */
                    break;
                } else {
                    vTaskDelay(1000/ portTICK_PERIOD_MS);
                }
            }
            if(TRUE == is_connect && 0 == s_retry_num) {
                strcpy(g_init_data.config_data.wifi_key, cfg);
                printf("correct wifi key~~\n");
                log_printf("蓝牙配置: wifi key = %s", cfg);
            } else {
                status = ESP_GATT_INTERNAL_ERROR;
            }
            is_connect = FALSE;
            s_retry_num = 0;
            printf("---------- WIFI TEST END ----------\n");
            esp_err_t err = esp_wifi_stop();
            if(err != ESP_OK) {
                ESP_LOGE(TAG, "esp_wifi_stop failed");
            }
            err = esp_wifi_deinit();
            if(err != ESP_OK) {
                ESP_LOGE(TAG, "esp_wifi_deinit failed");
            }
            break;
        case IDX_CHAR_IR_VOLTAGE_VAL:
            if(atoi(cfg)>4000) {
                status = ESP_GATT_INTERNAL_ERROR;
                break;
            }
            g_init_data.config_data.ir_voltage = atoi(cfg);
            log_printf("蓝牙配置: ir voltage = %s", cfg);

            char send_str[32];
            sprintf(send_str, "%s%d", SET_IR_VOLTAGE, atoi(cfg));
            printf("=-> send: %s\n", send_str);
            uart_write_bytes(ECHO_UART_NUM, send_str, strlen(send_str)+1);
            break;
        default:
            status = ESP_GATT_INTERNAL_ERROR;
            ESP_LOGE(TAG, "unknown char uuid");
    }
    if(status == ESP_GATT_OK) {
        store_init_data();
    }
    xSemaphoreGive(g_data_mutex);

end:
    esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, NULL);
    return status;
}

