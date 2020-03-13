#ifndef __COMMON_H__
#define __COMMON_H__

#define DEVICE_ID_FLAG      "device_id"
#define SERVICE_IP_FLAG     "service_ip"
#define SERVICE_PORT_FLAG   "service_port"
#define TCP_SERVER_ADRESS   "10.10.1.238"     //作为client，要连接TCP服务器地址
#define DEVICE_INFO         "123456"
#define CAMERA_OVER         "airbat\tover\r\n"
#define CAMERA_STOP         "airbat\tpass\r\n"
#define STORAGE_NAMESPACE   "storage"

#define ECHO_UART_NUM   (UART_NUM_0)


#define TCP_PORT            10086               //统一的端口号，包括TCP客户端或者服务端
#define CAMERA_VIDEO_TIME   12

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

/* 配置参数 */
typedef struct config_para{
    unsigned short service_port;    /* 服务器端口号 */
    char service_ip_str[16];        /* 服务器ip 地址 */
    char device_id[64];             /* 设备编号 */
}config_para;

/* 初始化参数 */
typedef struct init_info{
    time_t start_time;              /* 启动时间 */
    config_para config_data;        /* 配置参数 */
}init_info;

extern unsigned char g_camera_over;
extern unsigned char g_pic_send_over;
extern unsigned char g_update_flag;
extern init_info g_init_data;

#endif

