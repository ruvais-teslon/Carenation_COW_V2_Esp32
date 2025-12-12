#include "stdint.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"

#define PROXIMITY_TOP 13
#define PROXIMITY_BOTTOM 14
#define IR_ADC_CHANNEL ADC1_CHANNEL_2


extern float bottom_limit_mm,top_limit_mm,center_mm;
void adc_init();
void save_limit(const char *key, float value);
float load_limit(const char *key);
int8_t hit_bottom_limit();
int8_t hit_top_limit();
void start_distance_task();
void read_distance_mm();
