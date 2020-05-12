#ifndef __SPIFFS_EXAMPLE_MAIN_H
#define __SPIFFS_EXAMPLE_MAIN_H



typedef struct runLog {
    time_t boot;
    uint32_t connect_wifi;
    uint32_t connect_server;
    uint32_t camera_over;
    uint32_t send_over;
    uint32_t send_fail;
    uint32_t low_battery;
} __attribute__((packed)) RunLog;
//typedef struct runLog RunLog;


#define SET_LOG(x) do {\
    run_log.x = xTaskGetTickCount();\
} while(0)


extern RunLog run_log;
void spiffs_exam_app_main(void);
int run_log_write(void) ;
int run_log_read(int) ;


#endif
