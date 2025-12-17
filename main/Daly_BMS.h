#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"

#define BMS_UART UART_NUM_2

extern bool motorLockedLowSOC;
typedef enum {
    VOUT_IOUT_SOC = 0x90,
    MIN_MAX_TEMPERATURE = 0x92,
    DISCHARGE_CHARGE_MOS_STATUS = 0x93
} daly_cmd_t;

typedef struct {
    float pack_voltage;   // V
    float pack_current;   // A
    float pack_soc;       // %
} daly_pack_data_t;

typedef struct {
    int8_t max_temp;      // °C
    int8_t min_temp;      // °C
    float avg_temp;
} daly_temp_data_t;

daly_pack_data_t g_pack;
daly_temp_data_t g_temp;

void start_bms_task();