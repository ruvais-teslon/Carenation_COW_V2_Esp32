#include "stdint.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "string.h"

extern bool pcConnected;
extern bool calibrating;
extern QueueHandle_t pcQueue;

#define PC_UART UART_NUM_0

#define SYSTEM_METRICS_MARKER   0xAA
#define DEVICE_NAME_MARKER     0xBB

#define SYS_METRICS_LEN   10   // 1 + 8 + 1
#define DEV_NAME_LEN     22   // 1 + 20 + 1

typedef enum {
    RX_WAIT_MARKER,
    RX_COLLECT_FRAME
} rx_state_t;

typedef enum {
    PC_MSG_PACK,
    PC_MSG_TEMP,
    PC_MSG_PACK_INVALID,
    PC_MSG_TEMP_INVALID
} pc_msg_type_t;

typedef struct {
    pc_msg_type_t type;
    union {
        struct {
            float voltage;
            float current;
            float soc;
        } pack;

        struct {
            int min_temp;
            int max_temp;
            float avg_temp;
        } temp;
    };
} pc_msg_t;


void start_pc_task();
void display_device_name(uint16_t vp, const char *name);
void pc_send_temp_data(int min_temp, int max_temp, float avg_temp);
void pc_send_pack_data(float voltage, float current, float soc);
void pc_send_temp_invalid(void);
void pc_send_pack_invalid(void);