#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "string.h"
#include "esp_log.h"
#include "esp_vfs_dev.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "DWIN_HMI.h"
#include "motorControl.h"
#include "dist.h"

#define BUF_SIZE (1024)

#define LOW 0
#define HIGH 1

QueueHandle_t motorQueue;
QueueHandle_t displayQueue;

void UartInit()
{
    // --- Configure UART0 ---
    uart_driver_install(UART_NUM_0, BUF_SIZE, 256, 0, NULL, 0);
    uart_flush_input(UART_NUM_0); // clear any junk

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

void motorInit()
{
    // --- Configure PWM using LEDC ---
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT, // 0–1023
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 20000, // 20 kHz PWM
        .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .gpio_num = PWM_PIN,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0, // Start stopped
        .hpoint = 0};
    ledc_channel_config(&ledc_channel);

    motor_stop();
}

void init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
}

void adc_init()
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_2, ADC_ATTEN_DB_11);
}

void app_main(void)
{
    // Inits
    UartInit();
    gpioInit();
    motorInit();
    init_nvs();
    adc_init();

    
    motorQueue = xQueueCreate(10, sizeof(motor_cmd_t));
    displayQueue = xQueueCreate(10, sizeof(display_msg_t));
    // Starting Log
    ESP_LOGW("Esp32", "COWv1.2 ...");

    int8_t theme = loadTheme();
    if(theme == 1){
        setPage(21);
    }else{
        setPage(18);
    }

    bottom_limit_mm = load_limit("limit_bottom");
    top_limit_mm = load_limit("limit_top");
    if (top_limit_mm == -1.0 || bottom_limit_mm == -1.0)
    {  
        initial_calib = 1;
        run_calibration();
        initial_calib = 0;
    }

    start_dwin_task();
    start_motor_task();
    start_distance_task();
    start_animDisp_task();

    

}
