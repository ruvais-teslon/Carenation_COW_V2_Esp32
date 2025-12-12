#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "dist.h"
#include "nvs_flash.h"
#include "nvs.h"

extern QueueHandle_t motorQueue;

#define EN_PIN 40
#define SLP_PIN 47
#define PWM_PIN 4

extern float target_position_mm;
extern int8_t selected_preset;
extern float current_height_mm;

extern int8_t initial_calib;
extern bool calibrating;
// Motor direction
typedef enum {
    MOTOR_DIR_FORWARD = 1,
    MOTOR_DIR_BACKWARD = 0
} motor_direction_t;

typedef enum {
    MOTOR_CMD_FORWARD,
    MOTOR_CMD_BACKWARD,
    MOTOR_CMD_STOP,
    MOTOR_CMD_GOTO_POSITION,
    MOTOR_CMD_SAVE_POSITION,
    MOTOR_CMD_CALIBRATE
} motor_cmd_t;


typedef enum {
    MOTOR_WAKE = 1,
    MOTOR_SLEEP = 0
} motor_sleepcntrl_t;

void motor_forward();
void motor_backward();
void motor_stop(void);
void run_calibration();
void start_motor_task();
void save_preset(int8_t id, float value);
float load_preset(int8_t id);
void beepHMI();