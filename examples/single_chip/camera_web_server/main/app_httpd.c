// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include "app_httpd.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "driver/ledc.h"
//#include "camera_index.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "esp_spiffs.h"
#include "tcp_bsp.h"
#include "md5.h"
#include "driver/uart.h"
#include "common.h"

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#define TAG ""
#else
#include "esp_log.h"
static const char *TAG = "camera_httpd";
#endif

#if CONFIG_ESP_FACE_DETECT_ENABLED

#if CONFIG_ESP_FACE_DETECT_MTMN
#include "fd_forward.h"
#endif

#if CONFIG_ESP_FACE_DETECT_LSSH
#include "lssh_forward.h"
#endif

#if CONFIG_ESP_FACE_RECOGNITION_ENABLED
#include "fr_forward.h"

#define ENROLL_CONFIRM_TIMES 5
#define FACE_ID_SAVE_NUMBER 7
#endif

#define FACE_COLOR_WHITE 0x00FFFFFF
#define FACE_COLOR_BLACK 0x00000000
#define FACE_COLOR_RED 0x000000FF
#define FACE_COLOR_GREEN 0x0000FF00
#define FACE_COLOR_BLUE 0x00FF0000
#define FACE_COLOR_YELLOW (FACE_COLOR_RED | FACE_COLOR_GREEN)
#define FACE_COLOR_CYAN (FACE_COLOR_BLUE | FACE_COLOR_GREEN)
#define FACE_COLOR_PURPLE (FACE_COLOR_BLUE | FACE_COLOR_RED)
#endif

#ifdef CONFIG_LED_ILLUMINATOR_ENABLED
int led_duty = 0;
bool isStreaming = false;
#ifdef CONFIG_LED_LEDC_LOW_SPEED_MODE
#define CONFIG_LED_LEDC_SPEED_MODE LEDC_LOW_SPEED_MODE
#else
#define CONFIG_LED_LEDC_SPEED_MODE LEDC_HIGH_SPEED_MODE
#endif
#endif

typedef struct
{
    httpd_req_t *req;
    size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

#if CONFIG_ESP_FACE_DETECT_ENABLED

static int8_t detection_enabled = 0;

#if CONFIG_ESP_FACE_DETECT_MTMN
static mtmn_config_t mtmn_config = {0};
#endif

#if CONFIG_ESP_FACE_DETECT_LSSH
static lssh_config_t lssh_config;
static int min_face=80;
#endif

#if CONFIG_ESP_FACE_RECOGNITION_ENABLED
static int8_t recognition_enabled = 0;
static int8_t is_enrolling = 0;
static face_id_list id_list = {0};
#endif

#endif

typedef struct
{
    size_t size;  //number of values used for filtering
    size_t index; //current value index
    size_t count; //value count
    int sum;
    int *values; //array to be filled with values
} ra_filter_t;

static ra_filter_t ra_filter;

static ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size)
{
    memset(filter, 0, sizeof(ra_filter_t));

    printf("file:%s, line:%d, in ra_filter_init\r\n", __FILE__, __LINE__);
    filter->values = (int *)malloc(sample_size * sizeof(int));
    if (!filter->values)
    {
        return NULL;
    }
    memset(filter->values, 0, sample_size * sizeof(int));

    filter->size = sample_size;
    return filter;
}

static int ra_filter_run(ra_filter_t *filter, int value)
{
    printf("file:%s, line:%d, in ra_filter_run\r\n", __FILE__, __LINE__);
    if (!filter->values)
    {
        return value;
    }
    filter->sum -= filter->values[filter->index];
    filter->values[filter->index] = value;
    filter->sum += filter->values[filter->index];
    filter->index++;
    filter->index = filter->index % filter->size;
    if (filter->count < filter->size)
    {
        filter->count++;
    }
    return filter->sum / filter->count;
}

#if CONFIG_ESP_FACE_DETECT_ENABLED
#if CONFIG_ESP_FACE_RECOGNITION_ENABLED
static void rgb_print(dl_matrix3du_t *image_matrix, uint32_t color, const char *str)
{
    fb_data_t fb;
    fb.width = image_matrix->w;
    fb.height = image_matrix->h;
    fb.data = image_matrix->item;
    fb.bytes_per_pixel = 3;
    fb.format = FB_BGR888;
    fb_gfx_print(&fb, (fb.width - (strlen(str) * 14)) / 2, 10, color, str);
}

static int rgb_printf(dl_matrix3du_t *image_matrix, uint32_t color, const char *format, ...)
{
    char loc_buf[64];
    char *temp = loc_buf;
    int len;
    va_list arg;
    va_list copy;
    va_start(arg, format);
    va_copy(copy, arg);
    len = vsnprintf(loc_buf, sizeof(loc_buf), format, arg);
    va_end(copy);
    if (len >= sizeof(loc_buf))
    {
        temp = (char *)malloc(len + 1);
        if (temp == NULL)
        {
            return 0;
        }
    }
    vsnprintf(temp, len + 1, format, arg);
    va_end(arg);
    rgb_print(image_matrix, color, temp);
    if (len > 64)
    {
        free(temp);
    }
    return len;
}
#endif
static void draw_face_boxes(dl_matrix3du_t *image_matrix, box_array_t *boxes, int face_id)
{
    int x, y, w, h, i;
    uint32_t color = FACE_COLOR_YELLOW;
    printf("file:%s, line:%d, in draw_face_boxes\r\n", __FILE__, __LINE__);
    if (face_id < 0)
    {
        color = FACE_COLOR_RED;
    }
    else if (face_id > 0)
    {
        color = FACE_COLOR_GREEN;
    }
    fb_data_t fb;
    fb.width = image_matrix->w;
    fb.height = image_matrix->h;
    fb.data = image_matrix->item;
    fb.bytes_per_pixel = 3;
    fb.format = FB_BGR888;
    for (i = 0; i < boxes->len; i++)
    {
        // rectangle box
        x = (int)boxes->box[i].box_p[0];
        y = (int)boxes->box[i].box_p[1];
        w = (int)boxes->box[i].box_p[2] - x + 1;
        h = (int)boxes->box[i].box_p[3] - y + 1;
        fb_gfx_drawFastHLine(&fb, x, y, w, color);
        fb_gfx_drawFastHLine(&fb, x, y + h - 1, w, color);
        fb_gfx_drawFastVLine(&fb, x, y, h, color);
        fb_gfx_drawFastVLine(&fb, x + w - 1, y, h, color);
#if 0
        // landmark
        int x0, y0, j;
        for (j = 0; j < 10; j+=2) {
            x0 = (int)boxes->landmark[i].landmark_p[j];
            y0 = (int)boxes->landmark[i].landmark_p[j+1];
            fb_gfx_fillRect(&fb, x0, y0, 3, 3, color);
        }
#endif
    }
}

#if CONFIG_ESP_FACE_RECOGNITION_ENABLED
static int run_face_recognition(dl_matrix3du_t *image_matrix, box_array_t *net_boxes)
{
    dl_matrix3du_t *aligned_face = NULL;
    int matched_id = 0;

    printf("file:%s, line:%d, in run_face_recognition\r\n", __FILE__, __LINE__);
    aligned_face = dl_matrix3du_alloc(1, FACE_WIDTH, FACE_HEIGHT, 3);
    if (!aligned_face)
    {
        ESP_LOGE(TAG, "Could not allocate face recognition buffer");
        return matched_id;
    }
    if (align_face(net_boxes, image_matrix, aligned_face) == ESP_OK)
    {
        if (is_enrolling == 1)
        {
            int8_t left_sample_face = enroll_face(&id_list, aligned_face);

            if (left_sample_face == (ENROLL_CONFIRM_TIMES - 1))
            {
                ESP_LOGD(TAG, "Enrolling Face ID: %d", id_list.tail);
            }
            ESP_LOGD(TAG, "Enrolling Face ID: %d sample %d", id_list.tail, ENROLL_CONFIRM_TIMES - left_sample_face);
            rgb_printf(image_matrix, FACE_COLOR_CYAN, "ID[%u] Sample[%u]", id_list.tail, ENROLL_CONFIRM_TIMES - left_sample_face);
            if (left_sample_face == 0)
            {
                is_enrolling = 0;
                ESP_LOGD(TAG, "Enrolled Face ID: %d", id_list.tail);
            }
        }
        else
        {
            matched_id = recognize_face(&id_list, aligned_face);
            if (matched_id >= 0)
            {
                ESP_LOGW(TAG, "Match Face ID: %u", matched_id);
                rgb_printf(image_matrix, FACE_COLOR_GREEN, "Hello Subject %u", matched_id);
            }
            else
            {
                ESP_LOGW(TAG, "No Match Found");
                rgb_print(image_matrix, FACE_COLOR_RED, "Intruder Alert!");
                matched_id = -1;
            }
        }
    }
    else
    {
        ESP_LOGW(TAG, "Face Not Aligned");
        //rgb_print(image_matrix, FACE_COLOR_YELLOW, "Human Detected");
    }

    dl_matrix3du_free(aligned_face);
    return matched_id;
}
#endif
#endif

#ifdef CONFIG_LED_ILLUMINATOR_ENABLED
void enable_led(bool en)
{ // Turn LED On or Off
    int duty = en ? led_duty : 0;
    printf("file:%s, line:%d, in enable_led\r\n", __FILE__, __LINE__);
    if (en && isStreaming && (led_duty > CONFIG_LED_MAX_INTENSITY))
    {
        duty = CONFIG_LED_MAX_INTENSITY;
    }
    ledc_set_duty(CONFIG_LED_LEDC_SPEED_MODE, CONFIG_LED_LEDC_CHANNEL, duty);
    ledc_update_duty(CONFIG_LED_LEDC_SPEED_MODE, CONFIG_LED_LEDC_CHANNEL);
    ESP_LOGI(TAG, "Set LED intensity to %d", duty);
}
#endif

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len)
{
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    printf("file:%s, line:%d, in jpg_encode_stream\r\n", __FILE__, __LINE__);
    if (!index)
    {
        j->len = 0;
    }
    if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK)
    {
        return 0;
    }
    j->len += len;
    return len;
}

static esp_err_t capture_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    int64_t fr_start = esp_timer_get_time();

#ifdef CONFIG_LED_ILLUMINATOR_ENABLED
    enable_led(true);
    vTaskDelay(150 / portTICK_PERIOD_MS); // The LED needs to be turned on ~150ms before the call to esp_camera_fb_get()
    fb = esp_camera_fb_get();             // or it won't be visible in the frame. A better way to do this is needed.
    enable_led(false);
#else
    fb = esp_camera_fb_get();
#endif
    printf("file:%s, line:%d, in capture_handler\r\n", __FILE__, __LINE__);

    if (!fb)
    {
        ESP_LOGE(TAG, "Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

#if CONFIG_ESP_FACE_DETECT_ENABLED
    size_t out_len, out_width, out_height;
    uint8_t *out_buf;
    bool s;
    bool detected = false;
    int face_id = 0;
    if (!detection_enabled || fb->width > 400)
    {
#endif
        size_t fb_len = 0;
        if (fb->format == PIXFORMAT_JPEG)
        {
            fb_len = fb->len;
            res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
        }
        else
        {
            jpg_chunking_t jchunk = {req, 0};
            res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
            httpd_resp_send_chunk(req, NULL, 0);
            fb_len = jchunk.len;
        }
        esp_camera_fb_return(fb);
        int64_t fr_end = esp_timer_get_time();
        ESP_LOGI(TAG, "JPG: %uB %ums", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start) / 1000));
        return res;
#if CONFIG_ESP_FACE_DETECT_ENABLED
    }

    dl_matrix3du_t *image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);
    if (!image_matrix)
    {
        esp_camera_fb_return(fb);
        ESP_LOGE(TAG, "dl_matrix3du_alloc failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    out_buf = image_matrix->item;
    out_len = fb->width * fb->height * 3;
    out_width = fb->width;
    out_height = fb->height;

    s = fmt2rgb888(fb->buf, fb->len, fb->format, out_buf);
    esp_camera_fb_return(fb);
    if (!s)
    {
        dl_matrix3du_free(image_matrix);
        ESP_LOGE(TAG, "to rgb888 failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

#if CONFIG_ESP_FACE_DETECT_MTMN
    box_array_t *net_boxes = face_detect(image_matrix, &mtmn_config);
#endif

#if CONFIG_ESP_FACE_DETECT_LSSH
    box_array_t *net_boxes = lssh_detect_object(image_matrix, lssh_config);
#endif

    if (net_boxes)
    {
        detected = true;
#if CONFIG_ESP_FACE_RECOGNITION_ENABLED
        if (recognition_enabled)
        {
            face_id = run_face_recognition(image_matrix, net_boxes);
        }
#endif
        draw_face_boxes(image_matrix, net_boxes, face_id);
        dl_lib_free(net_boxes->score);
        dl_lib_free(net_boxes->box);
        if (net_boxes->landmark != NULL)
            dl_lib_free(net_boxes->landmark);
        dl_lib_free(net_boxes);
    }

    jpg_chunking_t jchunk = {req, 0};
    s = fmt2jpg_cb(out_buf, out_len, out_width, out_height, PIXFORMAT_RGB888, 90, jpg_encode_stream, &jchunk);
    dl_matrix3du_free(image_matrix);
    if (!s)
    {
        ESP_LOGE(TAG, "JPEG compression failed");
        return ESP_FAIL;
    }

    int64_t fr_end = esp_timer_get_time();
    ESP_LOGI(TAG, "FACE: %uB %ums %s%d", (uint32_t)(jchunk.len), (uint32_t)((fr_end - fr_start) / 1000), detected ? "DETECTED " : "", face_id);
    return res;
#endif
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[64];
#if CONFIG_ESP_FACE_DETECT_ENABLED
    dl_matrix3du_t *image_matrix = NULL;
    bool detected = false;
    int face_id = 0;
    int64_t fr_start = 0;
    int64_t fr_ready = 0;
    int64_t fr_face = 0;
    int64_t fr_recognize = 0;
    int64_t fr_encode = 0;
#endif
    static int64_t last_frame = 0;

//    int64_t test_frame = 0;
    if (!last_frame)
    {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK)
    {
        return res;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

#ifdef CONFIG_LED_ILLUMINATOR_ENABLED
    enable_led(true);
    isStreaming = true;
#endif

    printf("file:%s, line:%d, begin while\r\n", __FILE__, __LINE__);
    while (true)
    {
#if CONFIG_ESP_FACE_DETECT_ENABLED
        detected = false;
        face_id = 0;
#endif

        fb = esp_camera_fb_get();
        if (!fb)
        {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
        }
        else
        {
#if CONFIG_ESP_FACE_DETECT_ENABLED
            fr_start = esp_timer_get_time();
            fr_ready = fr_start;
            fr_face = fr_start;
            fr_encode = fr_start;
            fr_recognize = fr_start;
            if (!detection_enabled || fb->width > 400)
            {
#endif
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
                }
#if CONFIG_ESP_FACE_DETECT_ENABLED
            }
            else
            {

                image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);

                if (!image_matrix)
                {
                    ESP_LOGE(TAG, "dl_matrix3du_alloc failed");
                    res = ESP_FAIL;
                }
                else
                {
                    if (!fmt2rgb888(fb->buf, fb->len, fb->format, image_matrix->item))
                    {
                        ESP_LOGE(TAG, "fmt2rgb888 failed");
                        res = ESP_FAIL;
                    }
                    else
                    {
#if CONFIG_ESP_FACE_DETECT_LSSH
                        // lssh_update_config(&lssh_config, min_face, image_matrix->h, image_matrix->w);
#endif
                        fr_ready = esp_timer_get_time();
                        box_array_t *net_boxes = NULL;
                        if (detection_enabled)
                        {
#if CONFIG_ESP_FACE_DETECT_MTMN
                            net_boxes = face_detect(image_matrix, &mtmn_config);
#endif

#if CONFIG_ESP_FACE_DETECT_LSSH
                            net_boxes = lssh_detect_object(image_matrix, lssh_config);
#endif
                        }
                        fr_face = esp_timer_get_time();
                        fr_recognize = fr_face;
                        if (net_boxes || fb->format != PIXFORMAT_JPEG)
                        {
                            if (net_boxes)
                            {
                                detected = true;
#if CONFIG_ESP_FACE_RECOGNITION_ENABLED
                                if (recognition_enabled)
                                {
                                    face_id = run_face_recognition(image_matrix, net_boxes);
                                }
                                fr_recognize = esp_timer_get_time();
#endif
                                draw_face_boxes(image_matrix, net_boxes, face_id);
                                dl_lib_free(net_boxes->score);
                                dl_lib_free(net_boxes->box);
                                if (net_boxes->landmark != NULL)
                                    dl_lib_free(net_boxes->landmark);
                                dl_lib_free(net_boxes);
                            }
                            if (!fmt2jpg(image_matrix->item, fb->width * fb->height * 3, fb->width, fb->height, PIXFORMAT_RGB888, 90, &_jpg_buf, &_jpg_buf_len))
                            {
                                ESP_LOGE(TAG, "fmt2jpg failed");
                            }
                            esp_camera_fb_return(fb);
                            fb = NULL;
                        }
                        else
                        {
                            _jpg_buf = fb->buf;
                            _jpg_buf_len = fb->len;
                        }
                        fr_encode = esp_timer_get_time();
                        
                    }
                    dl_matrix3du_free(image_matrix);
                }
            }
#endif
        }
        
        if (res == ESP_OK)
        {
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
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
        if (res != ESP_OK)
        {
            break;
        }
        int64_t fr_end = esp_timer_get_time();

#if CONFIG_ESP_FACE_DETECT_ENABLED
        int64_t ready_time = (fr_ready - fr_start) / 1000;
        int64_t face_time = (fr_face - fr_ready) / 1000;
        int64_t recognize_time = (fr_recognize - fr_face) / 1000;
        int64_t encode_time = (fr_encode - fr_recognize) / 1000;
        int64_t process_time = (fr_encode - fr_start) / 1000;
#endif

        int64_t frame_time = fr_end - last_frame;
        last_frame = fr_end;
        frame_time /= 1000;
        uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);
        ESP_LOGI(TAG, "MJPG: %uB %ums (%.1ffps), AVG: %ums (%.1ffps)"
#if CONFIG_ESP_FACE_DETECT_ENABLED
                      ", %u+%u+%u+%u=%u %s%d"
#endif
                 ,
                 (uint32_t)(_jpg_buf_len),
                 (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time,
                 avg_frame_time, 1000.0 / avg_frame_time
#if CONFIG_ESP_FACE_DETECT_ENABLED
                 ,
                 (uint32_t)ready_time, (uint32_t)face_time, (uint32_t)recognize_time, (uint32_t)encode_time, (uint32_t)process_time,
                 (detected) ? "DETECTED " : "", face_id
#endif
        );
    }

#ifdef CONFIG_LED_ILLUMINATOR_ENABLED
    isStreaming = false;
    enable_led(false);
#endif

    last_frame = 0;
    return res;
}

/* add by liuwenjian 2020-3-4 begin */
/* �����豸������Ϣ */
static esp_err_t update_config(char *type, char *value)
{
    nvs_handle my_handle;
    esp_err_t err;

    xSemaphoreTake(g_data_mutex, portMAX_DELAY);
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK)
    {
        printf("file:%s, line:%d, nvs_open err = %d\r\n", __FILE__, __LINE__, err);
        goto update_config_fail_2;
    }

    if (strlen(value) <= 0)
    {
        printf("file:%s, line:%d, value = %s\r\n", __FILE__, __LINE__, value);
        goto update_config_fail;
    }
    
    if (0 == memcmp(type, DEVICE_ID_FLAG, strlen(DEVICE_ID_FLAG)))
    {
        memset(g_init_data.config_data.device_id, 0, sizeof(g_init_data.config_data.device_id));
        strcpy(g_init_data.config_data.device_id, value);
    }
    else if (0 == memcmp(type, SERVICE_IP_FLAG, strlen(DEVICE_ID_FLAG)))
    {
        memset(g_init_data.config_data.service_ip_str, 0, sizeof(g_init_data.config_data.service_ip_str));
        strcpy(g_init_data.config_data.service_ip_str, value);
    }
    else if (0 == memcmp(type, SERVICE_PORT_FLAG, strlen(SERVICE_PORT_FLAG)))
    {
        g_init_data.config_data.service_port = (unsigned short)atoi(value);
    }
    else if (0 == memcmp(type, "wifi_ssid", strlen("wifi_ssid")))
    {
        memset(g_init_data.config_data.wifi_ssid, 0, sizeof(g_init_data.config_data.wifi_ssid));
        strcpy(g_init_data.config_data.wifi_ssid, value);
    }
    else if (0 == memcmp(type, "wifi_key", strlen("wifi_key")))
    {
        memset(g_init_data.config_data.wifi_key, 0, sizeof(g_init_data.config_data.wifi_key));
        strcpy(g_init_data.config_data.wifi_key, value);
    }
    else if (0 == memcmp(type, "wifi_ap_key", strlen("wifi_ap_key")))
    {
        memset(g_init_data.config_data.wifi_ap_key, 0, sizeof(g_init_data.config_data.wifi_ap_key));
        strcpy(g_init_data.config_data.wifi_ap_key, value);
    }
    else if (0 == memcmp(type, "wifi_ap_ssid", strlen("wifi_ap_ssid")))
    {
        memset(g_init_data.config_data.wifi_ap_ssid, 0, sizeof(g_init_data.config_data.wifi_ap_ssid));
        strcpy(g_init_data.config_data.wifi_ap_ssid, value);
    }
    else if (0 == memcmp(type, "ir_voltage", strlen("ir_voltage")))
    {
        g_init_data.config_data.ir_voltage = atoi(value);

        char send_str[32];
        sprintf(send_str, "%s%d", SET_IR_VOLTAGE, atoi(value));
        printf("=-> send: %s\n", send_str);
        uart_write_bytes(ECHO_UART_NUM, send_str, strlen(send_str)+1);
    }
    else
    {
        printf("file:%s, line:%d, value = %s\r\n", __FILE__, __LINE__, value);
        goto update_config_fail;
    }

    printf("=-> write settings~~~\n");
    err = nvs_set_blob(my_handle, "device_info", &(g_init_data.config_data), sizeof(config_para));
    if (err != ESP_OK)
    {
        goto update_config_fail;
    }

    // Commit
    err = nvs_commit(my_handle);
    if (err != ESP_OK)
    {
        goto update_config_fail;
    }

    // Close
    nvs_close(my_handle);
    xSemaphoreGive(g_data_mutex);
    return ESP_OK;

update_config_fail:
    nvs_close(my_handle);

update_config_fail_2:
    xSemaphoreGive(g_data_mutex);
    return ESP_FAIL;
}
/* add by liuwenjian 2020-3-4 end */

static esp_err_t cmd_handler(httpd_req_t *req)
{
    char *buf;
    size_t buf_len;
    char variable[64] = {
        0,
    };
    char value[64] = {
        0,
    };

    printf("file:%s, line:%d, in cmd_handler\r\n", __FILE__, __LINE__);
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1)
    {
        buf = (char *)malloc(buf_len);
        if (!buf)
        {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
        {
            if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) == ESP_OK &&
                httpd_query_key_value(buf, "val", value, sizeof(value)) == ESP_OK)
            {
            }
            else
            {
                free(buf);
                httpd_resp_send_404(req);
                return ESP_FAIL;
            }
        }
        else
        {
            free(buf);
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
        free(buf);
    }
    else
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    int val = atoi(value);
    ESP_LOGI(TAG, "%s = %d, value = %s", variable, val, value);
    sensor_t *s = esp_camera_sensor_get();
    int res = 0;

    if (!strcmp(variable, "framesize"))
    {
        if (s->pixformat == PIXFORMAT_JPEG)
        {
            /* �������� */
            res = s->set_framesize(s, (framesize_t)val);
        }
    }
    /* add by liuwenjian 2020-3-4 begin */
    else if (!strcmp(variable, DEVICE_ID_FLAG))
    {
        /* �豸������� */
        printf("file:%s, line:%d, variable = %s, value = %s\r\n", 
            __FILE__, __LINE__, variable, value);
        res = update_config(DEVICE_ID_FLAG, value);
    }
    else if (!strcmp(variable, SERVICE_IP_FLAG))
    {
        /* ������ip ���� */
        printf("file:%s, line:%d, variable = %s, value = %s\r\n", 
            __FILE__, __LINE__, variable, value);
        res = update_config(SERVICE_IP_FLAG, value);
    }
    else if (!strcmp(variable, SERVICE_PORT_FLAG))
    {
        /* �������˿ں����� */
        printf("file:%s, line:%d, variable = %s, value = %s\r\n", 
            __FILE__, __LINE__, variable, value);
        res = update_config(SERVICE_PORT_FLAG, value);
    }
    /* add by liuwenjian 2020-3-4 end */
    else if (!strcmp(variable, "wifi_ssid"))
    {
        /* �豸������� */
        printf("file:%s, line:%d, variable = %s, value = %s\r\n", 
            __FILE__, __LINE__, variable, value);
        res = update_config("wifi_ssid", value);
    }
    else if (!strcmp(variable, "wifi_key"))
    {
        /* �豸������� */
        printf("file:%s, line:%d, variable = %s, value = %s\r\n", 
            __FILE__, __LINE__, variable, value);
        res = update_config("wifi_key", value);
    }
    else if (!strcmp(variable, "wifi_ap_ssid"))
    {
        /* �豸������� */
        printf("file:%s, line:%d, variable = %s, value = %s\r\n", 
            __FILE__, __LINE__, variable, value);
        res = update_config("wifi_ap_ssid", value);
    }
    else if (!strcmp(variable, "wifi_ap_key"))
    {
        /* �豸������� */
        printf("file:%s, line:%d, variable = %s, value = %s\r\n", 
            __FILE__, __LINE__, variable, value);
        res = update_config("wifi_ap_key", value);
    }
    else if (!strcmp(variable, "ir_voltage"))
    {
        /* �豸������� */
        printf("file:%s, line:%d, variable = %s, value = %s\r\n", 
            __FILE__, __LINE__, variable, value);
        res = update_config("ir_voltage", value);
    }
    else if (!strcmp(variable, "quality"))
        res = s->set_quality(s, val);
    else if (!strcmp(variable, "contrast"))
        res = s->set_contrast(s, val);
    else if (!strcmp(variable, "brightness"))
        res = s->set_brightness(s, val);
    else if (!strcmp(variable, "saturation"))
        res = s->set_saturation(s, val);
    else if (!strcmp(variable, "gainceiling"))
        res = s->set_gainceiling(s, (gainceiling_t)val);
    else if (!strcmp(variable, "colorbar"))
        res = s->set_colorbar(s, val);
    else if (!strcmp(variable, "awb"))
        res = s->set_whitebal(s, val);
    else if (!strcmp(variable, "agc"))
        res = s->set_gain_ctrl(s, val);
    else if (!strcmp(variable, "aec"))
        res = s->set_exposure_ctrl(s, val);
    else if (!strcmp(variable, "hmirror"))
        res = s->set_hmirror(s, val);
    else if (!strcmp(variable, "vflip"))
        res = s->set_vflip(s, val);
    else if (!strcmp(variable, "awb_gain"))
        res = s->set_awb_gain(s, val);
    else if (!strcmp(variable, "agc_gain"))
        res = s->set_agc_gain(s, val);
    else if (!strcmp(variable, "aec_value"))
        res = s->set_aec_value(s, val);
    else if (!strcmp(variable, "aec2"))
        res = s->set_aec2(s, val);
    else if (!strcmp(variable, "dcw"))
        res = s->set_dcw(s, val);
    else if (!strcmp(variable, "bpc"))
        res = s->set_bpc(s, val);
    else if (!strcmp(variable, "wpc"))
        res = s->set_wpc(s, val);
    else if (!strcmp(variable, "raw_gma"))
        res = s->set_raw_gma(s, val);
    else if (!strcmp(variable, "lenc"))
        res = s->set_lenc(s, val);
    else if (!strcmp(variable, "special_effect"))
        res = s->set_special_effect(s, val);
    else if (!strcmp(variable, "wb_mode"))
        res = s->set_wb_mode(s, val);
    else if (!strcmp(variable, "ae_level"))
        res = s->set_ae_level(s, val);
#ifdef CONFIG_LED_ILLUMINATOR_ENABLED
    else if (!strcmp(variable, "led_intensity"))
    {
        led_duty = val;
        if (isStreaming)
            enable_led(true);
    }
#endif

#if CONFIG_ESP_FACE_DETECT_ENABLED
    else if (!strcmp(variable, "face_detect"))
    {
        detection_enabled = val;
#if CONFIG_ESP_FACE_RECOGNITION_ENABLED
        if (!detection_enabled)
        {
            recognition_enabled = 0;
        }
#endif
    }
#if CONFIG_ESP_FACE_RECOGNITION_ENABLED
    else if (!strcmp(variable, "face_enroll"))
        is_enrolling = val;
    else if (!strcmp(variable, "face_recognize"))
    {
        recognition_enabled = val;
        if (recognition_enabled)
        {
            detection_enabled = val;
        }
    }
#endif
#endif
    else
    {
        res = -1;
    }

    if (res)
    {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    static char json_response[1024];
    nvs_handle my_handle;
    config_para config_data;
    esp_err_t err;

    // Open
    xSemaphoreTake(g_data_mutex, portMAX_DELAY);
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK)
    {
        printf("file:%s, line:%d, nvs_open failed!\r\n", __FILE__, __LINE__);
    }
    else
    {
        // Read run time blob
        size_t required_size = sizeof(config_data);  // value will default to 0, if not set yet in NVS
        // obtain required memory space to store blob being read from NVS
        err = nvs_get_blob(my_handle, "device_info", &config_data, &required_size);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        {
            printf("file:%s, line:%d, nvs_get_blob, err = %d\r\n", __FILE__, __LINE__, err);
        }
        else
        {
            printf("file:%s, line:%d, device_id = %s, ip = %s, port = %d\r\n", 
                __FILE__, __LINE__, config_data.device_id, config_data.service_ip_str, config_data.service_port);
        }
        
        nvs_close(my_handle);
    }
    xSemaphoreGive(g_data_mutex);
    /*-------------------------------------------------*/

    sensor_t *s = esp_camera_sensor_get();
    char *p = json_response;
    *p++ = '{';

    p += sprintf(p, "\"framesize\":%u,", s->status.framesize);
    p += sprintf(p, "\"quality\":%u,", s->status.quality);
    p += sprintf(p, "\"brightness\":%d,", s->status.brightness);
    p += sprintf(p, "\"contrast\":%d,", s->status.contrast);
    p += sprintf(p, "\"saturation\":%d,", s->status.saturation);
    p += sprintf(p, "\"device_id\":\"%s\",", config_data.device_id);
    p += sprintf(p, "\"service_ip\":\"%s\",", config_data.service_ip_str);
    p += sprintf(p, "\"service_port\":\"%d\",", config_data.service_port);
    p += sprintf(p, "\"wifi_ssid\":\"%s\",", config_data.wifi_ssid);
    p += sprintf(p, "\"wifi_key\":\"%s\",", config_data.wifi_key);
    p += sprintf(p, "\"wifi_ap_ssid\":\"%s\",", config_data.wifi_ap_ssid);
    p += sprintf(p, "\"wifi_ap_key\":\"%s\",", config_data.wifi_ap_key);
    p += sprintf(p, "\"ir_voltage\":\"%d\",", config_data.ir_voltage);
    p += sprintf(p, "\"sharpness\":%d,", s->status.sharpness);
    p += sprintf(p, "\"special_effect\":%u,", s->status.special_effect);
    p += sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
    p += sprintf(p, "\"awb\":%u,", s->status.awb);
    p += sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
    p += sprintf(p, "\"aec\":%u,", s->status.aec);
    p += sprintf(p, "\"aec2\":%u,", s->status.aec2);
    p += sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
    p += sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
    p += sprintf(p, "\"agc\":%u,", s->status.agc);
    p += sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
    p += sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
    p += sprintf(p, "\"bpc\":%u,", s->status.bpc);
    p += sprintf(p, "\"wpc\":%u,", s->status.wpc);
    p += sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
    p += sprintf(p, "\"lenc\":%u,", s->status.lenc);
    p += sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
    p += sprintf(p, "\"dcw\":%u,", s->status.dcw);
    p += sprintf(p, "\"colorbar\":%u", s->status.colorbar);
#ifdef CONFIG_LED_ILLUMINATOR_ENABLED
    p += sprintf(p, ",\"led_intensity\":%u", led_duty);
#else
    p += sprintf(p, ",\"led_intensity\":%d", -1);
#endif
#if CONFIG_ESP_FACE_DETECT_ENABLED
    p += sprintf(p, ",\"face_detect\":%u", detection_enabled);
#if CONFIG_ESP_FACE_RECOGNITION_ENABLED
    p += sprintf(p, ",\"face_enroll\":%u,", is_enrolling);
    p += sprintf(p, "\"face_recognize\":%u", recognition_enabled);
#endif
#endif
    *p++ = '}';
    *p++ = 0;
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json_response, strlen(json_response));
}

static esp_err_t index_handler(httpd_req_t *req)
{
//    FILE *fd;
//    char buf[128];
    nvs_handle my_handle;
    esp_err_t err;
    config_para config_data;

    memset(&config_data, 0, sizeof(config_data));

    extern const unsigned char index_ov2640_html_gz_start[] asm("_binary_index_ov2640_html_gz_start");
    extern const unsigned char index_ov2640_html_gz_end[] asm("_binary_index_ov2640_html_gz_end");
    size_t index_ov2640_html_gz_len = index_ov2640_html_gz_end - index_ov2640_html_gz_start;

    extern const unsigned char index_ov3660_html_gz_start[] asm("_binary_index_ov3660_html_gz_start");
    extern const unsigned char index_ov3660_html_gz_end[] asm("_binary_index_ov3660_html_gz_end");
    size_t index_ov3660_html_gz_len = index_ov3660_html_gz_end - index_ov3660_html_gz_start;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL)
    {
        if (s->id.PID == OV3660_PID)
        {
            return httpd_resp_send(req, (const char *)index_ov3660_html_gz_start, index_ov3660_html_gz_len);
        }
        else
        {
            return httpd_resp_send(req, (const char *)index_ov2640_html_gz_start, index_ov2640_html_gz_len);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Camera sensor not found");
        return httpd_resp_send_500(req);
    }
}

void app_httpd_main()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL};

    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_handler,
        .user_ctx = NULL};

    httpd_uri_t cmd_uri = {
        .uri = "/control",
        .method = HTTP_GET,
        .handler = cmd_handler,
        .user_ctx = NULL};

    httpd_uri_t capture_uri = {
        .uri = "/capture",
        .method = HTTP_GET,
        .handler = capture_handler,
        .user_ctx = NULL};

    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL};
        
    ra_filter_init(&ra_filter, 20);

#if CONFIG_ESP_FACE_DETECT_ENABLED

#if CONFIG_ESP_FACE_DETECT_MTMN
    mtmn_config.type = FAST;
    mtmn_config.min_face = 80;
    mtmn_config.pyramid = 0.707;
    mtmn_config.pyramid_times = 4;
    mtmn_config.p_threshold.score = 0.6;
    mtmn_config.p_threshold.nms = 0.7;
    mtmn_config.p_threshold.candidate_number = 20;
    mtmn_config.r_threshold.score = 0.7;
    mtmn_config.r_threshold.nms = 0.7;
    mtmn_config.r_threshold.candidate_number = 10;
    mtmn_config.o_threshold.score = 0.7;
    mtmn_config.o_threshold.nms = 0.7;
    mtmn_config.o_threshold.candidate_number = 1;
#endif

#if CONFIG_ESP_FACE_DETECT_LSSH
    lssh_config = lssh_get_config(min_face, 0.7, 0.3, 240, 320);
#endif

#if CONFIG_ESP_FACE_RECOGNITION_ENABLED
    face_id_init(&id_list, FACE_ID_SAVE_NUMBER, ENROLL_CONFIRM_TIMES);
#endif

#endif

    ESP_LOGI(TAG, "Starting web server on port: '%d'", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
        httpd_register_uri_handler(camera_httpd, &status_uri);
        httpd_register_uri_handler(camera_httpd, &capture_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
    ESP_LOGI(TAG, "Starting stream server on port: '%d'", config.server_port);
    if (httpd_start(&stream_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}

