#ifndef __COMMON_H__
#define __COMMON_H__

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

/*���İ�������*/
#define CORE_SHUT_DOWN_REQ   "~shutdown_coreboard_request"  //���İ巢���ػ�����
#define CORE_SHUT_DOWN       "~shutdown_coreboard_cmd"      //����mcu�����ػ�����
#define CORE_SHUT_DOWN_OK    "~coreboard_shutdown_ok"       //���İ�ػ����
#define IR_WKUP_PIN_RISING   "~ir_wkup_pin_rising"              //���ѽ������أ������ƶ�
#define IR_WKUP_PIN_FALLING  "~ir_wkup_pin_falling"            //���ѽ��½��أ�û���ƶ�
#define WAKE_UP_FROM_KEY     "~wkup_from_key"            //��������


#define TCP_PORT            10086               //ͳһ�Ķ˿ںţ�����TCP�ͻ��˻��߷����
#define CAMERA_VIDEO_TIME   12

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
}config_para;

/* ��ʼ������ */
typedef struct init_info{
    time_t start_time;              /* ����ʱ�� */
    config_para config_data;        /* ���ò��� */
}init_info;

extern unsigned char g_camera_over;
extern unsigned char g_pic_send_over;
extern unsigned char g_update_flag;
extern init_info g_init_data;
extern bool cameraEndFlag;

#endif

