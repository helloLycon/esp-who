#ifndef __GATTS_CONFIG_PARAM_H
#define __GATTS_CONFIG_PARAM_H




/* Attributes State Machine */
enum
{
    IDX_SVC = 0,
    IDX_CHAR_DEV_ID,
    IDX_CHAR_DEV_ID_VAL,
    //IDX_CHAR_CFG_A,

    IDX_CHAR_SERVER_IP,
    IDX_CHAR_SERVER_IP_VAL,

    IDX_CHAR_SERVER_PORT,
    IDX_CHAR_SERVER_PORT_VAL,
    IDX_CHAR_WIFI_SSID,
    IDX_CHAR_WIFI_SSID_VAL,
    IDX_CHAR_WIFI_KEY,
    IDX_CHAR_WIFI_KEY_VAL,
    IDX_CHAR_IR_VOLTAGE,
    IDX_CHAR_IR_VOLTAGE_VAL,

    HRS_IDX_NB,
};

/* read/write */
#define GATTS_CHAR_DECLARE_RW(index, char_uuid) \
[index]	 = \
{{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, \
  CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read_write}}, \
[index+1] = \
{{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&char_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, \
  GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(char_value), (uint8_t *)char_value}}

/* read-only */
#define GATTS_CHAR_DECLARE_RO(index, char_uuid) \
[index]	 = \
{{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *)&character_declaration_uuid, ESP_GATT_PERM_READ, \
  CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *)&char_prop_read}}, \
[index+1] = \
{{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *)&char_uuid, ESP_GATT_PERM_READ, \
  GATTS_DEMO_CHAR_VAL_LEN_MAX, sizeof(char_value), (uint8_t *)char_value}}

extern const uint16_t GATTS_SERVICE_UUID_TEST      ;
extern const uint16_t GATTS_CHAR_UUID_DEV_ID       ;
extern const uint16_t GATTS_CHAR_UUID_SERVER_IP       ;
extern const uint16_t GATTS_CHAR_UUID_SERVER_PORT       ;
extern const uint16_t GATTS_CHAR_UUID_WIFI_SSID       ;
extern const uint16_t GATTS_CHAR_UUID_WIFI_KEY       ;
extern const uint16_t GATTS_CHAR_UUID_IR_VOLTAGE       ;
extern uint16_t service_handle;

esp_gatt_status_t gatts_config_param_read_handler(esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
esp_gatt_status_t gatts_config_param_write_handler(esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);


#endif
