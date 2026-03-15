#include "app_config.h"
#include <stdbool.h>

app_runtime_config_t g_runtime_config = {
    .analog_attenuation = PCM5122_ANALOG_ATTEN_6DB,
    .digital_attenuation = PCM5122_DIGITAL_ATTEN_18DB,
    .power_save_mode = POWER_SAVE_LIGHT_DEEP,
    .light_sleep_delay_ms = 20 * 60 * 1000, // 20 minutes (prod default)
    .deep_sleep_delay_ms = 2 * 60 * 60 * 1000, // 2 hours
    .ir_is_enabled = true,
};
