
#include "dist.h"
#include "motorControl.h"

float current_height_mm = 0; // live height from sensor

float top_limit_mm = 0;
float bottom_limit_mm = 0;
float center_mm = 0;

void adc_init()
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_2, ADC_ATTEN_DB_11);
}

int8_t hit_bottom_limit()
{
    if (gpio_get_level(PROXIMITY_BOTTOM))
        return 1;

    if ((current_height_mm <= (bottom_limit_mm + 0.5f)) && !calibrating )   // 0.5mm tolerance
        return 1;

    if (current_height_mm <= 3.0f && !initial_calib)   // Hard-coded max height
        return 1;
    
    return 0;
}

int8_t hit_top_limit()
{
    if (gpio_get_level(PROXIMITY_TOP) )
        return 1;

    
    if ((current_height_mm >= (top_limit_mm - 0.1f)) && !calibrating)   // 0.1mm tolerance
        return 1;

    if (current_height_mm >= 20.0f)   // Hard-coded max height
        return 1;

    return 0;
}

float median_filter(float *v, int n)
{
    // simple bubble sort
    for (int i = 0; i < n - 1; i++)
        for (int j = i + 1; j < n; j++)
            if (v[j] < v[i]) {
                float tmp = v[i];
                v[i] = v[j];
                v[j] = tmp;
            }

    return v[n / 2];  // return median
}

float smooth(float input)
{
    static float smooth_height = 0.0f;
    static bool initialized = false;

    if (!initialized) {
        smooth_height = input;   // start from real reading
        initialized = true;
    } else {
        smooth_height = 0.7f * smooth_height + 0.3f * input;
    }

    return smooth_height;
}

void read_distance_mm()
{
    const int SAMPLE_COUNT = 11;   // 7 samples for median
    float samples[SAMPLE_COUNT];

    for (int i = 0; i < SAMPLE_COUNT; i++)
    {
        // Read ADC raw
        uint32_t raw = adc1_get_raw(IR_ADC_CHANNEL);

        // Convert to voltage
        float voltage = (raw / 4095.0f) * 3.3f;

        if (voltage < 0.1f) {
            samples[i] = -1; // invalid
        } else {
            float distance_cm = 12.08f * powf(voltage, -1.058f);

            distance_cm -= 0.20f;  // calibration offset (adjust if needed)

            samples[i] = distance_cm;
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // Step 1: median filter removes outliers
    float median_value = median_filter(samples, SAMPLE_COUNT);

    // Step 2: smooth (low-pass filter)
    float stable_value = smooth(median_value);
    stable_value = roundf(stable_value * 10.0f) / 10.0f;
    current_height_mm = stable_value;
}


void distance_task(void *arg)
{

    while (1)
    {
        read_distance_mm();

        // Optional debug print
        // printf("Height = %.1f mm\n", current_height_mm);

        vTaskDelay(pdMS_TO_TICKS(200)); // 20 Hz update rate
    }
}

void start_distance_task()
{
    xTaskCreate(distance_task, "distance_task", 2048, NULL, 5, NULL);
}