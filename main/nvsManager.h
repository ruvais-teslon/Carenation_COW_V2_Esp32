#include "stdint.h"
#include "stdio.h"

void nvs_init();

void saveTheme(int8_t theme);
int8_t loadTheme(void);

void savePreset(int8_t id, float value);
float loadPreset(int8_t id);

void save_limit(const char *key, float value);
float load_limit(const char *key);

void saveDevicename(const char *name);
bool loadDevicename(char *buffer, size_t buffer_size);