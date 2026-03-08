/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "encoders.h"
//  #include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
// #include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/rtc_io.h"

#include "audio_hal.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "nvs_flash.h" // this is needed even though linter suggests otherwise

#include <limits.h>

#include "board.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "internet_radio_adf.h"
#include "ir_remote.h"
#include "lvgl_ssd1306_setup.h"
#include "pcm5122_driver.h"
#include "screens.h"
#include "station_data.h"
#include <inttypes.h>

static const char *TAG = "encoders";

// extern audio_board_handle_t board_handle;

extern int station_count;
extern int current_station;
extern audio_pipeline_components_t audio_pipeline_components;

#define VOLUME_GPIO_A 42
#define VOLUME_GPIO_B 2
#define VOLUME_PRESS_GPIO 1

#define STATION_GPIO_A 39
#define STATION_GPIO_B 40
#define STATION_PRESS_GPIO 41

// polling periods
#define VOLUME_POLLING_PERIOD_MS 100
#define VOLUME_PRESS_POLLING_PERIOD_MS 20

#define STATION_POLLING_PERIOD_MS 100
#define STATION_PRESS_POLLING_PERIOD_MS 100

// this pause allows the user to change the station multiple times before the
// change takes effect
#define DELAY_BEFORE_STATION_CHANGE_MS 2000
// timing for long press on station switch to reboot
#define LONG_PRESS_TIME_MS 1500
// time to display IP address on screen
#define IP_SCREEN_DISPLAY_TIME_MS 3000
// reboot message is displayed for this time before rebooting
#define REBOOT_MESSAGE_DISPLAY_TIME_MS 100
// Time window to detect a second click for double-click actions
#define DOUBLE_CLICK_TIMEOUT_MS 300
// Set these to variables to allow future runtime configuration
// IFTT: If these variables change at runtime, ensure the logic in
// volume_press_task dynamically recalculates min_sleep_threshold_us.
uint32_t g_light_sleep_delay_ms = 10 * 1000;
uint32_t g_deep_sleep_delay_ms = 2 * 60 * 60 * 1000;

typedef enum {
  POWER_SAVE_NONE,
  POWER_SAVE_LIGHT_ONLY,
  POWER_SAVE_LIGHT_DEEP
} power_save_mode_t;

// Set the current power saving strategy as a variable for runtime adjustment
power_save_mode_t g_power_save_mode = POWER_SAVE_LIGHT_ONLY;
// Lockout period after wakeup to ignore ghost pulses (500ms)
#define WAKEUP_LOCKOUT_US (500 * 1000)

typedef struct {
  pcnt_unit_handle_t pcnt_unit;
  int speed;
  int value;
  int adjust;
  audio_board_handle_t board_handle;
} limited_pulse_counter_t;

typedef struct {
  pcnt_unit_handle_t pcnt_unit;
  int current_index;
  const int *values;
  int num_values;
} cyclic_pulse_counter_t;
static cyclic_pulse_counter_t *g_station_counter_ptr = NULL;

// Mute functionality state
static bool is_muted = false;
static int64_t mute_start_time = 0;
static limited_pulse_counter_t *g_volume_counter_ptr =
    NULL; // Pointer to the volume counter for shared access

static int64_t g_last_wakeup_time = 0;

static void save_volume_to_nvs(int volume) {
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle for writing volume!",
             esp_err_to_name(err));
    return;
  }

  err = nvs_set_i32(nvs_handle, "volume", volume);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) writing 'volume' to NVS!", esp_err_to_name(err));
  } else {
    ESP_LOGD(TAG, "Saved volume = %d to NVS", volume);
  }

  err = nvs_commit(nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) committing 'volume' to NVS!",
             esp_err_to_name(err));
  }

  nvs_close(nvs_handle);
}

static void save_mute_state_to_nvs(bool muted) {
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle for writing mute state!",
             esp_err_to_name(err));
    return;
  }

  err = nvs_set_u8(nvs_handle, "mute_state", muted ? 1 : 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) writing 'mute_state' to NVS!",
             esp_err_to_name(err));
  } else {
    ESP_LOGD(TAG, "Saved mute_state = %d to NVS", muted);
  }

  err = nvs_commit(nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) committing 'mute_state' to NVS!",
             esp_err_to_name(err));
  }

  nvs_close(nvs_handle);
}

// update the volume by clamping to range 0-100.  The adjust parameter is used
// to adapt the current pulse count to the clamped volume.  In this way one can
// adjust well below 0 but after ajustment it only take a click to get back
// above 0.
void update_volume_pulse_counter(void *pvParameters) {
  limited_pulse_counter_t *counter = (limited_pulse_counter_t *)pvParameters;
  g_volume_counter_ptr = counter; // Store pointer for global access

  // Restore initial volume to PCNT counter state
  // The counter is at 0, so we need to adjust it to match the initial volume
  int initial_count_offset =
      (counter->value - counter->adjust) * 4 / counter->speed;
  pcnt_unit_clear_count(counter->pcnt_unit);
  // This is a bit of a hack to set the counter. There's no direct 'set_count'
  if (initial_count_offset != 0) {
    // We can't directly set the count, but we can adjust our 'adjust' value
    // so that the next calculation is correct.
    // The formula is: new_volume = count / 4 * speed + adjust
    // We want new_volume to be initial_volume when count is 0.
    counter->adjust = counter->value;
  }

  for (;;) {
    int count;
    ESP_ERROR_CHECK(pcnt_unit_get_count(counter->pcnt_unit, &count));
    int new_volume = count / 4 * counter->speed + counter->adjust;
    if (new_volume < 0) {
      counter->adjust = -count / 4 * counter->speed;
      new_volume = 0;
    } else if (new_volume > 100) {
      counter->adjust = 100 - count / 4 * counter->speed;
      new_volume = 100;
    }
    if (new_volume != counter->value) {
      // Check for lockout period after wakeup
      if (esp_timer_get_time() - g_last_wakeup_time < WAKEUP_LOCKOUT_US) {
        ESP_LOGI(TAG, "Ghost volume pulse ignored (lockout): %d -> %d",
                 counter->value, new_volume);
        counter->value = new_volume;
      } else {
        // If muted and user changes volume, unmute first
        audio_hal_set_mute(counter->board_handle->audio_hal, false);
        is_muted = false;
        ESP_LOGI(TAG, "Unmuted by volume change to %d", new_volume);
        update_mute_state(false);
        save_mute_state_to_nvs(false);

        counter->value = new_volume;
        audio_hal_set_volume(counter->board_handle->audio_hal, new_volume);
        update_volume_slider(new_volume);
        save_volume_to_nvs(new_volume);
      }
    }

    vTaskDelay(
        pdMS_TO_TICKS(VOLUME_POLLING_PERIOD_MS)); // Poll for volume changes
  }
}

static void volume_press_task(void *pvParameters) {
  ESP_LOGI(
      TAG,
      "Volume press button task started. [BUILD_ID: robust_deep_sleep_v7]");
  uint64_t last_press_time = 0;
  while (1) {
    if (gpio_get_level(VOLUME_PRESS_GPIO) ==
        0) { // Button is pressed (active low)
      // Debounce delay
      vTaskDelay(pdMS_TO_TICKS(50));
      // Wait for button release (first click release)
      while (gpio_get_level(VOLUME_PRESS_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
      }

      // First click happened. Now wait and see if there is a second click.
      bool double_click = false;
      int waited_ms = 0;
      while (waited_ms < DOUBLE_CLICK_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(10));
        waited_ms += 10;
        if (gpio_get_level(VOLUME_PRESS_GPIO) == 0) {
          double_click = true;
          break;
        }
      }

      if (double_click) {
        ESP_LOGI(TAG, "Double click detected - Sending Audio Toggle signal");
        // Debounce second click
        vTaskDelay(pdMS_TO_TICKS(50));
        // Wait for release of second click
        while (gpio_get_level(VOLUME_PRESS_GPIO) == 0) {
          vTaskDelay(pdMS_TO_TICKS(10));
        }

#ifdef CONFIG_IR_REMOTE_ENABLED
        ir_remote_toggle_audio();
#endif
      } else {
        // Timeout reached, no second click -> Single Click Action (Mute Toggle)
        is_muted = !is_muted; // Toggle mute state
        audio_hal_set_mute(g_volume_counter_ptr->board_handle->audio_hal,
                           is_muted);

        if (is_muted) {
          ESP_LOGI(TAG, "Hardware muted");
          update_mute_state(true);
          save_mute_state_to_nvs(true);
          mute_start_time = esp_timer_get_time();
        } else {
          ESP_LOGI(TAG, "Hardware unmuted");
          update_mute_state(false);
          save_mute_state_to_nvs(false);
        }
      }
    }

    // Check for light sleep timeout if muted and power saving is enabled
    if (is_muted && g_power_save_mode != POWER_SAVE_NONE) {
      int64_t now = esp_timer_get_time();
      if (now - mute_start_time > (int64_t)g_light_sleep_delay_ms * 1000) {
        ESP_LOGI(TAG, "Muted for >%u ms. Entering light sleep...",
                 (unsigned int)g_light_sleep_delay_ms);

        // Disconnect WiFi before sleep to bypass bcn_timeout on wakeup
        set_wifi_sleep_mode(true);

        // Enter sleep (logic inside audio_pipeline_manager.c)
        // Pass timer wakeup duration ONLY if Light+Deep mode is active
        uint64_t requested_sleep_us =
            (g_power_save_mode == POWER_SAVE_LIGHT_DEEP)
                ? ((uint64_t)g_deep_sleep_delay_ms * 1000)
                : 0;
        int64_t sleep_start_time = esp_timer_get_time();

        ESP_LOGI(TAG,
                 "Entering light sleep stage (requested %llu us, mode: %d)...",
                 requested_sleep_us, (int)g_power_save_mode);

        // Set sentinel to ensure lockout is active during the sleep/wake
        // transition
        g_last_wakeup_time = LLONG_MAX;

        audio_pipeline_manager_sleep(&audio_pipeline_components,
                                     VOLUME_PRESS_GPIO, requested_sleep_us);

        // --- AFTER WAKEUP ---

        // Set wakeup timestamp for lockout immediately to prevent race with
        // polling tasks
        g_last_wakeup_time = esp_timer_get_time();

        // Check wakeup cause and duration
        int64_t actual_sleep_duration_us =
            esp_timer_get_time() - sleep_start_time;
        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

        ESP_LOGI(TAG,
                 "Woke from light sleep after %lld us. Cause: %d (1=EXT0, "
                 "2=EXT1, 4=TIMER, 7=GPIO)",
                 actual_sleep_duration_us, cause);

        // Calculate safety threshold based on current delay (90%)
        uint64_t min_sleep_threshold_us = (uint64_t)g_deep_sleep_delay_ms * 900;

        // ONLY enter deep sleep if in LIGHT_DEEP mode and it was a timer
        // timeout
        if (g_power_save_mode == POWER_SAVE_LIGHT_DEEP &&
            cause == ESP_SLEEP_WAKEUP_TIMER &&
            actual_sleep_duration_us >= min_sleep_threshold_us) {
          ESP_LOGI(TAG, "Deep sleep timeout reached. Transitioning to system "
                        "deep sleep...");

          // Ensure button is released before entering deep sleep to avoid
          // immediate wakeup
          while (gpio_get_level(VOLUME_PRESS_GPIO) == 0) {
            ESP_LOGI(TAG, "Waiting for button release before deep sleep...");
            vTaskDelay(pdMS_TO_TICKS(100));
          }

          // Ensure WiFi is stopped for a clean state
          set_wifi_sleep_mode(true);
          esp_wifi_stop();

          // Disable all wakeup sources before re-configuring for deep sleep
          esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

          // Ensure RTC pull-up is enabled for deep sleep
          rtc_gpio_init(VOLUME_PRESS_GPIO);
          rtc_gpio_set_direction(VOLUME_PRESS_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
          rtc_gpio_pullup_en(VOLUME_PRESS_GPIO);
          rtc_gpio_pulldown_dis(VOLUME_PRESS_GPIO);

          // Configure deep sleep wakeup on Volume Press GPIO
          esp_sleep_enable_ext0_wakeup(VOLUME_PRESS_GPIO, 0); // Wake on LOW

          ESP_LOGI(TAG, "Entering deep sleep NOW. Wake up with Volume Press.");
          esp_deep_sleep_start();
        } else {
          ESP_LOGI(TAG, "Light sleep interrupted (duration glitch or button "
                        "press). Resuming...");
        }

        // If we reach here, it was a GPIO wakeup (or other)
        ESP_LOGI(TAG, "Resuming from light sleep...");

        // Trigger reconnection immediately (non-blocking)
        set_wifi_sleep_mode(false);

        // Wake up display immediately for user feedback
        lvgl_ssd1306_wakeup();

        // Wait for wifi before restarting pipeline
        wait_for_wifi_connection();

        // Brief delay to let network stack settle
        vTaskDelay(pdMS_TO_TICKS(100));

        // Restart pipeline
        audio_pipeline_manager_wakeup(&audio_pipeline_components);

        // Reset watchdog to avoid spurious restarts
        reset_watchdog_counter();

        // Hardware is still muted from before sleep, but we might want to stay
        // muted the user said: "The light sleep state should be awakened by a
        // press on the volume switch along with unmuting." So we unmute on
        // wakeup.
        is_muted = false;
        audio_hal_set_mute(g_volume_counter_ptr->board_handle->audio_hal,
                           is_muted);
        update_mute_state(is_muted);
        save_mute_state_to_nvs(is_muted);
        ESP_LOGI(TAG, "Hardware unmuted after wakeup");
      }
    }

    // Debounce after everything
    vTaskDelay(pdMS_TO_TICKS(50));
    vTaskDelay(pdMS_TO_TICKS(VOLUME_PRESS_POLLING_PERIOD_MS));
  }
}

static void station_press_task(void *pvParameters) {
  ESP_LOGI(TAG, "Station press button task started.");
  while (1) {
    if (gpio_get_level(STATION_PRESS_GPIO) ==
        0) { // Button is pressed (active low)
      int64_t press_start_time = esp_timer_get_time();
      bool long_press_handled = false;

      // Debounce delay
      vTaskDelay(pdMS_TO_TICKS(50));

      // Wait while button is pressed
      while (gpio_get_level(STATION_PRESS_GPIO) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));

        // Check for long press during hold
        int64_t current_duration = esp_timer_get_time() - press_start_time;
        if (current_duration > LONG_PRESS_TIME_MS * 1000) {
          ESP_LOGI(TAG,
                   "Long press detected (%" PRId64
                   " us). Rebooting sequence initiated...",
                   current_duration);
          switch_to_reboot_screen();
          vTaskDelay(pdMS_TO_TICKS(REBOOT_MESSAGE_DISPLAY_TIME_MS));
          esp_restart();
          long_press_handled = true;
          break;
        }
      }

      // Only process short press if long press wasn't handled (and device
      // didn't restart)
      if (!long_press_handled) {
        int64_t press_duration = esp_timer_get_time() - press_start_time;
        ESP_LOGI(TAG, "Short press detected (%" PRId64 " us). Showing IP.",
                 press_duration);
        switch_to_ip_screen();

        // Get IP
        esp_netif_ip_info_t ip_info;
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (netif) {
          esp_netif_get_ip_info(netif, &ip_info);
          char ip_str[IP4ADDR_STRLEN_MAX];
          esp_ip4addr_ntoa(&ip_info.ip, ip_str, IP4ADDR_STRLEN_MAX);
          update_ip_label(ip_str);
        } else {
          update_ip_label("No Netif");
        }

        // Wait 5 seconds
        vTaskDelay(pdMS_TO_TICKS(IP_SCREEN_DISPLAY_TIME_MS));
        switch_to_home_screen();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(STATION_PRESS_POLLING_PERIOD_MS));
  }
}

void update_station_select_pulse_counter(void *pvParameters) {
  cyclic_pulse_counter_t *counter = (cyclic_pulse_counter_t *)pvParameters;
  int last_step_count = 0;
  ESP_ERROR_CHECK(pcnt_unit_get_count(counter->pcnt_unit, &last_step_count));
  last_step_count /= 4; // Each detent is 4 counts for a full cycle

  bool on_station_screen = false;

  for (;;) {
    const int fast_poll_ms = 20;
    const int slow_poll_ms = 200;
    const int inactivity_timeout_ms =
        DELAY_BEFORE_STATION_CHANGE_MS; // 2 seconds before action
    static int current_poll_ms = slow_poll_ms;
    static TickType_t last_change_time = 0;

    int raw_count;
    ESP_ERROR_CHECK(pcnt_unit_get_count(counter->pcnt_unit, &raw_count));
    int current_step_count = raw_count / 4;

    if (current_step_count != last_step_count) {
      // Check for lockout period after wakeup
      if (esp_timer_get_time() - g_last_wakeup_time < WAKEUP_LOCKOUT_US) {
        ESP_LOGI(TAG, "Ghost station pulse ignored (lockout): %d -> %d",
                 last_step_count, current_step_count);
        last_step_count = current_step_count;
        continue;
      }

      // A change occurred, switch to station screen if not already there
      if (!on_station_screen) {
        on_station_screen = true;
        ESP_LOGI(TAG, "Encoder turned, switching to station selection screen");
        switch_to_station_selection_screen();
      }

      int step_delta = current_step_count - last_step_count;

      // Update index with wrapping
      int new_index =
          (counter->current_index + step_delta) % counter->num_values;
      if (new_index < 0) {
        new_index += counter->num_values;
      }
      counter->current_index = new_index;
      ESP_LOGI(TAG, "Cyclic index: %d", counter->current_index);
      update_station_roller(counter->current_index);
      last_step_count = current_step_count;

      // A change occurred, switch to fast polling and record the time
      current_poll_ms = fast_poll_ms;
      last_change_time = xTaskGetTickCount();
    } else if (current_poll_ms == fast_poll_ms &&
               (xTaskGetTickCount() - last_change_time) >
                   pdMS_TO_TICKS(inactivity_timeout_ms)) {
      // Inactivity timeout reached, perform action and switch back to slow
      // polling
      ESP_LOGI(TAG, "Inactivity timeout, changing station to index %d",
               counter->current_index);

      change_station(counter->current_index);

      switch_to_home_screen();

      current_poll_ms = slow_poll_ms;
      on_station_screen = false;
    }
    vTaskDelay(pdMS_TO_TICKS(current_poll_ms));
  }
}

void sync_station_encoder_index(void) {
  if (g_station_counter_ptr) {
    g_station_counter_ptr->current_index = current_station;
    ESP_LOGI(TAG, "Synced station encoder index to %d", current_station);
  }
}

void init_encoder_switches(void) {
  gpio_config_t switch_config = {
      .pin_bit_mask =
          (1ULL << VOLUME_PRESS_GPIO) | (1ULL << STATION_PRESS_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&switch_config));

  // Wait a bit for potential capacitance/debounce settle
  vTaskDelay(pdMS_TO_TICKS(100));
}

bool is_station_switch_pressed(void) {
  return gpio_get_level(STATION_PRESS_GPIO) == 0;
}

bool is_volume_switch_pressed(void) {
  return gpio_get_level(VOLUME_PRESS_GPIO) == 0;
}

bool get_mute_state(void) { return is_muted; }

void init_encoders(audio_board_handle_t board_handle, int initial_volume,
                   bool initial_mute, int unmuted_volume) {
  is_muted = initial_mute;
  update_mute_state(is_muted);

  // ESP_LOGI(TAG, "set glitch filter");
  static pcnt_glitch_filter_config_t filter_config = {
      .max_glitch_ns = 1000,
  };

  //*****************  volume encoder **********************
  //*****************  volume encoder **********************
  gpio_config_t volume_encoder_gpio_config = {
      .pin_bit_mask = (1ULL << VOLUME_GPIO_A) | (1ULL << VOLUME_GPIO_B),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&volume_encoder_gpio_config));

  ESP_LOGI(TAG, "install volume pcnt unit");
  static pcnt_unit_config_t volume_unit_config = {
      .high_limit =
          INT16_MAX, // these are defined in <limits.h>  16 bit counter has
                     // type int (32bit?), force int16 limits
      .low_limit = INT16_MIN,
  };
  static pcnt_unit_handle_t volume_pcnt_unit = NULL;
  ESP_ERROR_CHECK(pcnt_new_unit(&volume_unit_config, &volume_pcnt_unit));

  // limited_pulse_counter_t volume_counter;
  ESP_ERROR_CHECK(
      pcnt_unit_set_glitch_filter(volume_pcnt_unit, &filter_config));

  ESP_LOGI(TAG, "install pcnt channels");
  static pcnt_chan_config_t chan_a_config = {
      .edge_gpio_num = VOLUME_GPIO_A,
      .level_gpio_num = VOLUME_GPIO_B,
  };
  static pcnt_channel_handle_t pcnt_chan_a = NULL;
  ESP_ERROR_CHECK(
      pcnt_new_channel(volume_pcnt_unit, &chan_a_config, &pcnt_chan_a));
  ESP_LOGI(TAG, "set edge and level actions for pcnt channels");

  ESP_ERROR_CHECK(pcnt_channel_set_edge_action(
      pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
      PCNT_CHANNEL_EDGE_ACTION_INCREASE));
  ESP_ERROR_CHECK(
      pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                    PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

  ESP_LOGI(TAG, "enable volume pcnt unit");
  ESP_ERROR_CHECK(pcnt_unit_enable(volume_pcnt_unit));
  ESP_LOGI(TAG, "clear volume pcnt unit");
  ESP_ERROR_CHECK(pcnt_unit_clear_count(volume_pcnt_unit));
  ESP_LOGI(TAG, "start volume pcnt unit");
  ESP_ERROR_CHECK(pcnt_unit_start(volume_pcnt_unit));

  // Use heap allocation for the counter so it can be shared
  limited_pulse_counter_t *volume_counter =
      malloc(sizeof(limited_pulse_counter_t));
  if (!volume_counter) {
    ESP_LOGE(TAG, "Failed to allocate memory for volume counter");
    return;
  }
  volume_counter->pcnt_unit = volume_pcnt_unit;
  volume_counter->value = initial_volume;
  volume_counter->adjust = initial_volume;
  volume_counter->speed = 5; // number of units of volume per encoder step
  volume_counter->board_handle = board_handle;
  ESP_LOGI(TAG, "start volume update task");
  xTaskCreate(update_volume_pulse_counter, "update_volume_pulse_counter",
              4 * 1024, volume_counter, 5, NULL);

  xTaskCreate(volume_press_task, "volume_press_task", 6144, NULL, 5, NULL);

  // *********************  station encoder  **********************
  static gpio_config_t station_encoder_gpio_config = {
      .pin_bit_mask = (1ULL << STATION_GPIO_A) | (1ULL << STATION_GPIO_B),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&station_encoder_gpio_config));
  static pcnt_unit_config_t station_unit_config = {
      .high_limit =
          INT16_MAX, // these are defined in <limits.h>  16 bit counter has
                     // type int (32bit?), force int16 limits
      .low_limit = INT16_MIN,
  };
  static pcnt_unit_handle_t station_pcnt_unit = NULL;
  ESP_ERROR_CHECK(pcnt_new_unit(&station_unit_config, &station_pcnt_unit));

  ESP_ERROR_CHECK(
      pcnt_unit_set_glitch_filter(station_pcnt_unit, &filter_config));

  static pcnt_chan_config_t chan_b_config = {.edge_gpio_num = STATION_GPIO_A,
                                             .level_gpio_num = STATION_GPIO_B,
                                             .flags = {
                                                 .invert_edge_input = true,
                                                 .invert_level_input = true,
                                             }};
  static pcnt_channel_handle_t pcnt_chan_b = NULL;
  ESP_ERROR_CHECK(
      pcnt_new_channel(station_pcnt_unit, &chan_b_config, &pcnt_chan_b));

  ESP_ERROR_CHECK(pcnt_channel_set_edge_action(
      pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
      PCNT_CHANNEL_EDGE_ACTION_INCREASE));
  ESP_ERROR_CHECK(
      pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                    PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

  // enable cyclic counter
  ESP_LOGI(TAG, "enable station pcnt unit");
  ESP_ERROR_CHECK(pcnt_unit_enable(station_pcnt_unit));
  ESP_LOGI(TAG, "clear station pcnt unit");
  ESP_ERROR_CHECK(pcnt_unit_clear_count(station_pcnt_unit));
  ESP_LOGI(TAG, "start station pcnt unit");
  ESP_ERROR_CHECK(pcnt_unit_start(station_pcnt_unit));

  // Use malloc to keep it in scope for the task and for sync function
  g_station_counter_ptr = malloc(sizeof(cyclic_pulse_counter_t));
  g_station_counter_ptr->pcnt_unit = station_pcnt_unit;
  g_station_counter_ptr->num_values = station_count;
  g_station_counter_ptr->current_index = current_station;

  xTaskCreate(update_station_select_pulse_counter,
              "update_station_select_pulse_counter", 4 * 1024,
              g_station_counter_ptr, 5, NULL);
  xTaskCreate(station_press_task, "station_press_task", 4096, NULL, 5, NULL);
}
