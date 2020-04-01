/* ESPRESSIF MIT License
 * 
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 * 
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_camera.h"
#include "app_camera.h"
#include "sdkconfig.h"
#include "tcp_bsp.h"
#include "md5.h"
#include "esp_system.h"
#include "common.h"
#include "camera_error.h"
#include "i2c_example_main.h"
#include "adc1_example_main.h"

static const char *TAG = "app_camera";
/* add by liuwenjian 2020-3-4 begin */
pic_queue *g_pic_queue_head = NULL;
pic_queue *g_pic_queue_tail = NULL;

static int send_jpeg(pic_queue *send_pic)
{
    my_MD5_CTX md5;
    int i;
    int ret;
    char md5_str[MD5_STR_LEN + 1];
    unsigned char md5_value[MD5_SIZE];
    packet_info packet_send_data;
    send_jpeg_info jpeg_data;
    int send_len = 0;
    int socket_fd = -1;
    unsigned long send_time;
//    int rate;
    esp_err_t sock_ret = ESP_OK;
//    struct timeval start_tv, end_tv;
    static unsigned long sn = 1;

//    vTaskDelay(10000);

    get_socket_status(&socket_fd);
//    printf("file:%s, line:%d, socket_fd = %d\r\n", __FILE__, __LINE__, socket_fd);
    
    if (socket_fd < 0)
    {
        sock_ret = create_tcp_client();
        printf("file:%s, line:%d, sock_ret = %d\r\n", __FILE__, __LINE__, sock_ret);
    }

    if (ESP_OK == sock_ret)
    {
//        gettimeofday(&start_tv, NULL);
        if (NULL != send_pic)
        {
            // init md5
            my_MD5Init(&md5);
            
            while (send_pic->pic_len > send_len)
            {
                if (send_pic->pic_len - send_len > 1024)
                {
                    my_MD5Update(&md5, (send_pic->pic_info + send_len), 1024);
                    jpeg_data.buf = send_pic->pic_info + send_len;
                    jpeg_data.create_time = send_pic->cur_time;
                    jpeg_data.num = sn;
                    jpeg_data.send_count = send_len;
                    packet_send_data.data = (void *)(&jpeg_data);
                    packet_send_data.send_len = 1024 + 14;
                    packet_send_data.status = HAVE_DATA;
                    packet_send_data.type = SEND_FACE_PIC_CODE;
                    
    //                printf("file:%s, line:%d, begin send_data\r\n", __FILE__, __LINE__);
                    ret = send_data(packet_send_data, true);
                    if (CAMERA_OK != ret)
                    {
                        printf("file:%s, line:%d, send_data failed\r\n", __FILE__, __LINE__);
                        close_socket();
                        return ret;
                    }
    /*                else
                    {
    //                    printf("file:%s, line:%d, begin recv_data\r\n", __FILE__, __LINE__);
                        ret = recv_data();
                        if (0 != ret)
                        {
                            printf("file:%s, line:%d, recv_data failed\r\n", __FILE__, __LINE__);
                            close_socket();
                            return ;
                        }
                    }*/
                    send_len += 1024;
                }
                else
                {
                    my_MD5Update(&md5, (send_pic->pic_info + send_len), (send_pic->pic_len - send_len));
                    jpeg_data.buf = send_pic->pic_info + send_len;
                    jpeg_data.create_time = send_pic->cur_time;
                    jpeg_data.num = sn;
                    jpeg_data.send_count = send_len;
                    packet_send_data.data = (void *)(&jpeg_data);
                    packet_send_data.send_len = send_pic->pic_len - send_len + 14;
                    packet_send_data.status = HAVE_DATA;
                    packet_send_data.type = SEND_FACE_PIC_CODE;
                    
    //                printf("file:%s, line:%d, begin send_data\r\n", __FILE__, __LINE__);
                    ret = send_data(packet_send_data, true);
                    if (CAMERA_OK != ret)
                    {
                        printf("file:%s, line:%d, send_data failed\r\n", __FILE__, __LINE__);
                        close_socket();
                        return ret;
                    }
    /*                else
                    {
    //                    printf("file:%s, line:%d, begin recv_data\r\n", __FILE__, __LINE__);
                        ret = recv_data();
                        if (0 != ret)
                        {
                            printf("file:%s, line:%d, recv_data failed\r\n", __FILE__, __LINE__);
                            close_socket();
                            return ;
                        }
                    }*/
                    send_len = send_pic->pic_len;
                }
            }

            my_MD5Final(&md5, md5_value);
            for (i = 0; i < MD5_SIZE; i++)
            {
                snprintf((md5_str + i * 2), (2 + 1), "%02x", md5_value[i]);
            }
            md5_str[MD5_STR_LEN] = '\0'; // add end
            
    //        packet_send_data.buf = buf + send_len;
            jpeg_data.buf = (unsigned char *)md5_str;
            jpeg_data.create_time = send_pic->cur_time;
            jpeg_data.num = sn;
            sn++;
            jpeg_data.send_count = send_len;
            packet_send_data.data = (void *)(&jpeg_data);
            packet_send_data.send_len = MD5_STR_LEN + 14;
            packet_send_data.status = NOT_HAVE_DATA;
            packet_send_data.type = SEND_FACE_PIC_CODE;

    //        printf("file:%s, line:%d, md5_str = %s\r\n", __FILE__, __LINE__, md5_str);
            ret = send_data(packet_send_data, true);
            if (CAMERA_OK != ret)
            {
                printf("file:%s, line:%d, send_data failed\r\n", __FILE__, __LINE__);
                close_socket();
                return ret;
            }
/*        else
        {
//            printf("file:%s, line:%d, begin recv_data\r\n", __FILE__, __LINE__);
            ret = recv_data();
            if (0 != ret)
            {
                printf("file:%s, line:%d, recv_data failed ret = %d\r\n", __FILE__, __LINE__, ret);
                close_socket();
                return ;
            }
        }*/
        
/*        gettimeofday(&end_tv, NULL);
        send_time = (end_tv.tv_sec - start_tv.tv_sec) * 1000000 + (end_tv.tv_usec - start_tv.tv_usec);
        rate = send_pic->pic_len * 1000 / send_time;*/
//        printf("file:%s, line:%d, len = %d, send_time = %lu, rate = %d, time = %ld\r\n", 
//            __FILE__, __LINE__, len, send_time, rate, end_tv.tv_sec);
        }
        else
        {
            jpeg_data.buf = NULL;
            jpeg_data.create_time = 0;
            jpeg_data.num = 0xffff;
            jpeg_data.send_count = 0;
            packet_send_data.data = (void *)(&jpeg_data);
            packet_send_data.send_len = 14;
            packet_send_data.status = NOT_HAVE_DATA;
            packet_send_data.type = SEND_FACE_PIC_CODE;
            
//                printf("file:%s, line:%d, begin send_data\r\n", __FILE__, __LINE__);
            ret = send_data(packet_send_data, true);
            if (CAMERA_OK != ret)
            {
                printf("file:%s, line:%d, send_data failed\r\n", __FILE__, __LINE__);
                close_socket();
                return ret;
            }
        }
    }
    else
    {
        return CAMERA_ERROR_CREATE_SOCKET_FAILED;
    }

    return CAMERA_OK;
}

/* 图片出队函数 */
void pic_out_queue()
{
    int ret;
    pic_queue *send_pic = g_pic_queue_head;

    if (NULL == send_pic)
    {
        printf("file:%s, line:%d, queue is empty!\r\n", __FILE__, __LINE__);
        return ;
    }

    if(noManFlag) {
        goto skip_send;
    }

    //printf("file:%s, line:%d, send_pic = %p, next = %p, len = %d\r\n", 
    //    __FILE__, __LINE__, send_pic, send_pic->next, send_pic->pic_len);
    ret = send_jpeg(send_pic);
    if (CAMERA_OK != ret)
    {
        printf("file:%s, line:%d, send_jpeg failed! ret = %d\r\n", __FILE__, __LINE__, ret);
        return ;
    }

skip_send:
    g_pic_queue_head = g_pic_queue_head->next;
    if (NULL == g_pic_queue_head)
    {
        g_pic_queue_tail = NULL;
    }

    free(send_pic->pic_info);
    free(send_pic);
    send_pic = NULL;

    return ;
}

/* 图片入队函数 */
void pic_in_queue(int len, unsigned char *buf)
{
    pic_queue *cur_pic = NULL;
    cur_pic = (pic_queue *)heap_caps_malloc(sizeof(pic_queue), MALLOC_CAP_SPIRAM);
    if (NULL == cur_pic)
    {
        printf("file:%s, line:%d, heap_caps_malloc %d, failed!\r\n", __FILE__, __LINE__, sizeof(pic_queue));
        return ;
    }

    cur_pic->pic_info = (unsigned char *)heap_caps_malloc((len + 1), MALLOC_CAP_SPIRAM);
    if (NULL == cur_pic->pic_info)
    {
        free(cur_pic);
        printf("file:%s, line:%d, heap_caps_malloc %d, failed!\r\n", __FILE__, __LINE__, len);
        return ;
    }
    cur_pic->next = NULL;
    cur_pic->pic_len = len;
    memcpy(cur_pic->pic_info, buf, len);
    cur_pic->cur_time = time(NULL);
    
    if (NULL != g_pic_queue_tail)
    {
        g_pic_queue_tail->next = cur_pic;
//        printf("file:%s, line:%d, in pic_in_queue, tail = %p, next = %p, head = %p\r\n", 
//            __FILE__, __LINE__, g_pic_queue_tail, g_pic_queue_tail->next, g_pic_queue_head);
//        printf("file:%s, line:%d, g_pic_queue_tail = %p, g_pic_queue_tail->next = %p, g_pic_queue_head = %p\r\n", 
//            __FILE__, __LINE__, g_pic_queue_tail, g_pic_queue_tail->next, g_pic_queue_head);
        g_pic_queue_tail = cur_pic;
//        printf("file:%s, line:%d, g_pic_queue_tail = %p, g_pic_queue_tail->next = %p, g_pic_queue_head = %p\r\n", 
//            __FILE__, __LINE__, g_pic_queue_tail, g_pic_queue_tail->next, g_pic_queue_head);
    }
    else
    {
        g_pic_queue_tail = cur_pic;
        g_pic_queue_head = cur_pic;
    }

    return ;
}

void send_heartbeat_packet()
{
    int ret;
    int socket_fd;
    int sock_ret = -1;
    send_heartbeat_info heartbeat_data;
    packet_info packet_send_data;

    heartbeat_data.battery = (uint16_t)adc_read_battery_percent();
    heartbeat_data.cur_time = time(NULL);

    packet_send_data.type = SEND_HEARTBEAT_CODE;
    packet_send_data.status = 0x00;
    packet_send_data.send_len = 6;
    packet_send_data.data = (void *)(&heartbeat_data);

    get_socket_status(&socket_fd);
//    printf("file:%s, line:%d, socket_fd = %d\r\n", __FILE__, __LINE__, socket_fd);
    
    if (socket_fd < 0)
    {
        sock_ret = create_tcp_client();
        printf("file:%s, line:%d, sock_ret = %d\r\n", __FILE__, __LINE__, sock_ret);
    }
    else
    {
        sock_ret = ESP_OK;
    }

    if (ESP_OK == sock_ret)
    {
        printf("file:%s, line:%d, begin send_data\r\n", __FILE__, __LINE__);
        ret = send_data(packet_send_data, false);
        if (CAMERA_OK != ret)
        {
            printf("file:%s, line:%d, send_data failed ret = %d\r\n", 
                __FILE__, __LINE__, ret);
            close_socket();
        }
    }
}
/* add by liuwenjian 2020-3-4 end */

static esp_err_t stream_send()
{
    char timeStr[32];
    uint8_t reg[8];
    time_t timeValue;
    struct tm tmValue, rtcValue;
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    uint32_t old_time, cur_time;

#if 0
//    uint8_t *ptr = NULL;
//    char *part_buf[64];
    printf("file:%s, line:%d, begin esp_wait_sntp_sync\r\n", __FILE__, __LINE__);
    /* add by liuwenjian 2020-3-4 begin */
    /* 用于时间同步 */
    esp_wait_sntp_sync();
    time(&timeValue);
    localtime_r(&timeValue, &tmValue);
    pcf8563RtcRead(I2C_RTC_MASTER_NUM, reg);
    pcf8563RtcToString(reg, timeStr);
    printf("==> rtc: %s\r\n", timeStr);
    pcf8563RtcWrite(I2C_RTC_MASTER_NUM, &tmValue);
    printf("file:%s, line:%d, ---->(%d-%02d-%02d %02d:%02d:%02d)\r\n", __FILE__, __LINE__, tmValue.tm_year+1900, tmValue.tm_mon+1, tmValue.tm_mday, tmValue.tm_hour, tmValue.tm_min, tmValue.tm_sec);

//    int64_t test_frame = 0;
    g_init_data.start_time = time(NULL);

    /* 用于发送心跳包 */
    send_heartbeat_packet();
    /* add by liuwenjian 2020-3-4 end */

#ifdef CONFIG_LED_ILLUMINATOR_ENABLED
    enable_led(true);
    isStreaming = true;
#endif

#endif
    /* modify by liuwenjian 2020-3-4 begin */
    /* 录像计时 */
    cur_time = old_time = time(NULL);
    printf("file:%s, line:%d, begin while, cur_time = %d\r\n", __FILE__, __LINE__, cur_time);
    ESP_LOGI(TAG, "<---------START CAPTURE--------->");
    while (false == noManFlag)
    {
        fb = esp_camera_fb_get();
    
//        printf("file:%s, line:%d, fb = %p\r\n", __FILE__, __LINE__, fb);
        if (!fb)
        {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
        }
        else
        {
                if (fb->format != PIXFORMAT_JPEG)
                {
                    bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                    esp_camera_fb_return(fb);
                    fb = NULL;
                    if (!jpeg_converted)
                    {
                        ESP_LOGE(TAG, "JPEG compression failed");
                        res = ESP_FAIL;
                    }
                }
                else
                {
                    _jpg_buf_len = fb->len;
                    _jpg_buf = fb->buf;
//                    printf("file:%s, line:%d, fb->len = %d, time = %ld\r\n", 
//                        __FILE__, __LINE__, fb->len, time(NULL));
                    /* 图片入队列 */
                    if(false == noManFlag) {
                        pic_in_queue(fb->len, fb->buf);
                    }
//                    send_jpeg(fb->len, fb->buf);
                    cur_time = xTaskGetTickCount();
                    if ((cur_time - old_time > (CAMERA_VIDEO_TIME*configTICK_RATE_HZ) ) && (FALSE == g_camera_over))
                    {
                        /* 超时结束录制 */
                        printf("file:%s, line:%d, camera over, cur_time = %d\r\n", __FILE__, __LINE__, cur_time);
                        g_camera_over = TRUE;
                        break;
                    }
/*                    ptr = (uint8_t *)malloc(fb->len);
                    if (NULL == ptr)
                    {
                        printf("file:%s, line:%d, time = %ld, malloc failed!\r\n", 
                            __FILE__, __LINE__, time(NULL));
                    }*/
//                    test_frame = esp_timer_get_time();
//                    printf("file:%s, line:%d, test_frame = %lld, time = %ld\r\n", 
//                        __FILE__, __LINE__, test_frame, time(NULL));
                }
        }

        if (fb)
        {
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        }
        else if (_jpg_buf)
        {
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
    }
    
    /* modify by liuwenjian 2020-3-4 end */

#ifdef CONFIG_LED_ILLUMINATOR_ENABLED
    isStreaming = false;
    enable_led(false);
#endif

    return res;
}

/* 获取摄像头图形并发送 */
static void get_camera_data_task(void *pvParameter)
{
    stream_send();
    vTaskDelete(NULL);
}

/* 接收服务器数据 */
static void recv_data_task(void *pvParameter)
{
    int ret;
//    int sock_ret;
    int socket_fd = -1;

    while (true)
    {
        get_socket_status(&socket_fd);
/*        if (socket_fd < 0)
        {
            sock_ret = create_tcp_client();
            printf("file:%s, line:%d, sock_ret = %d\r\n", __FILE__, __LINE__, sock_ret);
        }*/

        if (socket_fd >= 0)
        {
            ret = recv_data();
            if ((0 != ret) && (1 != ret))
            {
                printf("file:%s, line:%d, recv_data failed\r\n", __FILE__, __LINE__);
                close_socket();
            }
        }
        else
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    vTaskDelete(NULL);
}

static void flash_led(void) {
    for (int i=0; i<3;i++) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
        gpio_set_level(15, 0);
        vTaskDelay(50 / portTICK_PERIOD_MS);
        gpio_set_level(15, 1);
    }
}

/* 队列读取图片并发送出去 */
static void send_queue_pic_task(void *pvParameter)
{
    int ret;
    extern int max_sleep_uptime;
    extern unsigned char is_connect;
    char timeStr[32];
    time_t timeValue;
    struct tm tmValue, rtcValue;
    uint8_t reg[8];
    printf("file:%s, line:%d, begin esp_wait_sntp_sync\r\n", __FILE__, __LINE__);

    /* 用于时间同步 */
    //printf("skip esp_wait_sntp_sync.....\n");
    while( !is_connect ) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    esp_wait_sntp_sync();
    time(&timeValue);
    localtime_r(&timeValue, &tmValue);
    pcf8563RtcRead(I2C_RTC_MASTER_NUM, reg);
    pcf8563RtcToString(reg, timeStr);
    printf("==> rtc: %s\r\n", timeStr);
    pcf8563RtcWrite(I2C_RTC_MASTER_NUM, &tmValue);
    printf("file:%s, line:%d, ---->(%d-%02d-%02d %02d:%02d:%02d)\r\n", __FILE__, __LINE__, tmValue.tm_year+1900, tmValue.tm_mon+1, tmValue.tm_mday, tmValue.tm_hour, tmValue.tm_min, tmValue.tm_sec);

    //    int64_t test_frame = 0;
    g_init_data.start_time = time(NULL);

    /* 用于发送心跳包 */
    send_heartbeat_packet();

    while (true)
    {
        if (noManFlag || ((NULL == g_pic_queue_head)&&(TRUE == g_camera_over)) )
        {
//            printf("file:%s, line:%d, send over\r\n", __FILE__, __LINE__);
            ret = send_jpeg(NULL);
            if (CAMERA_OK != ret)
            {
                printf("file:%s, line:%d, send_jpeg failed! ret = %d\r\n", __FILE__, __LINE__, ret);
                vTaskDelete(NULL);
                return ;
            }
            /* send over: okay */
            printf("======send over========\r\n");
            flash_led();
            g_pic_send_over = TRUE;
            if( max_sleep_uptime == DEF_MAX_SLEEP_TIME ) {
                upgrade_block();
                printf("=-> send shutdown request\n");
                uart_write_bytes(ECHO_UART_NUM, CORE_SHUT_DOWN_REQ, strlen(CORE_SHUT_DOWN_REQ)+1);
            }
            break;
        }
        else if (NULL != g_pic_queue_head)
        {
            if ((NULL != g_pic_queue_head->next) || (TRUE == g_camera_over))
            {
                pic_out_queue();
            }
        }
        else
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }

    vTaskDelete(NULL);
}

void app_camera_main ()
{
#if CONFIG_CAMERA_MODEL_ESP_EYE
    /* IO13, IO14 is designed for JTAG by default,
     * to use it as generalized input,
     * firstly declair it as pullup input */
    gpio_config_t conf;
    conf.mode = GPIO_MODE_INPUT;
    conf.pull_up_en = GPIO_PULLUP_ENABLE;
    conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    conf.intr_type = GPIO_INTR_DISABLE;
    conf.pin_bit_mask = 1LL << 13;
    gpio_config(&conf);
    conf.pin_bit_mask = 1LL << 14;
    gpio_config(&conf);
#endif

#ifdef CONFIG_LED_ILLUMINATOR_ENABLED
    gpio_set_direction(CONFIG_LED_LEDC_PIN,GPIO_MODE_OUTPUT);
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_8_BIT,            // resolution of PWM duty
        .freq_hz         = 1000,                        // frequency of PWM signal
        .speed_mode      = LEDC_LOW_SPEED_MODE,  // timer mode
        .timer_num       = CONFIG_LED_LEDC_TIMER        // timer index
    };
    ledc_channel_config_t ledc_channel = {
        .channel    = CONFIG_LED_LEDC_CHANNEL,
        .duty       = 0,
        .gpio_num   = CONFIG_LED_LEDC_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .hpoint     = 0,
        .timer_sel  = CONFIG_LED_LEDC_TIMER
    };
    #ifdef CONFIG_LED_LEDC_HIGH_SPEED_MODE
    ledc_timer.speed_mode = ledc_channel.speed_mode = LEDC_HIGH_SPEED_MODE;
    #endif
    switch (ledc_timer_config(&ledc_timer))
    {
        case ESP_ERR_INVALID_ARG: ESP_LOGE(TAG, "ledc_timer_config() parameter error"); break;
        case ESP_FAIL: ESP_LOGE(TAG, "ledc_timer_config() Can not find a proper pre-divider number base on the given frequency and the current duty_resolution"); break;
        case ESP_OK: if (ledc_channel_config(&ledc_channel) == ESP_ERR_INVALID_ARG) {
            ESP_LOGE(TAG, "ledc_channel_config() parameter error");
          }
          break;
        default: break;
    }
#endif
//    camera_fb_t *fb = NULL;


    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    //init with high specs to pre-allocate larger buffers
    config.frame_size = FRAMESIZE_UXGA;
//    config.frame_size = 8;
    config.jpeg_quality = 10;
    config.fb_count = 2;
//    printf("file:%s, line:%d, in app_camera_main, time = %ld\r\n", __FILE__, __LINE__, time(NULL));

    // camera init
/*    printf("file:%s, line:%d, LEDC_CHANNEL_0 = %d, LEDC_TIMER_0 = %d, PIXFORMAT_JPEG = %d, FRAMESIZE_UXGA = %d\r\n", 
        __FILE__, __LINE__, LEDC_CHANNEL_0, LEDC_TIMER_0, PIXFORMAT_JPEG, FRAMESIZE_UXGA);*/

    int retry;
    esp_err_t err;
    led_gpio_init();
    for (retry = 0; retry<3; retry++) {
        err = esp_camera_init(&config);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Camera init failed with error 0x%x, retry", err);
            vTaskDelay(300 / portTICK_PERIOD_MS);
        } else {
            gpio_set_level(15, 1);
            ESP_LOGI(TAG, "Camera init succeed");
            break;
        }
    }
    if (err != ESP_OK)
    {
        for(int i=0;i<10;i++) {
            ESP_LOGE(TAG, "Camera init failed with error 0x%x, return", err);
        }
        upgrade_block();
        printf("sleep_______\n");
        esp_deep_sleep_start();
        return;
    }

//    printf("file:%s, line:%d, begin esp_camera_sensor_get, time = %ld\r\n", __FILE__, __LINE__, time(NULL));
    sensor_t * s = esp_camera_sensor_get();
    //initial sensors are flipped vertically and colors are a bit saturated
    if (s->id.PID == OV3660_PID)
    {
        s->set_vflip(s, 1);//flip it back
        s->set_brightness(s, 1);//up the blightness just a bit
        s->set_saturation(s, -2);//lower the saturation
    }
    
    //drop down frame size for higher initial frame rate
//    printf("file:%s, line:%d, begin set_framesize\r\n", __FILE__, __LINE__);
    /* 设置像素 */
    s->set_framesize(s, FRAMESIZE_QVGA + 3);

//    printf("file:%s, line:%d, begin get_camera_data_task, time = %ld\r\n", __FILE__, __LINE__, time(NULL));
    /* add by liuwenjian 2020-3-4 begin */
    /* 创建任务摄像头开始录制 */
    xTaskCreate(&get_camera_data_task, "get_camera_data_task", 8192, NULL, 4, NULL);
//    printf("file:%s, line:%d, begin recv_data_task\r\n", __FILE__, __LINE__);
//    xTaskCreate(&recv_data_task, "recv_data_task", 8192, NULL, 5, NULL);
    /* 创建任务发送图片 */
    i2c_app_init();
    adc_app_main_init();
    xTaskCreate(&send_queue_pic_task, "send_queue_pic_task", 8192, NULL, 5, NULL);
    /* add by liuwenjian 2020-3-4 end */

//    stream_send();
//    fb = esp_camera_fb_get();
//    printf("file:%s, line:%d, fb = %p\r\n", __FILE__, __LINE__, fb);
}

