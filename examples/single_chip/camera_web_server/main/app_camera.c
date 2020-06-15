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
#include "sd_card_example_main.h"
#include "spiffs_example_main.h"

static const char *TAG = "app_camera";

bool g_camera_power = true;

portMUX_TYPE is_connect_server_spinlock = portMUX_INITIALIZER_UNLOCKED;
bool is_connect_server = false;

TaskHandle_t get_camera_data_task_handle;
TaskHandle_t send_queue_pic_task_handle;

/* there is something to do with video queue */
xSemaphoreHandle vq_save_trigger;
xSemaphoreHandle vq_upload_trigger;
xSemaphoreHandle start_capture_trigger;
xSemaphoreHandle save_pic_completed;
pic_queue *upload_pic_pointer;

/* mutex of video-queue */
xSemaphoreHandle vq_mtx;

uint32_t send_video_start_time;


static int send_jpeg(pic_queue *send_pic, time_t argtime)
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
    pic_queue *sd_pic = NULL;

//    vTaskDelay(10000);

    get_socket_status(&socket_fd);
//    printf("file:%s, line:%d, socket_fd = %d\r\n", __FILE__, __LINE__, socket_fd);
    
    if (socket_fd < 0)
    {
        sock_ret = create_tcp_client();
        ESP_LOGE(TAG, "file:%s, line:%d, sock_ret = %d\r\n", __FILE__, __LINE__, sock_ret);
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
                if (send_pic->pic_len - send_len > APP_PACKET_DATA_LEN)
                {
                    my_MD5Update(&md5, (send_pic->pic_info + send_len), APP_PACKET_DATA_LEN);
                    jpeg_data.buf = send_pic->pic_info + send_len;
                    //v = send_pic->video;
                    //jpeg_data.create_time = send_pic->cur_time;
                    jpeg_data.create_time = argtime;
                    jpeg_data.num = send_pic->sn;
                    jpeg_data.send_count = send_len;
                    packet_send_data.data = (void *)(&jpeg_data);
                    packet_send_data.send_len = APP_PACKET_DATA_LEN + 14;
                    packet_send_data.status = HAVE_DATA;
                    packet_send_data.type = SEND_FACE_PIC_CODE;
                    
    //                printf("file:%s, line:%d, begin send_data\r\n", __FILE__, __LINE__);
                    printf("%d 发: sn = %d\n", xTaskGetTickCount(), jpeg_data.num);
                    ret = send_data(packet_send_data, true);
                    if (CAMERA_OK != ret)
                    {
                        printf("file:%s, line:%d, send_data failed\r\n", __FILE__, __LINE__);
                        close_socket();
                        if(sd_pic) {
                            free(sd_pic);
                        }
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
                    send_len += APP_PACKET_DATA_LEN;
                }
                else
                {
                    my_MD5Update(&md5, (send_pic->pic_info + send_len), (send_pic->pic_len - send_len));
                    jpeg_data.buf = send_pic->pic_info + send_len;
                    //v = send_pic->video;
                    //jpeg_data.create_time = send_pic->cur_time;
                    jpeg_data.create_time = argtime;
                    jpeg_data.num = send_pic->sn;
                    jpeg_data.send_count = send_len;
                    packet_send_data.data = (void *)(&jpeg_data);
                    packet_send_data.send_len = send_pic->pic_len - send_len + 14;
                    packet_send_data.status = HAVE_DATA;
                    packet_send_data.type = SEND_FACE_PIC_CODE;
                    
    //                printf("file:%s, line:%d, begin send_data\r\n", __FILE__, __LINE__);
                    printf("%d 发: sn = %d size=%d\n", xTaskGetTickCount(), jpeg_data.num, send_pic->pic_len);
                    ret = send_data(packet_send_data, true);
                    if (CAMERA_OK != ret)
                    {
                        printf("file:%s, line:%d, send_data failed\r\n", __FILE__, __LINE__);
                        close_socket();
                        if(sd_pic) {
                            free(sd_pic);
                        }
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
            //v = send_pic->video;
            //jpeg_data.create_time = send_pic->cur_time;
            jpeg_data.create_time = argtime;
            jpeg_data.num = send_pic->sn;
            jpeg_data.send_count = send_len;
            packet_send_data.data = (void *)(&jpeg_data);
            packet_send_data.send_len = MD5_STR_LEN + 14;
            packet_send_data.status = NOT_HAVE_DATA;
            packet_send_data.type = SEND_FACE_PIC_CODE;

    //        printf("file:%s, line:%d, md5_str = %s\r\n", __FILE__, __LINE__, md5_str);
            printf("%d 发: md5 = %s\n", xTaskGetTickCount(), md5_str);
            ret = send_data(packet_send_data, true);
            if (CAMERA_OK != ret)
            {
                printf("file:%s, line:%d, send_data failed\r\n", __FILE__, __LINE__);
                close_socket();
                if(sd_pic) {
                    free(sd_pic);
                }
                return ret;
            }
        }
        else
        {
            rd_sdcard_fp_close();
            jpeg_data.buf = NULL;
            jpeg_data.create_time = 0;
            jpeg_data.num = 0xffff;
            jpeg_data.send_count = 0;
            packet_send_data.data = (void *)(&jpeg_data);
            packet_send_data.send_len = 14;
            packet_send_data.status = NOT_HAVE_DATA;
            packet_send_data.type = SEND_FACE_PIC_CODE;
            
//                printf("file:%s, line:%d, begin send_data\r\n", __FILE__, __LINE__);
            printf("%d send NULL: sn = %d\n", xTaskGetTickCount(), jpeg_data.num);
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

    if(sd_pic) {
        free(sd_pic);
    }
    return CAMERA_OK;
}

void send_heartbeat_packet()
{
    int ret;
    int socket_fd;
    int sock_ret = -1;
    send_heartbeat_info heartbeat_data;
    packet_info packet_send_data;

    xSemaphoreTake(vpercent_ready, portMAX_DELAY);
    heartbeat_data.battery = vPercent;
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
        portENTER_CRITICAL(&is_connect_server_spinlock);
        is_connect_server = true;
        portEXIT_CRITICAL(&is_connect_server_spinlock);
        log_enum(LOG_CONNECT_SERVER);
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

int cam_power_down(void) {
    if(g_camera_power == false) {
        /* already closed */
        return 0;
    }
    for(int i=0; i<3; i++) {
        printf("send cam_power_down request...\n");
        uart_write_bytes(ECHO_UART_NUM, CAMERA_POWER_DOWN_REQ, strlen(CAMERA_POWER_DOWN_REQ));
        for(int wait = 0; wait<20; wait++) {
            if(g_camera_power == false) {
                //printf("camera power down OKAY\n");
                return 0;
            } else {
                vTaskDelay(50 / portTICK_PERIOD_MS);
            }
        }
    }
    return -1;
}

void camera_start_capture(void) {
    printf("++++++++++++++++ (%s)\n", __func__);
    xSemaphoreGive(start_capture_trigger);
}

void camera_finish_capture(void) {
    printf("++++++++++++++++ (%s)\n", __func__);
    lock_vq();
    vq_tail->complete = true;
    /* 伪图片，代表视频结束 */
    pic_in_queue(vq_tail, 0, NULL);
    if(NULL == upload_pic_pointer) {
        upload_pic_pointer = vq_tail->tail_pic;
    }
    if(NULL == save_pic_pointer) {
        save_pic_pointer = vq_tail->tail_pic;
    }
    unlock_vq();
    xSemaphoreGive(vq_upload_trigger);
    xSemaphoreGive(vq_save_trigger);
}

void camera_drop_capture(void) {
    //printf("++++++++++++++++ (%s)\n", __func__);
    lock_vq();
    drop_video(vq_tail);
    unlock_vq();
}

int camera_capture_one_video(void) {
    camera_fb_t *fb = NULL;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;

    ESP_LOGI(TAG, "<---------CAPTURE VIDEO--------->");
    portENTER_CRITICAL(&cam_ctrl_spinlock);
    cam_ctrl.start_ticks = xTaskGetTickCount();
    portEXIT_CRITICAL(&cam_ctrl_spinlock);
    new_video();

    for(;;) {
        fb = esp_camera_fb_get();

        if (!fb)
        {
            ESP_LOGE(TAG, "Camera capture failed");
            //res = ESP_FAIL;
        }
        else
        {
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;

            /* 结束拍摄 */
            lock_vq();
            portENTER_CRITICAL(&cam_ctrl_spinlock);
            bool b_idle = (cam_ctrl.status == CAM_IDLE);
            portEXIT_CRITICAL(&cam_ctrl_spinlock);
            if(b_idle || NULL==vq_tail || (vq_tail && vq_tail->complete)) {
                /* free framebuffer */
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
                unlock_vq();
                return ESP_OK;
            }
            /* 图片入队列 */
            pic_in_queue(vq_tail, fb->len, fb->buf);

            /* 如果上传或者保存的资源没了，赋新值 */
            if(NULL == upload_pic_pointer) {
                upload_pic_pointer = vq_tail->tail_pic;
            }
            if(NULL == save_pic_pointer) {
                save_pic_pointer = vq_tail->tail_pic;
            }
            unlock_vq();
            
            xSemaphoreGive(vq_upload_trigger);
            xSemaphoreGive(vq_save_trigger);
#if 0
            cur_time = xTaskGetTickCount();
            if ((cur_time - old_time > (CAMERA_VIDEO_TIME*configTICK_RATE_HZ) ) && (false == g_camera_over))
            {
                /* 超时结束录制 */
                printf("file:%s, line:%d, camera over, cur_time = %d\r\n", __FILE__, __LINE__, cur_time);
                g_camera_over = true;
                log_enum(LOG_CAMERA_OVER);
                break;
            }
#endif
        }

        /* free framebuffer */
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
}

static void camera_capture_task(void *arg)
{
    char timeStr[32];
    uint8_t reg[8];
    time_t timeValue;
    struct tm tmValue, rtcValue;
    esp_err_t res = ESP_OK;
    uint32_t old_time, cur_time;


    /* 录像计时 */
    cur_time = old_time = xTaskGetTickCount();
    for(;;)
    {
        /* wait */
        xSemaphoreTake(start_capture_trigger, portMAX_DELAY);
        while(true) {
            lock_vq();
            bool bv = (vq_tail && false == vq_tail->is_in_sdcard);
            unlock_vq();

            if (bv) {
                /* 内存里还有数据 */
                xSemaphoreTake(save_pic_completed, portMAX_DELAY);
            } else {
                break;
            }
        }
        camera_capture_one_video();
    }

}

#if 0
/* 获取摄像头图形并发送 */
static void get_camera_data_task(void *pvParameter)
{
    camera_capture_task();

    /* finish capture */
    //cam_power_down();
    ESP_LOGI(TAG, "delete thread get_camera_data_task!");
    vTaskDelete(NULL);
}
#endif

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

/* 队列读取图片并发送出去 */
static void send_queue_pic_task(void *pvParameter)
{
    int ret;
    extern int max_sleep_uptime;
    extern unsigned char is_connect;
    uint8_t reg[8];
    pic_queue *send_pic;
    printf("file:%s, line:%d, begin esp_wait_sntp_sync\r\n", __FILE__, __LINE__);

    /* 用于时间同步 */
    //printf("skip esp_wait_sntp_sync.....\n");
    while( !is_connect ) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    log_enum(LOG_CONNECT_WIFI);
    g_init_data.start_time = time(NULL);

    /* 用于发送心跳包 */
    send_heartbeat_packet();

    for(;;) {
        xSemaphoreTake(vq_upload_trigger, portMAX_DELAY);
        for(;;) {
            lock_vq();
            if(upload_pic_pointer) {
                if(upload_pic_pointer->pic_len) {
                    /*------------------判断mem/sdcard-------------------*/
                    video_queue *v = upload_pic_pointer->video;
                    send_pic = (pic_queue *)malloc(PIC_DATA_OFFSET + upload_pic_pointer->pic_len);
                    memcpy(send_pic, upload_pic_pointer, sizeof(pic_queue));
                    if(v->is_in_sdcard) {
                        read_one_pic_from_sdcard(send_pic);
                    } else {
                        memcpy(send_pic->pic_info, upload_pic_pointer->pic_info, upload_pic_pointer->pic_len);
                    }
                    time_t argtime = v->time;

                    /* 发送时间控制 */
                    if(upload_pic_pointer == v->head_pic) {
                        log_printf("开始发送");
                        printf("+++++++++ 开始发送一个视频!!!\n");
                        portENTER_CRITICAL(&time_var_spinlock);
                        send_video_start_time = xTaskGetTickCount();
                        portEXIT_CRITICAL(&time_var_spinlock);
                    }
                    
                    unlock_vq();
                    send_jpeg(send_pic, argtime);
                    lock_vq();
                    free(send_pic);
                } else {
                    /* last pseudo pic */
                    unlock_vq();
                    send_jpeg(NULL, 0);
                    lock_vq();
                    log_printf("发送结束");
                    printf("+++ 一个视频发送结束\n");
                    portENTER_CRITICAL(&time_var_spinlock);
                    send_video_start_time = 0;
                    portEXIT_CRITICAL(&time_var_spinlock);
                    drop_video(upload_pic_pointer->video);
                }
            } else {
                /* ? */
                unlock_vq();
                break;
            }

            /* 尝试下一张图片 */
            video_queue *video = NULL;
            if( upload_pic_pointer ) {
                video = upload_pic_pointer->video;
                if(upload_pic_pointer->next) {
                    /* next picture */
                    upload_pic_pointer = upload_pic_pointer->next;
                } else if(video && video->next && video->next->head_pic) {
                    /* new video */
                    upload_pic_pointer = video->next->head_pic;
                } else {
                    /* nothing else */
                    upload_pic_pointer = NULL;
                    unlock_vq();
                    break;
                }
            } else {
                /* nothing else */
                //upload_pic_pointer = NULL;
                unlock_vq();
                break;
            }

            unlock_vq();
        }
    }

#if  0
    /* 老代码，执行不到这里 */
    ESP_LOGE(TAG, "老代码 %s", __func__);
    while (false)
    {
        if ((NULL == g_pic_queue_head)&&(true == g_camera_over)) 
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
            //sdcard_test();
            //flash_led();
            log_enum(LOG_SEND_OVER);

            portENTER_CRITICAL(&g_pic_send_over_spinlock);
            g_pic_send_over = TRUE;
            portEXIT_CRITICAL(&g_pic_send_over_spinlock);
            portENTER_CRITICAL(&time_var_spinlock);
            bool b = max_sleep_uptime == DEF_MAX_SLEEP_TIME;
            portEXIT_CRITICAL(&time_var_spinlock);
            if( b ) {
                sdcard_log_write();
                printf("=-> send shutdown request\n");
                uart_write_bytes(ECHO_UART_NUM, CORE_SHUT_DOWN_REQ, strlen(CORE_SHUT_DOWN_REQ)+1);
            }
            break;
        }
        else if (NULL != g_pic_queue_head)
        {
            if ((NULL != g_pic_queue_head->next) || (true == g_camera_over))
            {
                pic_out_queue();
            }
        }
        else
        {
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
    }
#endif

    vTaskDelete(NULL);
}

int app_camera_main ()
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
    config.jpeg_quality = 30;
    config.fb_count = 2;
//    printf("file:%s, line:%d, in app_camera_main, time = %ld\r\n", __FILE__, __LINE__, time(NULL));

    // camera init
/*    printf("file:%s, line:%d, LEDC_CHANNEL_0 = %d, LEDC_TIMER_0 = %d, PIXFORMAT_JPEG = %d, FRAMESIZE_UXGA = %d\r\n", 
        __FILE__, __LINE__, LEDC_CHANNEL_0, LEDC_TIMER_0, PIXFORMAT_JPEG, FRAMESIZE_UXGA);*/

    int retry;
    esp_err_t err;
    //led_gpio_init();
    for (retry = 0; retry<3; retry++) {
        err = esp_camera_init(&config);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Camera init failed with error 0x%x, retry", err);
            vTaskDelay(300 / portTICK_PERIOD_MS);
        } else {
            //gpio_set_level(15, 1);
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
        return ESP_FAIL;
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

    /* sem create */
    vq_save_trigger = xSemaphoreCreateCounting(1, 0);
    if(NULL == vq_save_trigger) {
        ESP_LOGE(TAG, "vq_save_trigger");
        return ESP_FAIL;
    }
    vq_upload_trigger = xSemaphoreCreateCounting(1, 0);
    if(NULL == vq_upload_trigger) {
        ESP_LOGE(TAG, "vq_upload_trigger");
        return ESP_FAIL;
    }
    /* 上电拍摄，后续需要触发 */
    start_capture_trigger = xSemaphoreCreateCounting(1, 1);
    if(NULL == start_capture_trigger) {
        ESP_LOGE(TAG, "start_capture_trigger");
        return ESP_FAIL;
    }
    save_pic_completed = xSemaphoreCreateCounting(1, 0);
    if(NULL == save_pic_completed) {
        ESP_LOGE(TAG, "save_pic_completed");
        return ESP_FAIL;
    }
    vq_mtx = xSemaphoreCreateMutex();
    if(NULL == vq_mtx) {
        ESP_LOGE(TAG, "vq_mtx");
        return ESP_FAIL;
    }
    /* 创建任务摄像头开始录制 */
    xTaskCreate(&camera_capture_task, "camera_capture_task", 4096, NULL, 4, &get_camera_data_task_handle);
//    printf("file:%s, line:%d, begin recv_data_task\r\n", __FILE__, __LINE__);
//    xTaskCreate(&recv_data_task, "recv_data_task", 8192, NULL, 5, NULL);
    /* 创建任务发送图片 */
    xTaskCreate(&send_queue_pic_task, "send_queue_pic_task", 3072, NULL, 5, &send_queue_pic_task_handle);
    /* add by liuwenjian 2020-3-4 end */

//    stream_send();
//    fb = esp_camera_fb_get();
//    printf("file:%s, line:%d, fb = %p\r\n", __FILE__, __LINE__, fb);
    return ESP_OK;
}

