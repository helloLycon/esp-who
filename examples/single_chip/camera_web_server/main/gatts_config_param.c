#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "driver/uart.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "gatts_table_creat_demo.h"
#include "esp_gatt_common_api.h"
#include "gatts_config_param.h"
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
            break;
        case IDX_CHAR_SERVER_IP_VAL:
            strcpy(g_init_data.config_data.service_ip_str, cfg);
            break;
        case IDX_CHAR_SERVER_PORT_VAL:
            if(atoi(cfg)==0 || atoi(cfg)>65535) {
                status = ESP_GATT_INTERNAL_ERROR;
                break;
            }
            //strcpy(g_init_data.config_data.service_ip_str, cfg);
            g_init_data.config_data.service_port = (uint16_t)atoi(cfg);
            break;
        case IDX_CHAR_WIFI_SSID_VAL:
            strcpy(g_init_data.config_data.wifi_ssid, cfg);
            break;
        case IDX_CHAR_WIFI_KEY_VAL:
            if(cfg[0] && strlen(cfg)<8) {
                status = ESP_GATT_INTERNAL_ERROR;
                break;
            }
            strcpy(g_init_data.config_data.wifi_key, cfg);
            break;
        case IDX_CHAR_IR_VOLTAGE_VAL:
            if(atoi(cfg)>4000) {
                status = ESP_GATT_INTERNAL_ERROR;
                break;
            }
            g_init_data.config_data.ir_voltage = atoi(cfg);

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

