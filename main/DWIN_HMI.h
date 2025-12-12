
#include <stdio.h>
#include "driver/uart.h"
#include "string.h"
#include "math.h"
#include "nvsManager.h"


#define DWIN_UART  UART_NUM_1

// DWIN command headers
#define CMD_HEAD1 0x5A
#define CMD_HEAD2 0xA5
#define CMD_WRITE 0x82
#define CMD_READ 0x83
#define MIN_ASCII 32
#define MAX_ASCII 255

extern QueueHandle_t motorQueue;
extern QueueHandle_t displayQueue;

typedef enum {
    DISP_CMD_SET_PAGE,
    DISP_CMD_SET_TEXT,
    DISP_CMD_SET_VP
} display_cmd_t;

typedef struct {
    display_cmd_t cmd;
    uint16_t addr;      // For text or VP
    char text[16];
    uint16_t value;
} display_msg_t;

void setPage(uint8_t page);
void start_dwin_task();
void start_animDisp_task();

void display_set_page(uint16_t page);
// int8_t loadTheme(void);