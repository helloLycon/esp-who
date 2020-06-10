#ifndef __COMMON_H__
#define __COMMON_H__

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#define DEVICE_ID_FLAG      "device_id"
#define SERVICE_IP_FLAG     "service_ip"
#define SERVICE_PORT_FLAG   "service_port"
#define TCP_SERVER_ADRESS   "10.10.1.238"     //作为client，要连接TCP服务器地址
#define DEVICE_INFO         "123456"
#define CAMERA_OVER         "airbat\tover\r\n"
#define CAMERA_STOP         "airbat\tpass\r\n"
#define STORAGE_NAMESPACE   "storage"

#define sec2tick(x)  ((x)*configTICK_RATE_HZ)

#define ECHO_UART_NUM   (UART_NUM_0)

#define DBG_NO_SLEEP_MODE  0
#define DEF_MAX_SLEEP_TIME  600000

#define APP_PACKET_DATA_LEN  (1024*20)

extern xSemaphoreHandle vq_mtx;
#define lock_vq()   xSemaphoreTake(vq_mtx, portMAX_DELAY)
#define unlock_vq() xSemaphoreGive(vq_mtx)

/*核心板命令码*/
#define CORE_SHUT_DOWN_REQ   "~shutdown_coreboard_request"  //核心板发出关机请求
#define CORE_SHUT_DOWN       "~shutdown_coreboard_cmd"      //主控mcu发出关机命令
#define CORE_SHUT_DOWN_OK    "~coreboard_shutdown_ok"       //核心板关机完成
#define IR_WKUP_PIN_RISING   "~ir_wkup_pin_rising"              //唤醒脚上升沿，有人移动
#define IR_WKUP_PIN_FALLING  "~ir_wkup_pin_falling"            //唤醒脚下降沿，没人移动
#define SET_IR_VOLTAGE       "~set_ir_voltage="             //配置红外灯控制电压阀值
#define SET_IR_VOLTAGE_SUC   "~set_ir_voltage_suc"
#define SET_IR_VOLTAGE_FAIL  "~set_ir_voltage_fail"
#define GET_STATUS    "~get_status"                           //获取状态
#define REC_STATUS  "~rec_status="                          //返回状态~status=唤醒来源，移动侦测io状态，~rec_status=key(ir),high(low)
#define KEY_WKUP_PIN_RISING  "~key_wkup_pin_rising"    //按键唤醒脚上升沿，有按键事件
#define CAMERA_POWER_DOWN_REQ    "~camera_power_down_req"
#define CAMERA_POWER_DOWN_OK    "~camera_power_down_ok"


#define TCP_PORT            10086               //统一的端口号，包括TCP客户端或者服务端
#define MAX_CAMERA_VIDEO_TIME   10

#define IR_VOL_UNSET      0
#define LAST_BTRY_PERCENT_UNSET 0xffff

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
    char wifi_ssid[64];
    char wifi_key[32];
    char wifi_ap_ssid[64];
    char wifi_ap_key[32];
    int  ir_voltage;
    /* sntp->rtc */
    time_t last_sntp;
    uint32_t rtc_set;
    /* last battery-percent */
    uint16_t last_btry_percent;
}config_para;

/* 初始化参数 */
typedef struct init_info{
    config_para config_data;        /* 配置参数 */
    time_t start_time;              /* 启动时间 */
}init_info;

enum CamStatus {
    CAM_IDLE = 0,
    CAM_CAPTURE,
};

struct cam_ctrl_block {
    bool first_capture_determined; //第一个视频是否已经被决断(valid/unvalid)
    enum CamStatus status;
    uint32_t start_ticks;
    uint32_t last_rising_edge;
    uint32_t prev_rising_edge;  //倒数第二个
    uint32_t last_falling;
};

extern unsigned char g_pic_send_over;
extern xSemaphoreHandle g_update_over;
extern init_info g_init_data;
extern portMUX_TYPE max_sleep_uptime_spinlock;
extern portMUX_TYPE g_pic_send_over_spinlock;
extern portMUX_TYPE cam_ctrl_spinlock;
extern bool wake_up_flag;
extern xSemaphoreHandle vpercent_ready;
extern xSemaphoreHandle g_data_mutex;
extern xSemaphoreHandle vq_upload_trigger;
extern xSemaphoreHandle vq_save_trigger;
extern unsigned char is_connect;
extern int s_retry_num;
extern TaskHandle_t get_camera_data_task_handle;
extern TaskHandle_t send_queue_pic_task_handle;
extern TaskHandle_t simple_ota_example_task_handle;
extern struct cam_ctrl_block cam_ctrl;

void upgrade_block(void) ;
int led_gpio_init(void);
esp_err_t store_init_data(void);
const char *mk_win_time_str(const time_t t,char *str);


#endif

