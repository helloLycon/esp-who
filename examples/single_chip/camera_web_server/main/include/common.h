#ifndef __COMMON_H__
#define __COMMON_H__

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DEVICE_ID_FLAG      "device_id"
#define SERVICE_IP_FLAG     "service_ip"
#define SERVICE_PORT_FLAG   "service_port"
#define TCP_SERVER_ADRESS   "10.10.1.238"     //��Ϊclient��Ҫ����TCP��������ַ
#define DEVICE_INFO         "123456"
#define CAMERA_OVER         "airbat\tover\r\n"
#define CAMERA_STOP         "airbat\tpass\r\n"
#define STORAGE_NAMESPACE   "storage"

#define ECHO_UART_NUM   (UART_NUM_0)

#define DBG_NO_SLEEP_MODE  0
#define DEF_MAX_SLEEP_TIME  60

/*���İ�������*/
#define CORE_SHUT_DOWN_REQ   "~shutdown_coreboard_request"  //���İ巢���ػ�����
#define CORE_SHUT_DOWN       "~shutdown_coreboard_cmd"      //����mcu�����ػ�����
#define CORE_SHUT_DOWN_OK    "~coreboard_shutdown_ok"       //���İ�ػ����
#define IR_WKUP_PIN_RISING   "~ir_wkup_pin_rising"              //���ѽ������أ������ƶ�
#define IR_WKUP_PIN_FALLING  "~ir_wkup_pin_falling"            //���ѽ��½��أ�û���ƶ�
#define SET_IR_VOLTAGE       "~set_ir_voltage="             //���ú���ƿ��Ƶ�ѹ��ֵ
#define SET_IR_VOLTAGE_SUC   "~set_ir_voltage_suc"
#define SET_IR_VOLTAGE_FAIL  "~set_ir_voltage_fail"
#define GET_STATUS    "~get_status"                           //��ȡ״̬
#define REC_STATUS  "~rec_status="                          //����״̬~status=������Դ���ƶ����io״̬��~rec_status=key(ir),high(low)
#define KEY_WKUP_PIN_RISING  "~key_wkup_pin_rising"    //�������ѽ������أ��а����¼�
#define CAMERA_POWER_DOWN_REQ    "~camera_power_down_req"
#define CAMERA_POWER_DOWN_OK    "~camera_power_down_ok"


#define TCP_PORT            10086               //ͳһ�Ķ˿ںţ�����TCP�ͻ��˻��߷����
#define CAMERA_VIDEO_TIME   12

#define IR_VOL_UNSET      0

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

/* ���ò��� */
typedef struct config_para{
    unsigned short service_port;    /* �������˿ں� */
    char service_ip_str[16];        /* ������ip ��ַ */
    char device_id[64];             /* �豸��� */
    char wifi_ssid[64];
    char wifi_key[32];
    char wifi_ap_ssid[64];
    char wifi_ap_key[32];
    int  ir_voltage;
    /* sntp->rtc */
    time_t last_sntp;
    uint32_t rtc_set;
}config_para;

/* ��ʼ������ */
typedef struct init_info{
    time_t start_time;              /* ����ʱ�� */
    config_para config_data;        /* ���ò��� */
}init_info;

extern bool g_camera_over;
extern unsigned char g_pic_send_over;
extern unsigned char g_update_flag;
extern init_info g_init_data;

void upgrade_block(void) ;
int led_gpio_init(void);
esp_err_t store_init_data(void);


#endif

