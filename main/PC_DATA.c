#include "PC_DATA.h"
#include "DWIN_HMI.h"

#define BUF_SIZE 256
#define FRAME_MAX_LEN 32
#define DISP_NAME_LEN 20

static uint8_t rx_buf[BUF_SIZE];
static char strbuf[BUF_SIZE];

bool pcConnected = false;
bool pcJustConnected = 0;

const unsigned long PC_READ_INTERVAL_MS = 60000;
static uint32_t lastPCReadTime = 0;

void display_device_name(uint16_t vp, const char *name)
{
    char buf[DISP_NAME_LEN + 1];

    memset(buf, ' ', DISP_NAME_LEN); // fill with spaces

    size_t len = strlen(name);
    if (len > DISP_NAME_LEN)
        len = DISP_NAME_LEN;

    memcpy(buf, name, len);

    buf[DISP_NAME_LEN] = '\0';
    display_set_text(vp, buf);
}

void updateBinaryDataOnHMI(uint8_t cpu, uint8_t cpuSpeed, uint8_t ramUsed,
                           uint8_t ramPercent, uint8_t ramTotal, uint8_t diskUsed,
                           uint8_t diskPercent, uint8_t diskTotal)
{
    char buf[6];

    snprintf(buf, sizeof(buf), "%-5u", cpu);
    display_set_text(0x9000, buf);

    snprintf(buf, sizeof(buf), "%-5u", cpuSpeed);
    display_set_text(0x1100, buf);

    snprintf(buf, sizeof(buf), "%-5u", ramUsed);
    display_set_text(0x1400, buf);

    snprintf(buf, sizeof(buf), "%-5u", ramPercent);
    display_set_text(0x1200, buf);

    snprintf(buf, sizeof(buf), "%-5u", ramTotal);
    display_set_text(0x1500, buf);

    snprintf(buf, sizeof(buf), "%-5u", diskUsed);
    display_set_text(0x1600, buf);

    snprintf(buf, sizeof(buf), "%-5u", diskPercent);
    display_set_text(0x1300, buf);

    snprintf(buf, sizeof(buf), "%-5u", diskTotal);
    display_set_text(0x1700, buf);
}

static void parseBinaryData(uint8_t *rx, int len)
{
    static rx_state_t state = RX_WAIT_MARKER;
    static uint8_t frame[FRAME_MAX_LEN];
    static uint8_t index = 0;
    static uint8_t expected_len = 0;
    static uint8_t marker = 0;

    uint32_t now = esp_timer_get_time() / 1000;

    for (int i = 0; i < len; i++)
    {
        uint8_t byte = rx[i];

        switch (state)
        {
        case RX_WAIT_MARKER:
            if (byte == SYSTEM_METRICS_MARKER)
            {
                // printf("[PARSE] SYSTEM_METRICS_MARKER detected\n");
                marker = byte;
                expected_len = SYS_METRICS_LEN;
                index = 0;
                frame[index++] = byte;
                state = RX_COLLECT_FRAME;
            }
            else if (byte == DEVICE_NAME_MARKER)
            {
                // printf("[PARSE] DEVICE_NAME_MARKER detected\n");
                marker = byte;
                expected_len = DEV_NAME_LEN;
                index = 0;
                frame[index++] = byte;
                state = RX_COLLECT_FRAME;
            }
            break;

        case RX_COLLECT_FRAME:
            if (index >= FRAME_MAX_LEN || index >= expected_len)
            {
                // Overflow or protocol error → reset
                state = RX_WAIT_MARKER;
                index = 0;
                break;
            }

            frame[index++] = byte;

            if (index == expected_len)
            {
                // ---------- CHECKSUM ----------
                uint8_t checksum = 0;
                for (int j = 0; j < expected_len - 1; j++)
                    checksum += frame[j];

                if (checksum == frame[expected_len - 1])
                {
                    pcConnected = true;
                    pcJustConnected = true;
                    lastPCReadTime = now;

                    /* ---- SYSTEM METRICS ---- */
                    if (marker == SYSTEM_METRICS_MARKER)
                    {
                        updateBinaryDataOnHMI(
                            frame[1], frame[2], frame[3], frame[4],
                            frame[5], frame[6], frame[7], frame[8]);
                    }

                    /* ---- DEVICE NAME ---- */
                    else if (marker == DEVICE_NAME_MARKER)
                    {
                        char newName[21];
                        memcpy(newName, &frame[1], 20);
                        newName[20] = '\0';
                        // printf("[PARSE] Device name received: \"%s\"\n", newName);
                        char storedName[21];
                        bool hasStoredName = loadDevicename(storedName, sizeof(storedName));

                        if (!hasStoredName || strcmp(newName, storedName) != 0)
                        {
                            saveDevicename(newName);
                        }

                        display_device_name(0x1800, newName);
                    }
                }

                // Reset for next frame
                state = RX_WAIT_MARKER;
                index = 0;
            }
            break;
        }
    }
}

void updatePackMeasurementsOnPC(float voltage, float current, float soc)
{
    if (!pcConnected)
        return;

    char bmsData[128];

    snprintf(bmsData, sizeof(bmsData),
             "basic,%.2f,%.2f,%.2f\r\n",
             voltage, current, soc);

    uart_write_bytes(PC_UART, bmsData, strlen(bmsData));
}

void updatePackTempOnPC(int tempMin, int tempMax, float tempAverage)
{
    if (!pcConnected)
        return;

    char tempData[128];

    snprintf(tempData, sizeof(tempData),
             "temp,%d,%d,%.2f\r\n",
             tempMin, tempMax, tempAverage);

    uart_write_bytes(PC_UART, tempData, strlen(tempData));
}

void pc_send_pack_data(float voltage, float current, float soc)
{
    if (pcQueue == NULL)
        return;

    pc_msg_t msg = {
        .type = PC_MSG_PACK,
        .pack = {
            .voltage = voltage,
            .current = current,
            .soc = soc}};

    xQueueSend(pcQueue, &msg, 0);
}

void pc_send_temp_data(int min_temp, int max_temp, float avg_temp)
{
    if (pcQueue == NULL)
        return;

    pc_msg_t msg = {
        .type = PC_MSG_TEMP,
        .temp = {
            .min_temp = min_temp,
            .max_temp = max_temp,
            .avg_temp = avg_temp}};

    xQueueSend(pcQueue, &msg, 0);
}

void pc_send_pack_invalid(void)
{
    if (pcQueue == NULL)
        return;

    pc_msg_t msg = {
        .type = PC_MSG_PACK_INVALID};

    xQueueSend(pcQueue, &msg, 0);
}

void pc_send_temp_invalid(void)
{
    if (pcQueue == NULL)
        return;

    pc_msg_t msg = {
        .type = PC_MSG_TEMP_INVALID};

    xQueueSend(pcQueue, &msg, 0);
}

static void pcTask(void *arg)
{

    const char *query = "ESP32_ID_QUERY";
    const char *reply = "ESP32-S3-IDENTIFIED\r\n";

    while (1)
    {
        int len = uart_read_bytes(PC_UART, rx_buf, sizeof(rx_buf),
                                  pdMS_TO_TICKS(100));

        if (len > 0)
        {
            /* ---- Binary protocol ---- */
            parseBinaryData(rx_buf, len);

            /* ---- Handshake (string-safe copy) ---- */
            int cpy = (len < BUF_SIZE - 1) ? len : BUF_SIZE - 1;
            memcpy(strbuf, rx_buf, cpy);
            strbuf[cpy] = '\0';

            if (strstr(strbuf, query))
            {
                uart_write_bytes(PC_UART, reply, strlen(reply));
            }
        }

        pc_msg_t msg;
        while (xQueueReceive(pcQueue, &msg, 0)) // NON-blocking
        {
            switch (msg.type)
            {
            case PC_MSG_PACK:
                updatePackMeasurementsOnPC(msg.pack.voltage,
                                           msg.pack.current,
                                           msg.pack.soc);
                break;

            case PC_MSG_TEMP:
                updatePackTempOnPC(msg.temp.min_temp,
                                   msg.temp.max_temp,
                                   msg.temp.avg_temp);
                break;

            case PC_MSG_PACK_INVALID:
                    uart_write_bytes(PC_UART,"basic,--,--,--\r\n",
                                     strlen("basic,--,--,--\r\n"));
                break;

            case PC_MSG_TEMP_INVALID:
                    uart_write_bytes(PC_UART,"temp,--,--,--\r\n",
                                     strlen("temp,--,--,--\r\n"));
                break;
            }
        }

        uint32_t now = esp_timer_get_time() / 1000;

        if (pcConnected && (now - lastPCReadTime > PC_READ_INTERVAL_MS * 2))
        {
            pcConnected = false;
            int8_t theme = loadTheme();
            display_set_page(theme == 1 ? 21 : 18);
        }

        if (pcJustConnected && !calibrating)
        {
            pcJustConnected = false;
            int8_t theme = loadTheme();
            display_set_page(theme == 1 ? 5 : 1);
        }
    }
}

void start_pc_task()
{
    xTaskCreate(pcTask, "pc_task", 4096, NULL, 4, NULL);
}
