#include "pcm5122_driver.h"
#include "audio_volume.h"
#include "board.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include <string.h>

static const char *PCM_TAG = "PCM5122_DRIVER";
static i2c_bus_handle_t i2c_handle;
static codec_dac_volume_config_t *dac_vol_handle;

#define PCM5122_DAC_VOL_CFG_DEFAULT()                                          \
  {                                                                            \
      .max_dac_volume = 24,                                                    \
      .min_dac_volume = -103,                                                  \
      .board_pa_gain = BOARD_PA_GAIN,                                          \
      .volume_accuracy = 0.5,                                                  \
      .dac_vol_symbol = -1,                                                    \
      .zero_volume_reg = 0,                                                    \
      .reg_value = 0,                                                          \
      .user_volume = 0,                                                        \
      .offset_conv_volume = NULL,                                              \
  }

#define PCM_ASSERT(a, format, b, ...)                                          \
  if ((a) != 0) {                                                              \
    ESP_LOGE(PCM_TAG, format, ##__VA_ARGS__);                                  \
    return b;                                                                  \
  }

audio_hal_func_t MY_AUDIO_CODEC_PCM5122_DEFAULT_HANDLE = {
    .audio_codec_initialize = pcm5122_init,
    .audio_codec_deinitialize = pcm5122_deinit,
    .audio_codec_ctrl = pcm5122_ctrl_state,
    .audio_codec_config_iface = pcm5122_config_i2s,
    .audio_codec_set_mute = pcm5122_set_voice_mute,
    .audio_codec_set_volume = pcm5122_set_voice_volume,
    .audio_codec_get_volume = pcm5122_get_voice_volume,
    .audio_codec_enable_pa = pcm5122_pa_power,
    .audio_hal_lock = NULL,
    .handle = NULL,
};

static esp_err_t pcm_write_reg(uint8_t slave_addr, uint8_t reg_add,
                               uint8_t data) {
  return i2c_bus_write_bytes(i2c_handle, slave_addr, &reg_add, sizeof(reg_add),
                             &data, sizeof(data));
}

static esp_err_t pcm_read_reg(uint8_t reg_add, uint8_t *p_data) {
  return i2c_bus_read_bytes(i2c_handle, PCM5122_ADDR, &reg_add, sizeof(reg_add),
                            p_data, 1);
}

static esp_err_t pcm5122_write_reg(uint8_t reg_add, uint8_t data) {
  return pcm_write_reg(PCM5122_ADDR, reg_add, data);
}

static int i2c_init() {
  int res;
  i2c_config_t pcm_i2c_cfg = {.mode = I2C_MODE_MASTER,
                              .sda_pullup_en = GPIO_PULLUP_ENABLE,
                              .scl_pullup_en = GPIO_PULLUP_ENABLE,
                              .master.clk_speed = 100000};
  res = get_i2c_pins(I2C_NUM_0, &pcm_i2c_cfg);
  PCM_ASSERT(res, "getting i2c pins error", -1);
  i2c_handle = i2c_bus_create(I2C_NUM_0, &pcm_i2c_cfg);
  return res;
}

esp_err_t pcm5122_init(audio_hal_codec_config_t *cfg) {
  int res = 0;

  res = i2c_init();

  // Basic init sequence for PCM5122 (Wake up from standby)
  res |= pcm5122_write_reg(PCM5122_PAGE, 0x00);    // Page 0
  res |= pcm5122_write_reg(PCM5122_STANDBY, 0x00); // Wake up
  res |= pcm5122_write_reg(PCM5122_MUTE, 0x00);    // Unmute

  // Configure I2S based on cfg
  res |= pcm5122_config_i2s(cfg->codec_mode, &cfg->i2s_iface);

  // Set default volume
  codec_dac_volume_config_t vol_cfg = PCM5122_DAC_VOL_CFG_DEFAULT();
  dac_vol_handle = audio_codec_volume_init(&vol_cfg);
  pcm5122_set_voice_volume(80); // Set a reasonable default

  ESP_LOGI(PCM_TAG, "PCM5122 init complete");
  return res == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t pcm5122_deinit(void) {
  pcm5122_write_reg(PCM5122_PAGE, 0x00);
  pcm5122_write_reg(PCM5122_STANDBY, 0x11); // Enter standby
  i2c_bus_delete(i2c_handle);
  audio_codec_volume_deinit(dac_vol_handle);
  return ESP_OK;
}

esp_err_t pcm5122_ctrl_state(audio_hal_codec_mode_t mode,
                             audio_hal_ctrl_t ctrl_state) {
  if (mode != AUDIO_HAL_CODEC_MODE_DECODE &&
      mode != AUDIO_HAL_CODEC_MODE_BOTH) {
    ESP_LOGW(PCM_TAG, "PCM5122 only supports DAC mode");
    return ESP_FAIL;
  }

  if (ctrl_state == AUDIO_HAL_CTRL_STOP) {
    // Soft mute
    pcm5122_write_reg(PCM5122_PAGE, 0x00);
    pcm5122_write_reg(PCM5122_MUTE, 0x11);
  } else {
    // Unmute
    pcm5122_write_reg(PCM5122_PAGE, 0x00);
    pcm5122_write_reg(PCM5122_MUTE, 0x00);
  }
  return ESP_OK;
}

esp_err_t pcm5122_config_i2s(audio_hal_codec_mode_t mode,
                             audio_hal_codec_i2s_iface_t *iface) {
  uint8_t format_val = 0;
  // PCM5122 Reg 9: I2S config
  // 00: I2S, 01: Left justified, 10: Right justified
  // 00: 16-bit, 01: 20-bit, 10: 24-bit, 11: 32-bit

  // Default I2S is 0x00 for bits 4-5.
  if (iface->bits == AUDIO_HAL_BIT_LENGTH_16BITS) {
    format_val |= (0x00 << 4);
  } else if (iface->bits == AUDIO_HAL_BIT_LENGTH_24BITS) {
    format_val |= (0x02 << 4);
  } else if (iface->bits == AUDIO_HAL_BIT_LENGTH_32BITS) {
    format_val |= (0x03 << 4);
  }

  if (iface->fmt == AUDIO_HAL_I2S_NORMAL) {
    format_val |= 0x00;
  } else if (iface->fmt == AUDIO_HAL_I2S_LEFT) {
    format_val |= 0x01;
  } else if (iface->fmt == AUDIO_HAL_I2S_RIGHT) {
    format_val |= 0x02;
  }

  esp_err_t res = pcm5122_write_reg(PCM5122_BCK_LRCK_CFG, format_val);
  return res;
}

// PCM5122 volume goes from 0x00 (+24dB) to 0xFF (-103dB) in 0.5dB steps
// Let's implement a simple 0-100 linear mapping for now
esp_err_t pcm5122_set_voice_volume(int volume) {
  esp_err_t res = ESP_OK;
  if (volume < 0)
    volume = 0;
  if (volume > 100)
    volume = 100;

  // 100 -> 0dB (0x30 hex / 48 dec)
  // 0 -> -103dB (0xFF hex / 255 dec)
  // Simple mapping: Inverse relation
  uint8_t reg_val;
  if (volume == 0) {
    reg_val = 0xFF; // Muted effectively
  } else {
    // Map 1-100 to 0xEF down to 0x30 (approx mapping, 0x30 is 0dB, we probably
    // don't want positive gain) Range is roughly 255 (mute) to 48 (0db) -> 207
    // steps
    reg_val = (uint8_t)(255 - ((volume * 207) / 100));
    if (reg_val < 0x30)
      reg_val = 0x30; // Cap at 0dB to prevent distortion
  }

  res |= pcm5122_write_reg(PCM5122_PAGE, 0x00);
  res |= pcm5122_write_reg(PCM5122_VOL_L, reg_val);
  res |= pcm5122_write_reg(PCM5122_VOL_R, reg_val);

  if (dac_vol_handle) {
    dac_vol_handle->user_volume = volume;
  }

  return res;
}

esp_err_t pcm5122_get_voice_volume(int *volume) {
  if (dac_vol_handle) {
    *volume = dac_vol_handle->user_volume;
    return ESP_OK;
  }
  *volume = 0;
  return ESP_FAIL;
}

esp_err_t pcm5122_set_voice_mute(bool enable) {
  esp_err_t res = pcm5122_write_reg(PCM5122_PAGE, 0x00);
  if (enable) {
    res |= pcm5122_write_reg(PCM5122_MUTE, 0x11);
  } else {
    res |= pcm5122_write_reg(PCM5122_MUTE, 0x00);
  }
  return res;
}

esp_err_t pcm5122_get_voice_mute(void) { return ESP_OK; }

esp_err_t pcm5122_pa_power(bool enable) { return ESP_OK; }
