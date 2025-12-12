#include "stdint.h"
#include "stdio.h"

void nvs_init();

void saveTheme(int8_t theme);
int8_t loadTheme(void);

void save_preset(int8_t id, float value);
float load_preset(int8_t id);