#include "motorControl.h"
#include "PC_DATA.h"
#include "DWIN_HMI.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

motor_direction_t current_dir = MOTOR_DIR_FORWARD;
bool calibrating = false;
int8_t initial_calib = 0;
void motor_set_direction(motor_direction_t dir)
{
    gpio_set_level(EN_PIN, dir);
}

void motorSleepContrl(motor_sleepcntrl_t sleep)
{
    gpio_set_level(SLP_PIN, sleep);
}

void motor_set_speed(int duty)
{
    if (duty < 0)
        duty = 0;
    if (duty > 1023)
        duty = 1023;

    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void motor_forward()
{
    current_dir = MOTOR_DIR_FORWARD;
    motorSleepContrl(MOTOR_WAKE);
    motor_set_direction(MOTOR_DIR_FORWARD);
    motor_set_speed(1023);
}

void motor_backward()
{
    current_dir = MOTOR_DIR_BACKWARD;
    motorSleepContrl(MOTOR_WAKE);
    motor_set_direction(MOTOR_DIR_BACKWARD);
    motor_set_speed(1023);
}

void motor_stop(void)
{
    current_dir = 3;
    motor_set_speed(0);
    motorSleepContrl(MOTOR_SLEEP);
}

void movetoCenter()
{
    int64_t now = esp_timer_get_time();
    motor_backward();
    while (esp_timer_get_time() - now < 13000000)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    motor_stop();
}

void move_to_position(float target)
{
    printf("Target = %f, Current Height = %f\r\n", target, current_height_mm);

    bool going_up = (target > current_height_mm);

    if (going_up)
        motor_forward();
    else
        motor_backward();

    while (1)
    {
        // If going UP, only top limit matters
        if (going_up && hit_top_limit())
        {
            motor_stop();
            printf("Stopped: Top limit hit\n");
            break;
        }

        // If going DOWN, only bottom limit matters
        if (!going_up && hit_bottom_limit())
        {
            motor_stop();
            printf("Stopped: Bottom limit hit\n");
            break;
        }

        // Normal position comparison
        if (fabs(current_height_mm - target) < 0.1f)
        {
            printf("Reached height: %f\n", current_height_mm);
            motor_stop();
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void run_calibration()
{
    calibrating = true;
    int8_t theme = loadTheme();
    if (!initial_calib)
    {
        display_set_page(theme == 1 ? 15 : 14);
    }
    else
    {
        setPage(theme == 1 ? 15 : 14);
    }
    // Move down
    motor_backward();
    while (!hit_bottom_limit())
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    printf("Reached Down\r\n");
    motor_stop();
    vTaskDelay(pdMS_TO_TICKS(10));
    if (initial_calib)
    {
        read_distance_mm();
    }
    bottom_limit_mm = current_height_mm;
    printf("Bottom = %f\r\n", bottom_limit_mm);
    save_limit("limit_bottom", bottom_limit_mm);

    // Move up
    motor_forward();
    while (!hit_top_limit())
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    printf("Reached Up\r\n");
    motor_stop();

    vTaskDelay(pdMS_TO_TICKS(10));
    if (initial_calib)
    {
        for (int8_t i = 0; i < 10; i++)
        {
            read_distance_mm();
        }
    }
    top_limit_mm = current_height_mm;
    printf("Top = %f", top_limit_mm);
    save_limit("limit_top", top_limit_mm);

    float center = (top_limit_mm + bottom_limit_mm) / 2;
    if (initial_calib)
    {
        movetoCenter();
    }
    else
    {
        move_to_position(center);
    }
    if (pcConnected)
    {
        if (!initial_calib)
        {
            display_set_page(theme == 1 ? 5 : 1);
        }
        else
        {
            setPage(theme == 1 ? 5 : 1);
        }
    }
    else
    {
        if (!initial_calib)
        {
            display_set_page(theme == 1 ? 21 : 18);
        }
        else
        {
            setPage(theme == 1 ? 21 : 18);
        }
    }
    calibrating = false;
}

void save_preset(int8_t id, float value)
{
    char key[16];
    snprintf(key, sizeof(key), "preset%d", id);
    printf("Preset %d Saved = %f\r\n", id, value);
    nvs_handle_t h;
    nvs_open("settings", NVS_READWRITE, &h);
    int32_t temp = (int32_t)(value * 1000);
    printf("Saved %d\r\n", temp);
    nvs_set_i32(h, key, temp);
    nvs_commit(h);
    nvs_close(h);
}

float load_preset(int8_t id)
{
    int32_t value = 0;
    char key[16];
    snprintf(key, sizeof(key), "preset%d", id);

    nvs_handle_t h;
    esp_err_t err = nvs_open("settings", NVS_READONLY, &h);
    if (err != ESP_OK)
    {
        printf("NVS open failed\n");
        return -1; // or default
    }

    err = nvs_get_i32(h, key, &value);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        printf("%s not found in NVS\n", key);
        nvs_close(h);
        return -1; // treat missing as -1
    }

    nvs_close(h);
    float f = value / 1000.0f;
    return f;
}

void motor_task(void *arg)
{
    motor_cmd_t cmd;
    while (1)
    {
        if (!calibrating) // disable safety during calibration
        {
            if (current_dir == MOTOR_DIR_FORWARD && hit_top_limit())
                motor_stop();

            if (current_dir == MOTOR_DIR_BACKWARD && hit_bottom_limit())
                motor_stop();
        }

        if (xQueueReceive(motorQueue, &cmd, 10 / portTICK_PERIOD_MS))
        {
            switch (cmd)
            {
            case MOTOR_CMD_FORWARD:
                if (!hit_top_limit())
                {
                    motor_forward();
                }
                else
                {
                    motor_stop();
                }
                break;
            case MOTOR_CMD_BACKWARD:
                if (!hit_bottom_limit())
                {
                    motor_backward();
                }
                else
                {
                    motor_stop();
                }
                break;
            case MOTOR_CMD_STOP:
                motor_stop();
                break;
            case MOTOR_CMD_GOTO_POSITION:
            {
                int8_t theme = loadTheme();
                display_set_page(theme == 1 ? 16 : 17);

                move_to_position(target_position_mm);

                if (pcConnected)
                {
                    display_set_page(theme == 1 ? 5 : 1);
                }
                else
                {
                    display_set_page(theme == 1 ? 21 : 18);
                }
                break;
            }

            case MOTOR_CMD_SAVE_POSITION:
            {
                save_preset(selected_preset, current_height_mm);
                printf("Saving Presets\r\n");
                int8_t theme = loadTheme();
                display_set_page(theme == 1 ? 8 : 4);
                beepHMI();
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (pcConnected)
                {
                    display_set_page(theme == 1 ? 5 : 1);
                }
                else
                {
                    display_set_page(theme == 1 ? 21 : 18);
                }
                break;
            }

            case MOTOR_CMD_CALIBRATE:
                printf("Caliberating\r\n");
                run_calibration();
                break;
            }
        }
    }
}

void start_motor_task()
{
    xTaskCreate(motor_task, "motor_task", 4096, NULL, 5, NULL);
}