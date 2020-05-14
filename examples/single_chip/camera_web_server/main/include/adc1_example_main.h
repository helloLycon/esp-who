#ifndef __ADC1_EXAMPLE_MAIN_H
#define __ADC1_EXAMPLE_MAIN_H






void adc_app_main_init(void);
uint16_t  adc_read_battery_percent(void) ;
bool fix_battery_percent(uint16_t *current_value, const uint16_t *prev_value);

extern uint16_t vPercent;





#endif
