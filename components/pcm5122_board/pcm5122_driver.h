#ifndef __PCM5122_H__
#define __PCM5122_H__

#include "audio_hal.h"
#include "driver/i2c.h"
#include "esp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCM5122_ADDR 0x4C // Default I2C address when A1/A2 are low

// Selected PCM5122 Registers
#define PCM5122_PAGE 0x00
#define PCM5122_RESET 0x01
#define PCM5122_STANDBY 0x02
#define PCM5122_MUTE 0x03
#define PCM5122_PLL_EN 0x04
#define PCM5122_SPI_MISO_FUNC 0x08
#define PCM5122_BCK_LRCK_CFG 0x09
#define PCM5122_DSP_CLK_DIV 0x14
#define PCM5122_DAC_CLK_DIV 0x15
#define PCM5122_NCP_CLK_DIV 0x16
#define PCM5122_OSR_CLK_DIV 0x17
#define PCM5122_VOL_L 0x3D
#define PCM5122_VOL_R 0x3E

esp_err_t pcm5122_init(audio_hal_codec_config_t *cfg);
esp_err_t pcm5122_deinit(void);

esp_err_t pcm5122_ctrl_state(audio_hal_codec_mode_t mode,
                             audio_hal_ctrl_t ctrl_state);
esp_err_t pcm5122_config_i2s(audio_hal_codec_mode_t mode,
                             audio_hal_codec_i2s_iface_t *iface);

esp_err_t pcm5122_set_voice_volume(int volume);
esp_err_t pcm5122_get_voice_volume(int *volume);

esp_err_t pcm5122_set_voice_mute(bool enable);
esp_err_t pcm5122_get_voice_mute(void);

esp_err_t pcm5122_pa_power(bool enable);

#ifdef __cplusplus
}
#endif

#endif //__PCM5122_H__
