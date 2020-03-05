
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

//#define TCP_SERVER_CLIENT_OPTION FALSE              //esp32��Ϊclient
#define TCP_SERVER_CLIENT_OPTION TRUE              //esp32��Ϊserver

//#define TAG                     "HX-TCP"            //��ӡ��tag

//server
//AP�ȵ�ģʽ��������Ϣ
#define SOFT_AP_SSID            "HX-TCP-SERVER"     //�˺�
#define SOFT_AP_PAS             ""          //���룬����Ϊ��
#define SOFT_AP_MAX_CONNECT     1                   //�������ӵ�

//client
//STAģʽ������Ϣ,��Ҫ���ϵ�·�������˺�����
#define GATEWAY_SSID            "Massky_AP"         //�˺�
#define GATEWAY_PAS             "ztl62066206"       //����
#define PACKET_HEAD             0x55aa
#define PACKET_VERSION          0x10
#define SEND_FACE_PIC_CODE      0x22
#define SEND_HEARTBEAT_CODE     0x89
#define PACKET_END              0x0d

// FreeRTOS event group to signal when we are connected to wifi
#define WIFI_CONNECTED_BIT BIT0

extern int  g_total_data;
extern bool g_rxtx_need_restart;

/* ���������������� */
typedef struct send_heartbeat_info{
    unsigned short battery;     /* ʣ����� */
    time_t cur_time;            /* ��ǰʱ�� */
}send_heartbeat_info;

/* ����jpeg �������� */
typedef struct send_jpeg_info{
    unsigned short num;         /* ͼƬ��� */
    unsigned long send_count;   /* ͼƬƫ���� */
    time_t create_time;         /* ͼƬ����ʱ�� */
    unsigned char *buf;         /* ͼƬ���� */
}send_jpeg_info;

/* ���ķ�����Ϣ */
typedef struct packet_info{
    unsigned char status;       /* ���ķ���״̬ */
    unsigned short type;        /* ���ͱ������� */
    unsigned short send_len;    /* �������ݴ�С */
    void *data;                 /* �������� */
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

int send_data(packet_info packet_data);

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

