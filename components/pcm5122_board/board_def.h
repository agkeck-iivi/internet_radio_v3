#ifndef _AUDIO_BOARD_DEFINITION_H_
#define _AUDIO_BOARD_DEFINITION_H_

#include "audio_hal.h"

#define FUNC_SDCARD_EN (0)
#define SDCARD_OPEN_FILE_NUM_MAX 5
#define SDCARD_PWR_CTRL -1
#define ESP_SD_PIN_CLK -1
#define ESP_SD_PIN_CMD -1
#define ESP_SD_PIN_D0 -1
#define ESP_SD_PIN_D1 -1
#define ESP_SD_PIN_D2 -1
#define ESP_SD_PIN_D3 -1
#define ESP_SD_PIN_D4 -1
#define ESP_SD_PIN_D5 -1
#define ESP_SD_PIN_D6 -1
#define ESP_SD_PIN_D7 -1
#define ESP_SD_PIN_CD -1
#define ESP_SD_PIN_WP -1

#define FUNC_SYS_LEN_EN (0)
#define GREEN_LED_GPIO -1

#define FUNC_AUDIO_CODEC_EN (0)
#define AUXIN_DETECT_GPIO -1
#define HEADPHONE_DETECT -1
#define PA_ENABLE_GPIO -1
#define CODEC_ADC_I2S_PORT ((i2s_port_t)0)
#define CODEC_ADC_BITS_PER_SAMPLE ((i2s_data_bit_width_t)16)
#define CODEC_ADC_SAMPLE_RATE (48000)
#define RECORD_HARDWARE_AEC (false)
#define BOARD_PA_GAIN (10)

#define AUDIO_ADC_INPUT_CH_FORMAT "N"

extern audio_hal_func_t MY_AUDIO_CODEC_PCM5122_DEFAULT_HANDLE;
#define AUDIO_CODEC_DEFAULT_CONFIG()                                           \
  {                                                                            \
      .adc_input = AUDIO_HAL_ADC_INPUT_LINE1,                                  \
      .dac_output = AUDIO_HAL_DAC_OUTPUT_ALL,                                  \
      .codec_mode = AUDIO_HAL_CODEC_MODE_DECODE,                               \
      .i2s_iface =                                                             \
          {                                                                    \
              .mode = AUDIO_HAL_MODE_SLAVE,                                    \
              .fmt = AUDIO_HAL_I2S_NORMAL,                                     \
              .samples = AUDIO_HAL_48K_SAMPLES,                                \
              .bits = AUDIO_HAL_BIT_LENGTH_16BITS,                             \
          },                                                                   \
  };

#define FUNC_BUTTON_EN (1)
#define INPUT_KEY_NUM 6
#define BUTTON_REC_ID -1
#define BUTTON_MODE_ID -1
#define BUTTON_SET_ID -1
#define BUTTON_PLAY_ID -1
#define BUTTON_VOLUP_ID -1
#define BUTTON_VOLDOWN_ID -1

#define INPUT_KEY_DEFAULT_INFO()                                               \
  {                                                                            \
  }

#endif
