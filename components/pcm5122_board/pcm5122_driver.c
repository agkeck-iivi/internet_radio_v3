#include "pcm5122_driver.h"
#include "audio_volume.h"
#include "board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "app_config.h"
#include <math.h>
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

audio_hal_func_t MY_AUDIO_CODEC_PCM5122_DEFAULT_HANDLE = {
    .audio_codec_initialize = pcm5122_init,
    .audio_codec_deinitialize = pcm5122_deinit,
    .audio_codec_ctrl = pcm5122_ctrl_state,
    .audio_codec_config_iface = pcm5122_config_i2s,
    .audio_codec_set_mute = pcm5122_set_mute,
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
  if (res != 0) {
    ESP_LOGE(PCM_TAG, "getting i2c pins error");
    return -1;
  }
  i2c_handle = i2c_bus_create(I2C_NUM_0, &pcm_i2c_cfg);
  return res;
}

esp_err_t pcm5122_init(audio_hal_codec_config_t *cfg) {
  int res = 0;

  res = i2c_init();
  if (res != ESP_OK)
    return res;

  // MINIMAL SILENT BOOT FIX: Hardware-mute as the very 1st command
  res |= pcm5122_write_reg(PCM5122_MUTE, 0x11);

  ESP_LOGI(PCM_TAG, "Performing robust initialization...");

  res |= pcm5122_write_reg(PCM5122_PAGE, 0x00);    // Page 0
  res |= pcm5122_write_reg(PCM5122_STANDBY, 0x10); // Enter Standby (RQST bit)

  vTaskDelay(pdMS_TO_TICKS(10));
  res |= pcm5122_write_reg(PCM5122_RESET, 0x01); // Reset
  vTaskDelay(pdMS_TO_TICKS(10));

  // Reset Modules
  res |= pcm5122_write_reg(PCM5122_RESET, 0x10);
  uint8_t reset_state = 0x10;
  for (int i = 0; i < 10 && reset_state != 0; i++) {
    vTaskDelay(pdMS_TO_TICKS(10));
    pcm_read_reg(PCM5122_RESET, &reset_state);
    reset_state &= 0x10;
  }

  // Ensure hardware mute is set BEFORE exiting standby
  res |= pcm5122_write_reg(PCM5122_MUTE, 0x11);
  // Exit Standby and Powerdown
  res |= pcm5122_write_reg(PCM5122_STANDBY, 0x00);

  // Configure clock inference and error ignore (System default)
  res |= pcm5122_write_reg(PCM5122_ERROR_DETECT, 0x7D);

  // Manual Clock Config: REQUIRED because I2S_MCK_GPIO is not used on this board
  res |= pcm5122_write_reg(PCM5122_PLL_EN, 0x01);      // Enable PLL
  res |= pcm5122_write_reg(PCM5122_PLL_REF, 0x10);     // PLL Ref = BCK (0x10)
  res |= pcm5122_write_reg(PCM5122_DAC_CLK_SRC, 0x10); // DAC Src = PLL (0x10)

  // Configure I2S based on cfg
  res |= pcm5122_config_i2s(cfg->codec_mode, &cfg->i2s_iface);

  // Ensure volume linkage is disabled
  res |= pcm5122_write_reg(PCM5122_DIGITAL_VOL_CTRL, 0x00);

  // Disable Auto Mute
  res |= pcm5122_write_reg(PCM5122_AUTO_MUTE, 0x00);

  // Set default volume
  codec_dac_volume_config_t vol_cfg = PCM5122_DAC_VOL_CFG_DEFAULT();
  dac_vol_handle = audio_codec_volume_init(&vol_cfg);

  if (res != ESP_OK) {
    ESP_LOGE(PCM_TAG, "PCM5122 init failed");
  } else {
    ESP_LOGI(PCM_TAG, "PCM5122 init complete (MINIMAL SILENT BOOT)");
  }
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
    return pcm5122_set_mute(true);
  } else {
    // SILENT START: Keep it muted on start; app_main will unmute later
    ESP_LOGI(PCM_TAG, "PCM5122 START: Keeping muted for app-controlled transition");
    return ESP_OK;
  }
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

  esp_err_t res = pcm5122_write_reg(PCM5122_I2S_CONFIG, format_val);
  return res;
}

// PCM5122 volume goes from 0x00 (+24dB) to 0xFF (-103dB) in 0.5dB steps
// Let's implement a simple 0-100 linear mapping for now
esp_err_t pcm5122_set_voice_volume(int volume) {
  if (volume < 0)
    volume = 0;
  if (volume > 100)
    volume = 100;

  ESP_LOGI(PCM_TAG, "Setting volume to %d%%", volume);

  // Map 0-100% to natural logarithmic curve
  // base_reg = 48 (0dB) + g_digital_attenuation
  // This adds a runtime adjustable digital attenuation to the curve.
  uint8_t reg_val;
  if (volume <= 0) {
    reg_val = 255; // Muted
  } else {
    reg_val = (uint8_t)(48 + g_runtime_config.digital_attenuation +
                        80 * log10(100.0 / volume));
    if (reg_val > 255)
      reg_val = 255;
  }

  esp_err_t res = pcm5122_write_reg(PCM5122_PAGE, 0x00);
  if (res != ESP_OK)
    return res;

  res = pcm5122_write_reg(PCM5122_VOL_L, reg_val);
  if (res != ESP_OK) {
    ESP_LOGE(PCM_TAG, "Failed to set Left Volume: %s", esp_err_to_name(res));
    return res;
  }

  res = pcm5122_write_reg(PCM5122_VOL_R, reg_val);
  if (res != ESP_OK) {
    ESP_LOGE(PCM_TAG, "Failed to set Right Volume: %s", esp_err_to_name(res));
    return res;
  }

  if (dac_vol_handle) {
    dac_vol_handle->user_volume = volume;
  }

  ESP_LOGI(PCM_TAG, "Volume set successfully to %d%% (reg: 0x%02X)", volume,
           reg_val);
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

esp_err_t pcm5122_set_mute(bool enable) {
  ESP_LOGI(PCM_TAG, "Setting hardware mute: %s", enable ? "ON" : "OFF");
  esp_err_t res = pcm5122_write_reg(PCM5122_PAGE, 0x00);
  if (res != ESP_OK)
    return res;
  // Reg 0x06: Bits 0 (Left) and 4 (Right)
  res = pcm5122_write_reg(PCM5122_MUTE, enable ? 0x11 : 0x00);
  if (res != ESP_OK) {
    ESP_LOGE(PCM_TAG, "Failed to set mute: %s", esp_err_to_name(res));
  }
  return res;
}

esp_err_t pcm5122_apply_analog_attenuation(void) {
  esp_err_t res = pcm5122_write_reg(PCM5122_PAGE, 0x01); // Switch to Page 1
  uint8_t atten_val =
      (g_runtime_config.analog_attenuation == PCM5122_ANALOG_ATTEN_6DB) ? 0x11 : 0x00;
  res |= pcm5122_write_reg(0x02, atten_val);
  res |= pcm5122_write_reg(PCM5122_PAGE, 0x00); // Switch back to Page 0
  ESP_LOGI(PCM_TAG, "Analog attenuation applied: %s",
           (atten_val == 0x11) ? "-6dB" : "0dB");
  return res;
}

esp_err_t pcm5122_get_mute(bool *mute) {
  uint8_t data;
  pcm5122_write_reg(PCM5122_PAGE, 0x00);
  esp_err_t res = pcm_read_reg(PCM5122_MUTE, &data);
  if (res == ESP_OK) {
    // PCM5122_MUTE (Reg 3): Bit 4 is for Right, Bit 0 for Left
    // 1: Mute, 0: Unmute
    *mute = (data & 0x11) != 0;
  }
  return res;
}

esp_err_t pcm5122_pa_power(bool enable) { return ESP_OK; }
