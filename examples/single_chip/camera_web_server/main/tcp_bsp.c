/*
* @file         tcp_bsp.c 
* @brief        wifi�����¼������socket�շ����ݴ���
* @details      �ڹٷ�Դ��Ļ������ʵ��޸ĵ�����������ע��
* @author       hx-zsj 
* @par Copyright (c):  
*               �������߿����Ŷӣ�QQȺ��671139854
* @par History:          
*               Ver0.0.1:
                     hx-zsj, 2018/08/08, ��ʼ���汾\n 
*/

/* 
=============
ͷ�ļ�����
=============
*/
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/apps/sntp.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
//#include "esp_event_loop.h"
#include "esp_event.h"
#include "esp_log.h"
#include "tcp_bsp.h"
#include "common.h"
#include "camera_error.h"

/*
===========================
ȫ�ֱ�������
=========================== 
*/
//socket
static int server_socket = -1;                           //������socket
static struct sockaddr_in server_addr;                  //server��ַ
static struct sockaddr_in client_addr;                  //client��ַ
static unsigned int socklen = sizeof(client_addr);      //��ַ����
static int connect_socket = -1;                          //����socket
bool g_rxtx_need_restart = false;                       //�쳣���������ӱ��
static unsigned char *g_send_buf = NULL;
static unsigned char *g_recv_buf = NULL;

// int g_total_data = 0;



// #if EXAMPLE_ESP_TCP_PERF_TX && EXAMPLE_ESP_TCP_DELAY_INFO

// int g_total_pack = 0;
// int g_send_success = 0;
// int g_send_fail = 0;
// int g_delay_classify[5] = {0};

// #endif /*EXAMPLE_ESP_TCP_PERF_TX && EXAMPLE_ESP_TCP_DELAY_INFO*/


/*
===========================
��������
=========================== 
*/

static void esp_initialize_sntp(void)
{
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "ntp1.aliyun.com");
    sntp_init();
}

/*
* ʱ��ͬ��
* @param   void: ��
* @retval      void                :��
* @note        �޸���־ 
*               Ver0.0.1:
                    yf-lwj, 2020/01/13, ��ʼ���汾\n 
*/
void esp_wait_sntp_sync(void)
{
    char strftime_buf[64];
    time_t now = 0;
    struct tm timeinfo = {0};
    //     int retry = 0;

    esp_initialize_sntp();

    while (timeinfo.tm_year < (2020 - 1900))
    {
        vTaskDelay(100 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    setenv("TZ", "CTS-8", 1);
    tzset();

    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
}

/*
* ���socket ����״̬
* @param[out]   int *                 :��ȡsocket ��ֵ
* @retval      void                :��
* @note        �޸���־ 
*               Ver0.0.1:
                    yf-lwj, 2020/01/09, ��ʼ���汾\n 
*/
void get_socket_status(int *socket_fd)
{
    *socket_fd = connect_socket;
}

/*
* ������������
* @param[in]   void                 :��
* @retval      void                :��
* @note        �޸���־ 
*               Ver0.0.1:
                    hx-zsj, 2018/08/06, ��ʼ���汾\n 
*/
int recv_data()
{
    unsigned char sn_len;
    int recv_count = 0;
    int len = 0;            //����
//    int loop;

    if (NULL == g_recv_buf)
    {
        g_recv_buf = (unsigned char *)malloc(1024);
        if (NULL == g_recv_buf)
        {
            printf("file:%s, line:%d, malloc failed errno = %d, errno = %s\r\n", 
                __FILE__, __LINE__, errno, strerror(errno));
            return -1;
        }
    }
    
    //��ջ���
    memset(g_recv_buf, 0x00, 1024);
    //��ȡ��������
    len = recv(connect_socket, g_recv_buf, 1024, 0);
    g_rxtx_need_restart = false;
    if (len > 0)
    {
        recv_count = 0;
        sn_len = g_recv_buf[6];
        recv_count = 10 + sn_len;

/*        printf("file:%s, line:%d, sn_len = %d, recv_count = %d, len = %d, g_recv_buf[%d] = %02x\r\n", 
            __FILE__, __LINE__, sn_len, recv_count, len, recv_count, g_recv_buf[recv_count]);
        for (loop = 0; loop < len; loop++)
        {
            printf("%02x:", g_recv_buf[loop]);
        }
        printf("\n");*/
        
        if (0x00 != g_recv_buf[recv_count])
        {
            return 1;
        }
        //g_total_data += len;
        //��ӡ���յ�������
        //�������ݻط�
//        send(connect_socket, databuff, strlen(databuff), 0);
        //sendto(connect_socket, databuff , sizeof(databuff), 0, (struct sockaddr *) &remote_addr,sizeof(remote_addr));
    }
    else
    {
        //��ӡ������Ϣ
        show_socket_error_reason("recv_data", connect_socket);
        //���������ϣ��������
        g_rxtx_need_restart = true;
        
#if TCP_SERVER_CLIENT_OPTION
        //�����������쳣������break��close socket,��Ϊ������client
        //break;
        vTaskDelete(NULL);
#else
        //client
        break;
#endif
        return -1;
    }

/*
    close_socket();
    //�������
    g_rxtx_need_restart = true;
    vTaskDelete(NULL);
*/    
    return 0;
}


void package_message(packet_info packet_data, int *package_len, unsigned char *packet_buf)
{
    unsigned long net_send_count;
    unsigned short net_num;
    unsigned short packet_head;
    unsigned short net_send_len;
    unsigned short packet_len;
    unsigned long net_create_time;
    unsigned long net_class_num;
    unsigned long check_sum;
    unsigned char check_sum_ret;
//    unsigned char packet_buf[2048];
    unsigned char device_id_len;
    unsigned short net_battery;
    unsigned long net_heartbeat_time;
    send_jpeg_info *jpg_data;
    send_heartbeat_info *heartbeat_data;
    int loop;
//    int ret;

//    printf("file:%s, line:%d, in package_message\r\n", __FILE__, __LINE__);

    /* ����ͷ */
    packet_head = htons(PACKET_HEAD);
//    printf("file:%s, line:%d, packet_head = %x\r\n", __FILE__, __LINE__, packet_head);
    memcpy(packet_buf, &packet_head, 2);
    packet_len = 2;
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    /* �汾�� */
    packet_buf[packet_len] = PACKET_VERSION;
    packet_len++;
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    /* ״̬ */
    packet_buf[packet_len] = packet_data.status;
    packet_len++;

    /* �ź�ǿ�� */
    memset(&packet_buf[packet_len], 0x00, 2);
    packet_len += 2;
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    /* �豸��ų��� */
    device_id_len = (unsigned char)strlen(g_init_data.config_data.device_id);
    packet_buf[packet_len] = device_id_len;
    packet_len++;
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    /* �豸��� */
    if (device_id_len > 0)
    {
        memcpy(&packet_buf[packet_len], g_init_data.config_data.device_id, device_id_len);
        packet_len += device_id_len;
    }
    //Ԥ��
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    /* ������ */
    packet_buf[packet_len] = packet_data.type;
    packet_len++;
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    /* ���ݳ��� */
    net_send_len = htons(packet_data.send_len);
    memcpy(&packet_buf[packet_len], &net_send_len, 2);
    packet_len += 2;
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    if (SEND_FACE_PIC_CODE == packet_data.type)
    {
        /* ���� */
        jpg_data = (send_jpeg_info *)packet_data.data;
        /* ͼƬ��� */
        net_num = htons(jpg_data->num);
        memcpy(&packet_buf[packet_len], &net_num, 2);
        packet_len += 2;
//        printf("file:%s, line:%d, jpg_data->num = %d\r\n", __FILE__, __LINE__, jpg_data->num);

        /* ������ */
        net_class_num = htons(g_init_data.start_time);
        memcpy(&packet_buf[packet_len], &net_class_num, 4);
        packet_len += 4;
//        printf("file:%s, line:%d, g_init_data.start_time = %ld\r\n", __FILE__, __LINE__, g_init_data.start_time);

        /* ͼƬ����ʱ�� */
        net_create_time = htonl(jpg_data->create_time);
        memcpy(&packet_buf[packet_len], &net_create_time, 4);
        packet_len += 4;
//        printf("file:%s, line:%d, jpg_data->create_time = %ld\r\n", __FILE__, __LINE__, jpg_data->create_time);

        /* ƫ���� */
        net_send_count = htonl(jpg_data->send_count);
        memcpy(&packet_buf[packet_len], &net_send_count, 4);
        packet_len += 4;
//        printf("file:%s, line:%d, jpg_data->send_count = %lu\r\n", __FILE__, __LINE__, jpg_data->send_count);

        /* ���ݰ����� */
        if (packet_data.send_len > 14)
        {
            memcpy(&packet_buf[packet_len], jpg_data->buf, (packet_data.send_len - 14));
            packet_len += (packet_data.send_len - 14);
        }
    }
    else if (SEND_HEARTBEAT_CODE == packet_data.type)
    {
        heartbeat_data = (send_heartbeat_info *)packet_data.data;
        
        /* ��ȡ��ص��� */
        net_battery = htons(heartbeat_data->battery);
        memcpy(&packet_buf[packet_len], &net_battery, 2);
        packet_len += 2;

        /* ��ȡʱ�� */
        net_heartbeat_time = htonl(heartbeat_data->cur_time);
        memcpy(&packet_buf[packet_len], &net_heartbeat_time, 4);
        packet_len += 4;
    }
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    /* У��� */
    check_sum = 0;
    for (loop = 0; loop < packet_len; loop++)
    {
        check_sum += packet_buf[loop];
    }

    check_sum_ret = check_sum % 256;
    packet_buf[packet_len] = check_sum_ret;
    packet_len++;
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    /* ����β */
    packet_buf[packet_len] = PACKET_END;
    packet_len++;
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    *package_len = packet_len;

    return ;
}

/*
* ������������
* @param[in]   void                 :��
* @retval      void                :��
* @note        �޸���־ 
*               Ver0.0.1:
                    hx-zsj, 2020/01/07, ��ʼ���汾\n 
*/
int send_data(packet_info packet_data, bool recv_needed)
{
    int ret;
    int len = 0;            //����
    int send_len = 0;
    static unsigned char is_first = TRUE;

    if (NULL == g_send_buf)
    {
        g_send_buf = (unsigned char *)malloc(2048);
        if (NULL == g_send_buf)
        {
            printf("file:%s, line:%d, malloc failed errno = %d, errno = %s\r\n", 
                __FILE__, __LINE__, errno, strerror(errno));
            return CAMERA_ERROR_MALLOC_FAILED;
        }
    }
    
    //��ջ���
    memset(g_send_buf, 0x00, 2048);

//    printf("file:%s, line:%d, begin package_message\r\n", __FILE__, __LINE__);
    package_message(packet_data, &len, g_send_buf);
    //��ȡ��������
//    printf("file:%s, line:%d, connect_socket = %d, len = %d\r\n", 
//        __FILE__, __LINE__, connect_socket, len);
    send_len = send(connect_socket, g_send_buf, len, 0);
    g_rxtx_need_restart = false;
    if (send_len > 0)
    {
        //g_total_data += len;
        //��ӡ���յ�������
        //�������ݻط�
        //sendto(connect_socket, databuff , sizeof(databuff), 0, (struct sockaddr *) &remote_addr,sizeof(remote_addr));
//        printf("file:%s, line:%d, send len = %d\r\n", __FILE__, __LINE__, send_len);
    }
    else
    {
        //��ӡ������Ϣ
        show_socket_error_reason("recv_data", connect_socket);
        //���������ϣ��������
        g_rxtx_need_restart = true;
        
        printf("file:%s, line:%d, send failed errno = %d, errno = %s\r\n", 
            __FILE__, __LINE__, errno, strerror(errno));
        //�����������쳣������break��close socket,��Ϊ������client
        return CAMERA_ERROR_SEND_FAILED;
    }

    if( !recv_needed ) {
        goto skip_recv;
    }
    ret = recv_data();
    if ((0 != ret) && (1 != ret))
    {
        printf("file:%s, line:%d, recv_data failed\r\n", __FILE__, __LINE__);
        close_socket();
    }
skip_recv:
/*
    close_socket();
    //�������
    g_rxtx_need_restart = true;
    vTaskDelete(NULL);
*/
//    printf("file:%s, line:%d, send_len = %d\r\n", __FILE__, __LINE__, send_len);
    return CAMERA_OK;
}


/*
* ����tcp server
* @param[in]   isCreatServer          :�״�true���´�false
* @retval      void                 :��
* @note        �޸���־ 
*               Ver0.0.1:
                    hx-zsj, 2018/08/06, ��ʼ���汾\n 
*/
esp_err_t create_tcp_server(bool isCreatServer)
{
    //�״ν���server
    if (isCreatServer)
    {
        //�½�socket
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket < 0)
        {
            show_socket_error_reason("create_server", server_socket);
            //�½�ʧ�ܺ󣬹ر��½���socket���ȴ��´��½�
            close(server_socket);
            return ESP_FAIL;
        }
        //�����½�server socket����
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(g_init_data.config_data.service_port);
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        //bind:��ַ�İ�
        if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            show_socket_error_reason("bind_server", server_socket);
            //bindʧ�ܺ󣬹ر��½���socket���ȴ��´��½�
            close(server_socket);
            return ESP_FAIL;
        }
    }
    //listen���´�ʱ��ֱ�Ӽ���
    if (listen(server_socket, 5) < 0)
    {
        show_socket_error_reason("listen_server", server_socket);
        //listenʧ�ܺ󣬹ر��½���socket���ȴ��´��½�
        close(server_socket);
        return ESP_FAIL;
    }
    //accept����Ѱȫ���Ӷ���
    connect_socket = accept(server_socket, (struct sockaddr *)&client_addr, &socklen);
    if (connect_socket < 0)
    {
        show_socket_error_reason("accept_server", connect_socket);
        //acceptʧ�ܺ󣬹ر��½���socket���ȴ��´��½�
        close(server_socket);
        return ESP_FAIL;
    }
    return ESP_OK;
}


/*
* ����tcp client
* @param[in]   void                 :��
* @retval      void                :��
* @note        �޸���־ 
*               Ver0.0.1:
                    hx-zsj, 2018/08/06, ��ʼ���汾\n 
*               Ver0.0.12:
                    hx-zsj, 2018/08/09, ����close socket\n 
*/
esp_err_t create_tcp_client()
{
    //�½�socket
    connect_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (connect_socket < 0)
    {
        //��ӡ������Ϣ
        show_socket_error_reason("create client", connect_socket);
        printf("file:%s, line:%d, socket errno = %d, errno = %s\r\n", 
            __FILE__, __LINE__, errno, strerror(errno));
        //�½�ʧ�ܺ󣬹ر��½���socket���ȴ��´��½�
        return ESP_FAIL;
    }
    //�������ӷ�������Ϣ
    server_addr.sin_family = AF_INET;    
    server_addr.sin_port = htons(g_init_data.config_data.service_port);
    server_addr.sin_addr.s_addr = inet_addr(g_init_data.config_data.service_ip_str);

    printf("file:%s, line:%d, ip = %s, port = %d\r\n", 
        __FILE__, __LINE__, g_init_data.config_data.service_ip_str, g_init_data.config_data.service_port);
        
    //���ӷ�����
    if (connect(connect_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        //��ӡ������Ϣ
        show_socket_error_reason("client connect", connect_socket);
        //����ʧ�ܺ󣬹ر�֮ǰ�½���socket���ȴ��´��½�
        close(connect_socket);
        connect_socket = -1;
        printf("file:%s, line:%d, socket errno = %d, errno = %s\r\n", 
            __FILE__, __LINE__, errno, strerror(errno));
        return ESP_FAIL;
    }
    return ESP_OK;
}

/*
* ��ȡsocket�������
* @param[in]   socket                 :socket���
* @retval      void                :��
* @note        �޸���־ 
*               Ver0.0.1:
                    hx-zsj, 2018/08/06, ��ʼ���汾\n 
*/
int get_socket_error_code(int socket)
{
    int result;
    u32_t optlen = sizeof(int);
    int err = getsockopt(socket, SOL_SOCKET, SO_ERROR, &result, &optlen);
    if (err == -1)
    {
        //WSAGetLastError();
        return -1;
    }
    return result;
}

/*
* ��ȡsocket����ԭ��
* @param[in]   socket                 :socket���
* @retval      void                :��
* @note        �޸���־ 
*               Ver0.0.1:
                    hx-zsj, 2018/08/06, ��ʼ���汾\n 
*/
int show_socket_error_reason(const char *str, int socket)
{
    int err = get_socket_error_code(socket);

    if (err != 0)
    {
    }

    return err;
}

/*
* ���socket
* @param[in]   socket                 :socket���
* @retval      void                :��
* @note        �޸���־ 
*               Ver0.0.1:
                    hx-zsj, 2018/08/06, ��ʼ���汾\n 
*/
int check_working_socket()
{
    int ret;
#if EXAMPLE_ESP_TCP_MODE_SERVER
    ret = get_socket_error_code(server_socket);
    if (ret != 0)
    {
//        ESP_LOGW(TAG, "server socket error %d %s", ret, strerror(ret));
    }
    if (ret == ECONNRESET)
    {
        return ret;
    }
#endif
//    ESP_LOGD(TAG, "check connect_socket");
    ret = get_socket_error_code(connect_socket);
    if (ret != 0)
    {
//        ESP_LOGW(TAG, "connect socket error %d %s", ret, strerror(ret));
    }
    if (ret != 0)
    {
        return ret;
    }
    return 0;
}

/*
* �ر�socket
* @param[in]   socket                 :socket���
* @retval      void                :��
* @note        �޸���־ 
*               Ver0.0.1:
                    hx-zsj, 2018/08/06, ��ʼ���汾\n 
*/
void close_socket()
{
    printf("file:%s, line:%d, close_socket\r\n", __FILE__, __LINE__);
    close(connect_socket);
    connect_socket = -1;
//    close(server_socket);
}

