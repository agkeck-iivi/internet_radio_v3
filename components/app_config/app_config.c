#include "app_config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <stdbool.h>

static const char *TAG = "APP_CONFIG";
static const char *NVS_NAMESPACE = "app_config";

app_runtime_config_t g_runtime_config = {
    .analog_attenuation = PCM5122_ANALOG_ATTEN_6DB,
    .digital_attenuation = PCM5122_DIGITAL_ATTEN_3DB,
    .power_save_mode = POWER_SAVE_LIGHT_DEEP,
    .light_sleep_delay_ms = 20 * 60 * 1000,    // 20 minutes (prod default)
    .deep_sleep_delay_ms = 2 * 60 * 60 * 1000, // 2 hours
    .ir_is_enabled = false,
};

void load_app_config(void) {
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Error (%s) opening NVS handle for loading! Using defaults.",
             esp_err_to_name(err));
    return;
  }

  uint8_t u8_val;
  uint32_t u32_val;

  if (nvs_get_u8(nvs_handle, "anlg_attn", &u8_val) == ESP_OK) {
    g_runtime_config.analog_attenuation = (pcm5122_analog_atten_t)u8_val;
  }
  if (nvs_get_u8(nvs_handle, "digi_attn", &u8_val) == ESP_OK) {
    g_runtime_config.digital_attenuation = (pcm5122_digital_atten_t)u8_val;
  }
  if (nvs_get_u8(nvs_handle, "pwr_save", &u8_val) == ESP_OK) {
    g_runtime_config.power_save_mode = (power_save_mode_t)u8_val;
  }
  if (nvs_get_u32(nvs_handle, "light_dly", &u32_val) == ESP_OK) {
    g_runtime_config.light_sleep_delay_ms = u32_val;
  }
  if (nvs_get_u32(nvs_handle, "deep_dly", &u32_val) == ESP_OK) {
    g_runtime_config.deep_sleep_delay_ms = u32_val;
  }
  if (nvs_get_u8(nvs_handle, "ir_en", &u8_val) == ESP_OK) {
    g_runtime_config.ir_is_enabled = (u8_val != 0);
  }

  nvs_close(nvs_handle);
  ESP_LOGI(TAG, "Configuration loaded from NVS");
}

void save_app_config(void) {
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle for saving!",
             esp_err_to_name(err));
    return;
  }

  nvs_set_u8(nvs_handle, "anlg_attn", (uint8_t)g_runtime_config.analog_attenuation);
  nvs_set_u8(nvs_handle, "digi_attn", (uint8_t)g_runtime_config.digital_attenuation);
  nvs_set_u8(nvs_handle, "pwr_save", (uint8_t)g_runtime_config.power_save_mode);
  nvs_set_u32(nvs_handle, "light_dly", g_runtime_config.light_sleep_delay_ms);
  nvs_set_u32(nvs_handle, "deep_dly", g_runtime_config.deep_sleep_delay_ms);
  nvs_set_u8(nvs_handle, "ir_en", (uint8_t)g_runtime_config.ir_is_enabled);

  err = nvs_commit(nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) committing NVS updates!", esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "Configuration saved to NVS");
  }

  nvs_close(nvs_handle);
}
