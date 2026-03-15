#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Power save modes for the Internet Radio.
 */
typedef enum {
  POWER_SAVE_NONE,
  POWER_SAVE_LIGHT_ONLY,
  POWER_SAVE_LIGHT_DEEP
} power_save_mode_t;

/**
 * @brief PCM5122 Analog Attenuation Options.
 */
typedef enum {
  PCM5122_ANALOG_ATTEN_0DB = 0, // 2Vrms (0dB analog gain)
  PCM5122_ANALOG_ATTEN_6DB = 1, // 1Vrms (-6dB analog gain)
} pcm5122_analog_atten_t;

/**
 * @brief PCM5122 Digital Attenuation Options.
 */
typedef enum {
  PCM5122_DIGITAL_ATTEN_0DB = 0,
  PCM5122_DIGITAL_ATTEN_3DB = 6,   // 3dB * 2 steps/dB = 6
  PCM5122_DIGITAL_ATTEN_6DB = 12,  // 6dB * 2 steps/dB = 12
  PCM5122_DIGITAL_ATTEN_9DB = 18,
  PCM5122_DIGITAL_ATTEN_12DB = 24,
  PCM5122_DIGITAL_ATTEN_15DB = 30,
  PCM5122_DIGITAL_ATTEN_18DB = 36,
  PCM5122_DIGITAL_ATTEN_21DB = 42,
  PCM5122_DIGITAL_ATTEN_24DB = 48,
} pcm5122_digital_atten_t;

/**
 * @brief Consolidated runtime configuration structure.
 */
typedef struct {
  pcm5122_analog_atten_t analog_attenuation;
  pcm5122_digital_atten_t digital_attenuation;
  power_save_mode_t power_save_mode;
  uint32_t light_sleep_delay_ms;
  uint32_t deep_sleep_delay_ms;
  bool ir_is_enabled;
} app_runtime_config_t;

extern app_runtime_config_t g_runtime_config;

#endif // APP_CONFIG_H
