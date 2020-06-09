#ifndef __SD_CARD_EXAMPLE_H
#define __SD_CARD_EXAMPLE_H


#include "video-queue.h"

enum log_type {
    LOG_CONNECT_WIFI = 0,
    LOG_CONNECT_SERVER,
    LOG_CAMERA_OVER,
    LOG_SEND_OVER,
    LOG_SEND_FAIL,
    LOG_LOW_BATTERY,
    LOG_CONFIGURATION,
};

esp_err_t sdcard_init(void);
void sdcard_init_main(void);
int sdcard_test(void);
int log_enum(enum log_type type) ;
int log_printf(const char *format, ...) ;
esp_err_t sdcard_log_write(void);
char *sdcard_log_init(void) ;
void save_video_into_sdcard_task(void *arg);
const char *mk_sd_time_fname(time_t t, char *tmp) ;
int read_one_pic_from_sdcard(pic_queue *pic) ;
void rd_sdcard_fp_close(void) ;
int rd_sdcard_fp_open(bool force_reinit, time_t t) ;
void wr_sdcard_fp_close(void) ;
int wr_sdcard_fp_open(bool force_reinit, time_t t) ;

extern pic_queue *save_pic_pointer;


#endif
