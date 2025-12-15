#include "stdint.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "string.h"

extern bool pcConnected;

#define PC_UART UART_NUM_0

#define SYSTEM_METRICS_MARKER   0xAA
#define DEVICE_NAME_MARKER     0xBB

#define SYS_METRICS_LEN   10   // 1 + 8 + 1
#define DEV_NAME_LEN     22   // 1 + 20 + 1

typedef enum {
    RX_WAIT_MARKER,
    RX_COLLECT_FRAME
} rx_state_t;

void start_pc_task();
void display_device_name(uint16_t vp, const char *name);