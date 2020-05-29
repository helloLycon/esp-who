
#ifndef __TCP_BSP_H__
#define __TCP_BSP_H__



#ifdef __cplusplus
extern "C" {
#endif


#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE (!FALSE)
#endif

//#define TCP_SERVER_CLIENT_OPTION FALSE              //esp32作为client
#define TCP_SERVER_CLIENT_OPTION TRUE              //esp32作为server

//#define TAG                     "HX-TCP"            //打印的tag

//server
//AP热点模式的配置信息
#define SOFT_AP_SSID            "HX-TCP-SERVER"     //账号
#define SOFT_AP_PAS             ""          //密码，可以为空
#define SOFT_AP_MAX_CONNECT     1                   //最多的连接点

//client
//STA模式配置信息,即要连上的路由器的账号密码
#define GATEWAY_SSID            "Massky_AP"         //账号
#define GATEWAY_PAS             "ztl62066206"       //密码
#define PACKET_HEAD             0x55aa
#define PACKET_VERSION          0x10
#define SEND_FACE_PIC_CODE      0x22
#define SEND_HEARTBEAT_CODE     0x89
#define PACKET_END              0x0d

// FreeRTOS event group to signal when we are connected to wifi
#define WIFI_CONNECTED_BIT BIT0

extern int  g_total_data;
extern bool g_rxtx_need_restart;

/* 发送心跳报文数据 */
typedef struct send_heartbeat_info{
    unsigned short battery;     /* 剩余电量 */
    time_t cur_time;            /* 当前时间 */
}send_heartbeat_info;

/* 发送jpeg 具体数据 */
typedef struct send_jpeg_info{
    unsigned short num;         /* 图片编号 */
    unsigned long send_count;   /* 图片偏移量 */
    time_t create_time;         /* 图片创建时间 */
    unsigned char *buf;         /* 图片内容 */
}send_jpeg_info;

/* 报文发送信息 */
typedef struct packet_info{
    unsigned char status;       /* 报文发送状态 */
    unsigned short type;        /* 发送报文类型 */
    unsigned short send_len;    /* 发送数据大小 */
    void *data;                 /* 具体数据 */
}packet_info;

//using esp as station
void wifi_init_sta();
//using esp as softap
void wifi_init_softap();

void esp_wait_sntp_sync(void);

//create a tcp server socket. return ESP_OK:success ESP_FAIL:error
esp_err_t create_tcp_server(bool isCreatServer);
//create a tcp client socket. return ESP_OK:success ESP_FAIL:error
esp_err_t create_tcp_client();

// //send data task
// void send_data(void *pvParameters);
//receive data task
int recv_data();

int send_data(packet_info packet_data, bool);

void get_socket_status(int *socket_fd);

//close all socket
void close_socket();

//get socket error code. return: error code
int get_socket_error_code(int socket);

//show socket error code. return: error code
int show_socket_error_reason(const char* str, int socket);

//check working socket
int check_working_socket();


#ifdef __cplusplus
}
#endif


#endif /*#ifndef __TCP_BSP_H__*/

