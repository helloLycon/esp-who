/*
* @file         tcp_bsp.c 
* @brief        wifi连接事件处理和socket收发数据处理
* @details      在官方源码的基础是适当修改调整，并增加注释
* @author       hx-zsj 
* @par Copyright (c):  
*               红旭无线开发团队，QQ群：671139854
* @par History:          
*               Ver0.0.1:
                     hx-zsj, 2018/08/08, 初始化版本\n 
*/

/* 
=============
头文件包含
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
全局变量定义
=========================== 
*/
//socket
static int server_socket = -1;                           //服务器socket
static struct sockaddr_in server_addr;                  //server地址
static struct sockaddr_in client_addr;                  //client地址
static unsigned int socklen = sizeof(client_addr);      //地址长度
static int connect_socket = -1;                          //连接socket
bool g_rxtx_need_restart = false;                       //异常后，重新连接标记
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
函数声明
=========================== 
*/

static void esp_initialize_sntp(void)
{
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "ntp1.aliyun.com");
    sntp_init();
}

/*
* 时间同步
* @param   void: 无
* @retval      void                :无
* @note        修改日志 
*               Ver0.0.1:
                    yf-lwj, 2020/01/13, 初始化版本\n 
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
* 检测socket 连接状态
* @param[out]   int *                 :获取socket 数值
* @retval      void                :无
* @note        修改日志 
*               Ver0.0.1:
                    yf-lwj, 2020/01/09, 初始化版本\n 
*/
int get_socket_status(int *socket_fd)
{
    if(socket_fd) {
        *socket_fd = connect_socket;
    }
    return connect_socket;
}

/*
* 接收数据任务
* @param[in]   void                 :无
* @retval      void                :无
* @note        修改日志 
*               Ver0.0.1:
                    hx-zsj, 2018/08/06, 初始化版本\n 
*/
int recv_data()
{
    unsigned char sn_len;
    int recv_count = 0;
    int len = 0;            //长度
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
    
    //清空缓存
    memset(g_recv_buf, 0x00, 1024);
    //读取接收数据
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
        //打印接收到的数组
        //接收数据回发
//        send(connect_socket, databuff, strlen(databuff), 0);
        //sendto(connect_socket, databuff , sizeof(databuff), 0, (struct sockaddr *) &remote_addr,sizeof(remote_addr));
    }
    else
    {
        //打印错误信息
        show_socket_error_reason("recv_data", connect_socket);
        //服务器故障，标记重连
        g_rxtx_need_restart = true;
        
#if TCP_SERVER_CLIENT_OPTION
        //服务器接收异常，不用break后close socket,因为有其他client
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
    //标记重连
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
    static config_para cfg = {0};
    if(0 == cfg.device_id[0]) {
        xSemaphoreTake(g_data_mutex, portMAX_DELAY);
        cfg = g_init_data.config_data;
        xSemaphoreGive(g_data_mutex);
    }

//    printf("file:%s, line:%d, in package_message\r\n", __FILE__, __LINE__);

    /* 报文头 */
    packet_head = htons(PACKET_HEAD);
//    printf("file:%s, line:%d, packet_head = %x\r\n", __FILE__, __LINE__, packet_head);
    memcpy(packet_buf, &packet_head, 2);
    packet_len = 2;
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    /* 版本号 */
    packet_buf[packet_len] = PACKET_VERSION;
    packet_len++;
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    /* 状态 */
    packet_buf[packet_len] = packet_data.status;
    packet_len++;

    /* 信号强度 */
    memset(&packet_buf[packet_len], 0x00, 2);
    packet_len += 2;
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    /* 设备编号长度 */
    device_id_len = (unsigned char)strlen(cfg.device_id);
    packet_buf[packet_len] = device_id_len;
    packet_len++;
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    /* 设备编号 */
    if (device_id_len > 0)
    {
        memcpy(&packet_buf[packet_len], cfg.device_id, device_id_len);
        packet_len += device_id_len;
    }
    //预留
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    /* 命令字 */
    packet_buf[packet_len] = packet_data.type;
    packet_len++;
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    /* 数据长度 */
    net_send_len = htons(packet_data.send_len);
    memcpy(&packet_buf[packet_len], &net_send_len, 2);
    packet_len += 2;
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    if (SEND_FACE_PIC_CODE == packet_data.type)
    {
        /* 数据 */
        jpg_data = (send_jpeg_info *)packet_data.data;
        /* 图片编号 */
        net_num = htons(jpg_data->num);
        memcpy(&packet_buf[packet_len], &net_num, 2);
        packet_len += 2;
//        printf("file:%s, line:%d, jpg_data->num = %d\r\n", __FILE__, __LINE__, jpg_data->num);

        /* 归类编号 */
        net_class_num = htons(g_init_data.start_time);
        memcpy(&packet_buf[packet_len], &net_class_num, 4);
        packet_len += 4;
//        printf("file:%s, line:%d, g_init_data.start_time = %ld\r\n", __FILE__, __LINE__, g_init_data.start_time);

        /* 图片创建时间 */
        net_create_time = htonl(jpg_data->create_time);
        memcpy(&packet_buf[packet_len], &net_create_time, 4);
        packet_len += 4;
//        printf("file:%s, line:%d, jpg_data->create_time = %ld\r\n", __FILE__, __LINE__, jpg_data->create_time);

        /* 偏移量 */
        net_send_count = htonl(jpg_data->send_count);
        memcpy(&packet_buf[packet_len], &net_send_count, 4);
        packet_len += 4;
//        printf("file:%s, line:%d, jpg_data->send_count = %lu\r\n", __FILE__, __LINE__, jpg_data->send_count);

        /* 数据包内容 */
        if (packet_data.send_len > 14)
        {
            memcpy(&packet_buf[packet_len], jpg_data->buf, (packet_data.send_len - 14));
            packet_len += (packet_data.send_len - 14);
        }
    }
    else if (SEND_HEARTBEAT_CODE == packet_data.type)
    {
        heartbeat_data = (send_heartbeat_info *)packet_data.data;
        
        /* 获取电池电量 */
        net_battery = htons(heartbeat_data->battery);
        memcpy(&packet_buf[packet_len], &net_battery, 2);
        packet_len += 2;

        /* 获取时间 */
        net_heartbeat_time = htonl(heartbeat_data->cur_time);
        memcpy(&packet_buf[packet_len], &net_heartbeat_time, 4);
        packet_len += 4;
    }
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    /* 校验和 */
    check_sum = 0;
    for (loop = 0; loop < packet_len; loop++)
    {
        check_sum += packet_buf[loop];
    }

    check_sum_ret = check_sum % 256;
    packet_buf[packet_len] = check_sum_ret;
    packet_len++;
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    /* 数据尾 */
    packet_buf[packet_len] = PACKET_END;
    packet_len++;
//    printf("file:%s, line:%d, packet_len = %d\r\n", __FILE__, __LINE__, packet_len);

    *package_len = packet_len;

    return ;
}

/*
* 发送数据任务
* @param[in]   void                 :无
* @retval      void                :无
* @note        修改日志 
*               Ver0.0.1:
                    hx-zsj, 2020/01/07, 初始化版本\n 
*/
int send_data(packet_info packet_data, bool recv_needed)
{
    int ret;
    int len = 0;            //长度
    int send_len = 0;
    static unsigned char is_first = TRUE;

    if (NULL == g_send_buf)
    {
        g_send_buf = (unsigned char *)malloc(APP_PACKET_DATA_LEN + 128);
        if (NULL == g_send_buf)
        {
            printf("file:%s, line:%d, malloc failed errno = %d, errno = %s\r\n", 
                __FILE__, __LINE__, errno, strerror(errno));
            return CAMERA_ERROR_MALLOC_FAILED;
        }
    }
    
    //清空缓存
    memset(g_send_buf, 0x00, APP_PACKET_DATA_LEN + 128);

//    printf("file:%s, line:%d, begin package_message\r\n", __FILE__, __LINE__);
    package_message(packet_data, &len, g_send_buf);
    //读取接收数据
//    printf("file:%s, line:%d, connect_socket = %d, len = %d\r\n", 
//        __FILE__, __LINE__, connect_socket, len);

#if  0
    static uint16_t cnt = 0;
    memcpy(g_send_buf, &cnt, 2);
    cnt++;
    printf("cnt = %d len = %d\n", cnt, len);
    memset(g_send_buf+2, snsn, len-2);
#endif
    send_len = send(connect_socket, g_send_buf, len, 0);
    g_rxtx_need_restart = false;
    if (send_len > 0)
    {
        //g_total_data += len;
        //打印接收到的数组
        //接收数据回发
        //sendto(connect_socket, databuff , sizeof(databuff), 0, (struct sockaddr *) &remote_addr,sizeof(remote_addr));
//        printf("file:%s, line:%d, send len = %d\r\n", __FILE__, __LINE__, send_len);
    }
    else
    {
        //打印错误信息
        show_socket_error_reason("recv_data", connect_socket);
        //服务器故障，标记重连
        g_rxtx_need_restart = true;
        
        printf("file:%s, line:%d, send failed errno = %d, errno = %s\r\n", 
            __FILE__, __LINE__, errno, strerror(errno));
        //服务器接收异常，不用break后close socket,因为有其他client
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
    //<B1><EA><BC><C7><D6><D8><C1><AC>
    g_rxtx_need_restart = true;
    vTaskDelete(NULL);
*/
//    printf("file:%s, line:%d, send_len = %d\r\n", __FILE__, __LINE__, send_len);

    //vTaskDelay(10 / portTICK_PERIOD_MS);
    return CAMERA_OK;
}


/*
* 建立tcp server
* @param[in]   isCreatServer          :首次true，下次false
* @retval      void                 :无
* @note        修改日志 
*               Ver0.0.1:
                    hx-zsj, 2018/08/06, 初始化版本\n 
*/
esp_err_t create_tcp_server(bool isCreatServer)
{
    //首次建立server
    if (isCreatServer)
    {
        //新建socket
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket < 0)
        {
            show_socket_error_reason("create_server", server_socket);
            //新建失败后，关闭新建的socket，等待下次新建
            close(server_socket);
            return ESP_FAIL;
        }
        //配置新建server socket参数
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(g_init_data.config_data.service_port);
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        //bind:地址的绑定
        if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        {
            show_socket_error_reason("bind_server", server_socket);
            //bind失败后，关闭新建的socket，等待下次新建
            close(server_socket);
            return ESP_FAIL;
        }
    }
    //listen，下次时，直接监听
    if (listen(server_socket, 5) < 0)
    {
        show_socket_error_reason("listen_server", server_socket);
        //listen失败后，关闭新建的socket，等待下次新建
        close(server_socket);
        return ESP_FAIL;
    }
    //accept，搜寻全连接队列
    connect_socket = accept(server_socket, (struct sockaddr *)&client_addr, &socklen);
    if (connect_socket < 0)
    {
        show_socket_error_reason("accept_server", connect_socket);
        //accept失败后，关闭新建的socket，等待下次新建
        close(server_socket);
        return ESP_FAIL;
    }
    return ESP_OK;
}


/*
* 建立tcp client
* @param[in]   void                 :无
* @retval      void                :无
* @note        修改日志 
*               Ver0.0.1:
                    hx-zsj, 2018/08/06, 初始化版本\n 
*               Ver0.0.12:
                    hx-zsj, 2018/08/09, 增加close socket\n 
*/
esp_err_t create_tcp_client()
{
    xSemaphoreTake(g_data_mutex, portMAX_DELAY);
    config_para cfg = g_init_data.config_data;
    xSemaphoreGive(g_data_mutex);
    //新建socket
    connect_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (connect_socket < 0)
    {
        //打印报错信息
        show_socket_error_reason("create client", connect_socket);
        printf("file:%s, line:%d, socket errno = %d, errno = %s\r\n", 
            __FILE__, __LINE__, errno, strerror(errno));
        //新建失败后，关闭新建的socket，等待下次新建
        return ESP_FAIL;
    }
    //配置连接服务器信息
    server_addr.sin_family = AF_INET;    
    server_addr.sin_port = htons(cfg.service_port);
    server_addr.sin_addr.s_addr = inet_addr(cfg.service_ip_str);

    printf("file:%s, line:%d, ip = %s, port = %d\r\n", 
        __FILE__, __LINE__, cfg.service_ip_str, cfg.service_port);
        
    //连接服务器
    if (connect(connect_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        //打印报错信息
        show_socket_error_reason("client connect", connect_socket);
        //连接失败后，关闭之前新建的socket，等待下次新建
        close(connect_socket);
        connect_socket = -1;
        printf("file:%s, line:%d, socket errno = %d, errno = %s\r\n", 
            __FILE__, __LINE__, errno, strerror(errno));
        return ESP_FAIL;
    }
    return ESP_OK;
}

/*
* 获取socket错误代码
* @param[in]   socket                 :socket编号
* @retval      void                :无
* @note        修改日志 
*               Ver0.0.1:
                    hx-zsj, 2018/08/06, 初始化版本\n 
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
* 获取socket错误原因
* @param[in]   socket                 :socket编号
* @retval      void                :无
* @note        修改日志 
*               Ver0.0.1:
                    hx-zsj, 2018/08/06, 初始化版本\n 
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
* 检查socket
* @param[in]   socket                 :socket编号
* @retval      void                :无
* @note        修改日志 
*               Ver0.0.1:
                    hx-zsj, 2018/08/06, 初始化版本\n 
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
* 关闭socket
* @param[in]   socket                 :socket编号
* @retval      void                :无
* @note        修改日志 
*               Ver0.0.1:
                    hx-zsj, 2018/08/06, 初始化版本\n 
*/
void close_socket()
{
    printf("file:%s, line:%d, close_socket\r\n", __FILE__, __LINE__);
    close(connect_socket);
    connect_socket = -1;
//    close(server_socket);
}

