#include "nvs_flash.h"
#include "nvs.h"
#include "nvsManager.h"

void nvs_init()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
}

void saveTheme(int8_t theme)
{
    nvs_handle_t h;

    if (nvs_open("settings", NVS_READWRITE, &h) != ESP_OK)
    {
        printf("Failed to open NVS for saving theme\n");
        return;
    }

    nvs_set_i8(h, "theme", theme);
    nvs_commit(h);
    nvs_close(h);

    printf("Theme saved: %d\n", theme);
}

int8_t loadTheme(void)
{
    nvs_handle_t h;
    int8_t theme = 1; // default theme = 1

    esp_err_t err = nvs_open("settings", NVS_READONLY, &h);
    if (err != ESP_OK)
    {
        printf("Failed to open NVS for loading theme, using default\n");
        return 1; // default to theme 1
    }

    err = nvs_get_i8(h, "theme", &theme);

    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        printf("Theme not found in NVS, using default\n");
        theme = 1; // default
    }

    nvs_close(h);

    return theme;
}

void savePreset(int8_t id, float value)
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

float loadPreset(int8_t id)
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

