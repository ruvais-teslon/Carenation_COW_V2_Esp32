#include "Daly_BMS.h"
#include "DWIN_HMI.h"
#include "PC_DATA.h"
#include "motorControl.h"

bool motorLockedLowSOC = false;
static int lastAlertSOC = 100; // start high
static int prevSOC = -1;

void daly_send_command(uint8_t cmd)
{
    uint8_t frame[13] = {
        0xA5, // Start byte
        0x40, // Host address
        cmd,  // Command
        0x08, // Data length
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00};

    uint8_t checksum = 0;
    for (int i = 0; i < 12; i++)
    {
        checksum += frame[i];
    }
    frame[12] = checksum;

    uart_write_bytes(BMS_UART, (const char *)frame, 13);
}

static bool daly_receive(uint8_t expected_cmd, uint8_t *rx)
{
    int len = uart_read_bytes(BMS_UART, rx, 13, pdMS_TO_TICKS(200));
    if (len != 13)
        return false;
    if (rx[0] != 0xA5 || rx[2] != expected_cmd)
        return false;

    uint8_t sum = 0;
    for (int i = 0; i < 12; i++)
        sum += rx[i];
    return (sum == rx[12]);
}

bool getPackMeasurements(void)
{
    uint8_t rx[13];

    uart_flush_input(BMS_UART);
    daly_send_command(VOUT_IOUT_SOC);

    if (!daly_receive(VOUT_IOUT_SOC, rx))
        return false;

    uint16_t rawV = (rx[4] << 8) | rx[5];
    uint16_t rawI = (rx[8] << 8) | rx[9];
    uint16_t rawSOC = (rx[10] << 8) | rx[11];

    g_pack.pack_voltage = rawV * 0.1f;
    g_pack.pack_current = (rawI - 30000) * 0.1f;
    g_pack.pack_soc = rawSOC * 0.1f;

    return true;
}

bool getPackTemp(void)
{
    uint8_t rx[13];

    uart_flush_input(BMS_UART);
    daly_send_command(MIN_MAX_TEMPERATURE);

    if (!daly_receive(MIN_MAX_TEMPERATURE, rx))
        return false;

    g_temp.max_temp = rx[4] - 40;
    g_temp.min_temp = rx[6] - 40;
    g_temp.avg_temp = ((float)g_temp.max_temp + (float)g_temp.min_temp) * 0.5;

    return true;
}

void handleSOCLogic(int soc)
{
    if (soc < 0)
        return;

    /* ---------- HARD LOCK BELOW 3% ---------- */
    if (soc < 3)
    {
        motorLockedLowSOC = true;
        motor_stop();
        display_set_page(12); // critical SOC alert change page with connect charger also add beep
        beepHMI();
        vTaskDelay(pdMS_TO_TICKS(1000));
        prevSOC = soc;
        return;
    }

    /* ---------- UNLOCK WHEN SOC RECOVERS ---------- */
    if (motorLockedLowSOC && soc >= 3)
    {
        motorLockedLowSOC = false;

        // Re-sync alert level but DO NOT trigger alert
        lastAlertSOC = soc - (soc % 5);
        prevSOC = soc;
        return;
    }

    /* ---------- ONLY CHECK WHEN SOC IS DROPPING ---------- */
    if (prevSOC != -1 && soc < prevSOC)
    {
        if (prevSOC > 30 && soc <= 30)
        {
            lastAlertSOC = 30;
            display_set_page(12); // SOC warning alert

            vTaskDelay(pdMS_TO_TICKS(2000));

            int8_t theme = loadTheme();
            if (pcConnected)
            {
                display_set_page(theme == 1 ? 5 : 1);
            }
            else
            {
                display_set_page(theme == 1 ? 21 : 18);
            }
        }
        /* Subsequent 5% drops */
        else if (soc <= 30 && soc <= (lastAlertSOC - 5))
        {
            lastAlertSOC = soc - (soc % 5);
            display_set_page(12); // SOC warning alert

            vTaskDelay(pdMS_TO_TICKS(2000));

            int8_t theme = loadTheme();
            if (pcConnected)
            {
                display_set_page(theme == 1 ? 5 : 1);
            }
            else
            {
                display_set_page(theme == 1 ? 21 : 18);
            }
        }
    }

    prevSOC = soc;
}

static void bmsTask(void *arg)
{

    while (1)
    {
        bool pack_ok = getPackMeasurements();
        if (pack_ok)
        {
            updatePackMeasurementsOnHMI(g_pack.pack_voltage, g_pack.pack_current, g_pack.pack_soc);
            handleSOCLogic((int)g_pack.pack_soc);
            if (pcConnected)
            {
                pc_send_pack_data(g_pack.pack_voltage,
                                  g_pack.pack_current,
                                  g_pack.pack_soc);
            }
            // printf("[PACK] V=%.1fV I=%.1fA SOC=%.1f%%\n",
            //        g_pack.pack_voltage,
            //        g_pack.pack_current,
            //        g_pack.pack_soc);
        }
        else
        {
            display_set_text(0x2000, "--     ");
            display_set_text(0x6000, "--     ");
            display_set_text(0x4000, "--     ");
            display_set_text(0x3100, "--     ");
            if (pcConnected)
            {
                pc_send_pack_invalid();
            }
            // printf("[PACK] read failed\n");
        }

        vTaskDelay(pdMS_TO_TICKS(50)); // Daly mandatory gap

        bool temp_ok = getPackTemp();
        if (temp_ok)
        {
            updatePackTempOnHMI(g_temp.min_temp, g_temp.max_temp, g_temp.avg_temp);
            if (pcConnected)
            {
                pc_send_temp_data(g_temp.min_temp,
                                  g_temp.max_temp,
                                  g_temp.avg_temp);
            }
            // printf("[TEMP] Tmax=%dC Tmin=%dC Tavg=%.1fC\n",
            //        g_temp.max_temp,
            //        g_temp.min_temp,
            //        g_temp.avg_temp);
        }
        else
        {
            display_set_text(0x7000, "--     ");
            display_set_text(0x8000, "--     ");
            display_set_text(0x3000, "--     ");
            if (pcConnected)
            {
                pc_send_temp_invalid();
            }
            // printf("[TEMP] read failed\n");
        }

        if (motorLockedLowSOC)
        {
            vTaskDelay(pdMS_TO_TICKS(1000)); // 1 sec when SOC < 3%
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(60000)); // 60 sec normal
        }
    }
}

void start_bms_task()
{
    xTaskCreate(bmsTask, "BMS_task", 4096, NULL, 4, NULL);
}