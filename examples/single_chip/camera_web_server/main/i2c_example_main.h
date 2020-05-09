#ifndef __I2C_EXAMPLE_MAIN_H
#define __I2C_EXAMPLE_MAIN_H

#include <time.h>
#include "driver/i2c.h"

/*
typedef struct {
    uint8_t sec;
    uint8_t min;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
} RtcStruct;
*/
#define _I2C_NUMBER(num) I2C_NUM_##num
#define I2C_NUMBER(num) _I2C_NUMBER(num)


#define I2C_MASTER_SCL_IO 26               /*!< gpio number for I2C master clock */
#define I2C_MASTER_SDA_IO 27               /*!< gpio number for I2C master data  */
#define I2C_RTC_MASTER_NUM I2C_NUMBER(0) /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ 100000        /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE 0                           /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0                           /*!< I2C master doesn't need buffer */


#define RTC_SET_MAGIC  0x19999999


typedef struct tm RtcStruct;

void i2c_app_init();
esp_err_t pcf8563RtcRead(i2c_port_t i2c_num, uint8_t *data);
esp_err_t pcf8563RtcWrite(i2c_port_t i2c_num, const RtcStruct *rtcValue);
const char *pcf8563RtcToString(const uint8_t *pd, char *str);
int rtc_read_time(bool);
bool rtc_sntp_needed(void) ;
int sntp_rtc_routine(void) ;



#endif

