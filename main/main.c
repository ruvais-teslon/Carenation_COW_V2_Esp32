#include <stdio.h>
#include "sdkconfig.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "string.h"
#include "esp_log.h"
#include "esp_vfs_dev.h"
#include "DWIN_HMI.h"
#include "motorControl.h"
#include "dist.h"
#include "nvsManager.h"
#include "PC_DATA.h"
#include "Daly_BMS.h"

#define BUF_SIZE (1024)

#define LOW 0
#define HIGH 1

QueueHandle_t motorQueue;
QueueHandle_t displayQueue;
QueueHandle_t pcQueue;

void UartInit()
{
    // --- Configure UART0 ---
    uart_config_t uart0_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};

    uart_param_config(UART_NUM_0, &uart0_config);
    uart_driver_install(UART_NUM_0, BUF_SIZE, 0, 0, NULL, 0);
    uart_flush_input(UART_NUM_0);
    // uart_driver_install(UART_NUM_0, BUF_SIZE, 256, 0, NULL, 0);
    // uart_flush_input(UART_NUM_0); // clear any junk

    // --- Configure UART1 (TX=GPIO17, RX=GPIO18) ---
    const uart_port_t uart1_num = DWIN_UART;
    uart_config_t uart1_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};
    uart_param_config(uart1_num, &uart1_config);
    uart_set_pin(uart1_num, 17, 18, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(uart1_num, BUF_SIZE, 256, 0, NULL, 0);

    // Clear RX buffer before reading
    uart_flush_input(uart1_num);

    // --- Configure UART2 (TX=GPIO35, RX=GPIO36) ---
    const uart_port_t uart2_num = UART_NUM_2;
    uart_config_t uart2_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};
    uart_param_config(uart2_num, &uart2_config);
    uart_set_pin(uart2_num, 35, 36, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(uart2_num, BUF_SIZE, 256, 0, NULL, 0);

    // Clear RX buffer before reading
    uart_flush_input(uart2_num);
}

void gpioInit()
{
    // Configure Proximity pins
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PROXIMITY_TOP) | (1ULL << PROXIMITY_BOTTOM), // both pins
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

    // Configure Direction pin(EN)
    gpio_config_t io_conf_out = {
        .pin_bit_mask = (1ULL << EN_PIN) | (1ULL << SLP_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf_out);

    // Intilaise Configured pins
    gpio_set_level(EN_PIN, LOW);
    gpio_set_level(SLP_PIN, LOW);
}

void app_main(void)
{
    // Inits
    UartInit();  // UART Init
    gpioInit();  // GPIO Init
    motorInit(); // Motor Init
    nvs_init();  // NonVolatile Memmory Init
    adc_init();  // ADC Init

    motorQueue = xQueueCreate(10, sizeof(motor_cmd_t));     // Que Creation for Motor
    displayQueue = xQueueCreate(10, sizeof(display_msg_t)); // Que Creation for Display
    pcQueue = xQueueCreate(10, sizeof(pc_msg_t));
    // Starting Log
    ESP_LOGW("Cow", "V2.1");
    vTaskDelay(300); // Dwin Startup Delay

    int8_t theme = loadTheme();    // load Theme From NVS
    setPage(theme == 1 ? 21 : 18); // Set Page based on theme

    char storedName[21];
    if (loadDevicename(storedName, sizeof(storedName)))
    {
        display_device_name(0x1800, storedName);
    }
    else
    {
        const char *defaultName = "CarenationPC";
        saveDevicename(defaultName);
        display_device_name(0x1800, defaultName);
    }

    // Load Limits from NVS
    bottom_limit_mm = load_limit("limit_bottom");
    top_limit_mm = load_limit("limit_top");
    if (top_limit_mm == -1.0 || bottom_limit_mm == -1.0)
    {
        initial_calib = 1;
        run_calibration();
        initial_calib = 0;
    }

    start_dwin_task(); // Task to receive Data from dwin

    start_motor_task(); // Task to run Motors

    start_distance_task(); // Task to find the distance

    start_animDisp_task(); // Task to change Display Pages/Themes

    start_pc_task(); // Task to Communicate with PC Over UART

    start_bms_task(); // Task to Communicate with BMS
}
