#include "DWIN_HMI.h"
#include "motorControl.h"
#include "PC_DATA.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define DWIN_VP_UPDOWN 0x50
#define DWIN_VP_PRESETS 0x71
#define DWIN_VP_CALIBRATE 0x81
#define DWIN_VP_THEME 0x85

static int64_t press_start_time = 0;
int8_t selected_preset = 0;
static bool long_press_action_done = false;

float target_position_mm = 0;
float preset1_mm = 0, preset2_mm = 0, preset3_mm = 0;

void setPage(uint8_t page)
{
    uint8_t cmd[] = {CMD_HEAD1, CMD_HEAD2, 0x07, CMD_WRITE, 0x00, 0x84, 0x5A, 0x01, 0x00, page};
    uart_write_bytes(DWIN_UART, (const char *)cmd, sizeof(cmd));
}

void setBrightness(int8_t brightness)
{
    int8_t cmd[] = {CMD_HEAD1, CMD_HEAD2, 0x04, CMD_WRITE, 0x00, 0x82, brightness};
    uart_write_bytes(DWIN_UART, (const char *)cmd, sizeof(cmd));
}

void setText(long address, const char *text)
{
    int dataLen = strlen(text);
    if (dataLen <= 0)
        return;

    int8_t startCmd[] = {CMD_HEAD1, CMD_HEAD2, (uint8_t)(dataLen + 3), CMD_WRITE, (uint8_t)((address >> 8) & 0xFF), (uint8_t)(address & 0xFF)};

    int8_t sendBuffer[6 + dataLen];
    memcpy(sendBuffer, startCmd, sizeof(startCmd));
    memcpy(sendBuffer + 6, text, dataLen);
    uart_write_bytes(DWIN_UART, (const char *)sendBuffer, sizeof(sendBuffer));
}

void setVP(long address, int8_t data)
{
    int8_t cmd[] = {CMD_HEAD1, CMD_HEAD2, 0x05, CMD_WRITE, (int8_t)((address >> 8) & 0xFF), (int8_t)(address & 0xFF), 0x00, data};
    uart_write_bytes(DWIN_UART, (const char *)cmd, sizeof(cmd));
}

void restartHMI(void)
{
    uint8_t cmd[] = {CMD_HEAD1, CMD_HEAD2, 0x07, CMD_WRITE, 0x00, 0x04, 0x55, 0xAA, CMD_HEAD1, CMD_HEAD2};
    uart_write_bytes(DWIN_UART, (const char *)cmd, sizeof(cmd));
    vTaskDelay(pdMS_TO_TICKS(10));
}

void beepHMI()
{
    int8_t cmd[] = {CMD_HEAD1, CMD_HEAD2, 0x05, CMD_WRITE, 0x00, 0xA0, 0x00, 0x7D};
    uart_write_bytes(DWIN_UART, (const char *)cmd, sizeof(cmd));
}

static inline int constrainInt(int x, int a, int b)
{
    if (x < a)
        return a;
    else if (x > b)
        return b;
    else
        return x;
}

void updatePackMeasurementsOnHMI(float voltage, float current, float soc)
{
    char buffer[16];

    // Format SoC (State of Charge) as integer string
    int socInt = (int)round(soc);
    snprintf(buffer, sizeof(buffer), "%d     ", socInt);
    setText(0x2000, buffer);

    int mappedValue = (int)constrainInt(((soc - 1) / 20) + 1, 1, 5); // Map SoC to 1-5 range

    // Calculate power (watts)
    float watts = voltage * current;
    watts = fabs(watts);
    int wattsInt = (int)round(watts);
    snprintf(buffer, sizeof(buffer), "%d     ", wattsInt);

    if (current > 0)
    {                            // charging
        setText(0x4000, buffer); // watts on charging display
        setText(0x6000, "0           ");
        setVP(0x3100, mappedValue + 5);
    }
    else
    {                            // discharging
        setText(0x6000, buffer); // watts on discharging display
        setText(0x4000, "0           ");
        setVP(0x3100, mappedValue);
    }
}

void updatePackTempOnHMI(int tempMin, int tempMax, float tempAverage)
{
    char buffer[8];

    snprintf(buffer, sizeof(buffer), "%d", tempMin);
    setText(0x7000, buffer);

    snprintf(buffer, sizeof(buffer), "%d", tempMax);
    setText(0x8000, buffer);

    snprintf(buffer, sizeof(buffer), "%.1f", tempAverage);
    setText(0x3000, buffer);
}

float mapf(float x, float in_min, float in_max, float out_min, float out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

int constrain(int x, int a, int b)
{
    if (x < a)
        return a;
    if (x > b)
        return b;
    return x;
}

int map_with_hysteresis(float height, int steps)
{
    static int prev_10 = -1;
    static int prev_20 = -1;

    int *prev;
    if (steps == 10)
        prev = &prev_10;
    else if (steps == 20)
        prev = &prev_20;
    else
        return 1;

    float mapped = mapf(height, bottom_limit_mm, top_limit_mm, 0, steps - 1);
    int newVal = constrain((int)mapped + 1, 1, steps);

    if (*prev == -1)
    {
        *prev = newVal;
        return newVal;
    }

    float step_mm = (top_limit_mm - bottom_limit_mm) / (float)steps;

    // FIXED, SYMMETRIC HYSTERESIS (e.g. 30% of step)
    float hyst = 0.10f * step_mm;

    float zone_min = bottom_limit_mm + (*prev - 1) * step_mm;
    float zone_max = zone_min + step_mm;

    if (height < (zone_min - hyst))
        (*prev)--;
    else if (height > (zone_max + hyst))
        (*prev)++;

    return constrain(*prev, 1, steps);
}

void display_set_page(uint16_t page)
{
    display_msg_t msg = {
        .cmd = DISP_CMD_SET_PAGE,
        .value = page};
    xQueueSend(displayQueue, &msg, 0);
}

void display_set_text(uint16_t addr, const char *txt)
{
    display_msg_t msg = {
        .cmd = DISP_CMD_SET_TEXT,
        .addr = addr};
    strncpy(msg.text, txt, sizeof(msg.text));
    xQueueSend(displayQueue, &msg, 0);
}

void display_set_vp(uint16_t addr, uint16_t value)
{
    display_msg_t msg = {
        .cmd = DISP_CMD_SET_VP,
        .addr = addr,
        .value = value};
    xQueueSend(displayQueue, &msg, 0);
}

static void handle_preset_code(uint8_t code)
{
    int64_t now = esp_timer_get_time();

    // -------------------- PRESET 1 --------------------
    if (code == 0x05) // PRESS PRESET 1
    {
        selected_preset = 1;
        press_start_time = now;
        long_press_action_done = false;
        printf("Pressed preset 1\n");
        return;
    }

    if (code == 0x06) // HOLD REPEAT FOR PRESET 1
    {
        if (selected_preset == 1 &&
            !long_press_action_done &&
            now - press_start_time >= 3000000)
        {
            motor_cmd_t cmd = MOTOR_CMD_SAVE_POSITION;
            xQueueSend(motorQueue, &cmd, 0);
            long_press_action_done = true;

            printf("SAVE preset 1 (long press)\n");
        }
        return;
    }

    if (code == 0x11) // RELEASE PRESET 1
    {
        if (selected_preset == 1)
        {
            if (!long_press_action_done)
            {
                // SHORT PRESS → GO
                preset1_mm = loadPreset(1);
                target_position_mm = preset1_mm;

                motor_cmd_t cmd = MOTOR_CMD_GOTO_POSITION;
                xQueueSend(motorQueue, &cmd, 0);

                printf("GO preset 1 (short press)\n");
            }
            else
            {
                printf("Preset 1 released after long press\n");
            }
        }

        selected_preset = 0;
        press_start_time = 0;
        long_press_action_done = false;
        return;
    }

    // -------------------- PRESET 2 --------------------
    if (code == 0x07) // PRESS PRESET 2
    {
        selected_preset = 2;
        press_start_time = now;
        long_press_action_done = false;
        printf("Pressed preset 2\n");
        return;
    }

    if (code == 0x08) // HOLD REPEAT FOR PRESET 2
    {
        if (selected_preset == 2 &&
            !long_press_action_done &&
            now - press_start_time >= 3000000)
        {
            motor_cmd_t cmd = MOTOR_CMD_SAVE_POSITION;
            xQueueSend(motorQueue, &cmd, 0);
            long_press_action_done = true;

            printf("SAVE preset 2 (long press)\n");
        }
        return;
    }

    if (code == 0x12) // RELEASE PRESET 2
    {
        if (selected_preset == 2)
        {
            if (!long_press_action_done)
            {
                preset2_mm = loadPreset(2);
                target_position_mm = preset2_mm;

                motor_cmd_t cmd = MOTOR_CMD_GOTO_POSITION;
                xQueueSend(motorQueue, &cmd, 0);

                printf("GO preset 2 (short press)\n");
            }
            else
            {
                printf("Preset 2 released after long press\n");
            }
        }

        selected_preset = 0;
        press_start_time = 0;
        long_press_action_done = false;
        return;
    }

    // -------------------- PRESET 3 --------------------
    if (code == 0x09) // PRESS PRESET 3
    {
        selected_preset = 3;
        press_start_time = now;
        long_press_action_done = false;
        printf("Pressed preset 3\n");
        return;
    }

    if (code == 0x10) // HOLD REPEAT FOR PRESET 3
    {
        if (selected_preset == 3 &&
            !long_press_action_done &&
            now - press_start_time >= 3000000)
        {
            motor_cmd_t cmd = MOTOR_CMD_SAVE_POSITION;
            xQueueSend(motorQueue, &cmd, 0);
            long_press_action_done = true;

            printf("SAVE preset 3 (long press)\n");
        }
        return;
    }

    if (code == 0x13) // RELEASE PRESET 3
    {
        if (selected_preset == 3)
        {
            if (!long_press_action_done)
            {
                preset3_mm = loadPreset(3);
                target_position_mm = preset3_mm;

                motor_cmd_t cmd = MOTOR_CMD_GOTO_POSITION;
                xQueueSend(motorQueue, &cmd, 0);

                printf("GO preset 3 (short press)\n");
            }
            else
            {
                printf("Preset 3 released after long press\n");
            }
        }

        selected_preset = 0;
        press_start_time = 0;
        long_press_action_done = false;
        return;
    }
}

static void dwin_rx_task(void *arg)
{
    uint8_t data[64];

    while (1)
    {
        int len = uart_read_bytes(DWIN_UART, data, sizeof(data), pdMS_TO_TICKS(20));

        if (len > 0)
        {
            printf("RX (%d): ", len);
            for (int i = 0; i < len; i++)
                printf("%02X ", data[i]);
            printf("\n");
        }

        // -------------------------------------------------------
        //  FIX: Process ALL packets inside the received buffer
        // -------------------------------------------------------
        int8_t index = 0;

        while (index + 9 <= len) // at least 1 full packet available
        {
            // Search for packet header
            if (data[index] == 0x5A && data[index + 1] == 0xA5)
            {
                uint8_t *pkt = &data[index]; // pointer to this packet

                uint8_t vp = pkt[4];
                uint8_t code = pkt[5];

                // ------------------ UP/DOWN Buttons -------------------
                if (vp == DWIN_VP_UPDOWN)
                {
                    motor_cmd_t cmd;

                    if (code == 0x01)
                    {
                        cmd = MOTOR_CMD_BACKWARD;
                        // printf("Downward\r\n");
                    }
                    else if (code == 0x02)
                    {
                        cmd = MOTOR_CMD_FORWARD;
                        // printf("Upward\r\n");
                    }
                    else if (code == 0x03 || code == 0x04)
                    {
                        cmd = MOTOR_CMD_STOP;
                        printf("STOP\r\n");
                    }
                    else
                    {
                        index += 1;
                        continue;
                    }

                    xQueueSend(motorQueue, &cmd, 0);
                }

                // ------------------ PRESET BUTTONS --------------------
                else if (vp == DWIN_VP_PRESETS)
                {
                    handle_preset_code(code);
                }

                // ------------------ CALIBRATE BUTTON ------------------
                else if (vp == DWIN_VP_CALIBRATE)
                {
                    if (code == 0x00)
                    {
                        printf("Calibrate\r\n");
                        motor_cmd_t cmd = MOTOR_CMD_CALIBRATE;
                        xQueueSend(motorQueue, &cmd, 0);
                    }
                }

                else if (vp == DWIN_VP_THEME)
                {
                    if (pkt[8] == 0x01)
                    {
                        saveTheme(2);
                        if (pcConnected)
                        {
                            setPage(9);
                        }
                        else
                        {
                            setPage(20);
                        }
                    }
                    else if (pkt[8] == 0x02)
                    {
                        saveTheme(1);
                        if (pcConnected)
                        {
                            setPage(10);
                        }
                        else
                        {
                            setPage(23);
                        }
                    }
                }

                // move to next packet
                index += 9;
            }
            else
            {
                index++; // try next byte
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void display_task(void *arg)
{
    while (1)
    {
        if (current_height_mm > 0) // valid reading
        {
            // HYSTERESIS MAPPING
            int mappedValue = map_with_hysteresis(current_height_mm, 10);        // step 1–10
            int mappedNumValue = map_with_hysteresis(current_height_mm, 20) - 1; // step 0–19

            // Track old values and update display only on change
            static int prevMappedValue = -1;
            static int prevMappedNumValue = -1;

            if (mappedValue != prevMappedValue)
            {
                char txt[8];
                snprintf(txt, sizeof(txt), "%02d", mappedValue);
                setText(0x1000, txt);

                prevMappedValue = mappedValue;

                printf("Height: %f\n", current_height_mm);
                printf("Mapped Value = %d\r\n", mappedValue);
                printf("bottom=%f  top=%f\n", bottom_limit_mm, top_limit_mm);
            }

            if (mappedNumValue != prevMappedNumValue)
            {
                setVP(0x8100, mappedNumValue);
                prevMappedNumValue = mappedNumValue;

                printf("Mapped Num Value = %d\r\n", mappedNumValue);
            }
        }

        display_msg_t msg;
        while (xQueueReceive(displayQueue, &msg, 0)) // non-blocking
        {
            switch (msg.cmd)
            {
            case DISP_CMD_SET_PAGE:
                setPage(msg.value);
                break;

            case DISP_CMD_SET_TEXT:
                setText(msg.addr, msg.text);
                break;

            case DISP_CMD_SET_VP:
                setVP(msg.addr, msg.value);
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200)); // update every 200ms
    }
}

void start_animDisp_task()
{
    xTaskCreate(display_task, "display_task", 2048, NULL, 4, NULL);
}

void start_dwin_task()
{
    xTaskCreate(dwin_rx_task, "dwin_task", 4096, NULL, 4, NULL);
}