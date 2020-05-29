#ifndef __SD_CARD_EXAMPLE_H
#define __SD_CARD_EXAMPLE_H


enum log_type {
    LOG_CONNECT_WIFI = 0,
    LOG_CONNECT_SERVER,
    LOG_CAMERA_OVER,
    LOG_SEND_OVER,
    LOG_SEND_FAIL,
    LOG_LOW_BATTERY,
};

void sdcard_init_main(void);
int sdcard_test(void);
int log_enum(enum log_type type) ;
esp_err_t sdcard_log_write(void);
char *sdcard_log_init(void) ;


#endif
