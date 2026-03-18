/* Play internet radio stations

    based on esp-adf code: pipeline_living_stream

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "audio_common.h"
#include "audio_event_iface.h"
#include "audio_pipeline_manager.h"
#include "board.h"
// #include "driver/gpio.h"
#include "encoders.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_peripherals.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
// #include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "ir_remote.h"
#include "lvgl_ssd1306_setup.h"
#include "nvs_flash.h"
#include "screens.h"
// #include "sdkconfig.h"
#include "internet_radio_adf.h"
#include "app_config.h"
#include "station_data.h"
#include "web_server.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include <string.h>

static const char *TAG = "INTERNET_RADIO";

// Volume control
#define INITIAL_VOLUME 60

#include "gpio_assignments.h"

#define IR_TX_GPIO_NUM IR_TX_GPIO

#define BUTTON_POLLING_PERIOD_MS 100

#define BITRATE_UPDATE_INTERVAL_MS 1000

// oled screen with lvgl
extern int station_count; // from station_data.c
static lv_display_t *display;

// global variables for easier access in callbacks
audio_pipeline_components_t audio_pipeline_components = {0};
int current_station = 0;
static bool g_is_sleeping = false;



#define BITRATE_HISTORY_SIZE 10
static int g_bitrate_history[BITRATE_HISTORY_SIZE] = {0};
static int g_history_index = 0;

audio_board_handle_t board_handle = NULL;
audio_event_iface_handle_t evt = NULL;
esp_periph_set_handle_t periph_set = NULL;
// g_ir_tx_channel is no longer needed globally as it's managed by the ir_remote component

volatile int g_bitrate_kbps = 0;
// system monitor logging enable
static bool g_enable_sys_monitor = false;
static int g_consecutive_zero_count = 0;
volatile bool g_is_pipeline_running = false; // Flag to control watchdog
// Button Handles

static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

static void save_current_station_to_nvs(int station_index) {
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle for writing!",
             esp_err_to_name(err));
    return;
  }

  err = nvs_set_i32(nvs_handle, "station_idx", station_index);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) writing 'station_idx' to NVS!",
             esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "Saved current_station = %d to NVS", station_index);
  }

  err = nvs_commit(nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) committing 'station_idx' to NVS!",
             esp_err_to_name(err));
  }

  nvs_close(nvs_handle);
}

// Removed WiFi state NVS functions (reverting to full discovery)

void change_station(int new_station_index) {
  esp_err_t ret;

  if (new_station_index < 0 || new_station_index >= station_count) {
    ESP_LOGE(TAG, "Invalid station index: %d", new_station_index);
    return;
  }

  // Only change if the new station is different from the current one
  if (new_station_index == current_station) {
    ESP_LOGI(TAG, "Station %d is already selected. No change needed.",
             new_station_index);
    return;
  }

  // Mute at start of station change to avoid pops/noise
  if (board_handle && board_handle->audio_hal) {
    audio_hal_set_mute(board_handle->audio_hal, true);
  }

  ESP_LOGI(TAG, "Destroying current pipeline...");
  destroy_audio_pipeline(&audio_pipeline_components);
  vTaskDelay(
      pdMS_TO_TICKS(500)); // Allow network stack and memory manager to settle

  current_station = new_station_index;
  g_is_pipeline_running = false;
  ESP_LOGI(TAG, "Switching to station %d: %s, %s", current_station,
           radio_stations[current_station].call_sign,
           radio_stations[current_station].origin);
  sync_station_encoder_index(); // Sync encoder's internal state
  save_current_station_to_nvs(current_station);
  update_station_name(radio_stations[current_station].call_sign);
  update_station_origin(radio_stations[current_station].origin);

  ret = create_audio_pipeline(&audio_pipeline_components,
                              radio_stations[current_station].codec,
                              radio_stations[current_station].uri);
  if (ret != ESP_OK) {
    ESP_LOGE(
        TAG,
        "Failed to create new audio pipeline for station %s, %s. Error: %d",
        radio_stations[current_station].call_sign,
        radio_stations[current_station].origin, ret);
    // Restore mute state even on failure
    if (board_handle && board_handle->audio_hal) {
      audio_hal_set_mute(board_handle->audio_hal, get_mute_state());
    }
    return;
  }

  ESP_LOGI(TAG, "Starting new audio pipeline");
  reset_throughput_history();
  ret = audio_pipeline_run(audio_pipeline_components.pipeline);

  // Restore mute state after pipeline is started
  if (board_handle && board_handle->audio_hal) {
    audio_hal_set_mute(board_handle->audio_hal, get_mute_state());
  }

  // Link event listener
  if (evt) {
    audio_pipeline_set_listener(audio_pipeline_components.pipeline, evt);
  }

  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to run new audio pipeline. Error: %d", ret);
    destroy_audio_pipeline(&audio_pipeline_components);
  } else {
    g_is_pipeline_running = true;
  }
}

/* Event handler for catching system events */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == WIFI_PROV_EVENT) {
    switch (event_id) {
    case WIFI_PROV_START:
      switch_to_provisioning_screen();
      ESP_LOGI(TAG, "Provisioning started");
      break;
    case WIFI_PROV_CRED_RECV: {
      wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
      ESP_LOGI(TAG,
               "Received Wi-Fi credentials"
               "\n\tSSID: %s\n\tPassword: %s",
               (const char *)wifi_sta_cfg->ssid,
               (const char *)wifi_sta_cfg->password);
      break;
    }
    case WIFI_PROV_CRED_FAIL: {
      wifi_prov_sta_fail_reason_t *reason =
          (wifi_prov_sta_fail_reason_t *)event_data;
      ESP_LOGE(TAG,
               "Provisioning failed!\n\tReason: %s"
               "\n\tPlease reset to factory and retry.",
               (*reason == WIFI_PROV_STA_AUTH_ERROR)
                   ? "Wi-Fi station authentication failed"
                   : "Wi-Fi station not found");
      break;
    }
    case WIFI_PROV_CRED_SUCCESS:
      switch_to_home_screen();
      ESP_LOGI(TAG, "Provisioning successful");
      break;
    case WIFI_PROV_END:
      /* De-initialize manager once provisioning is finished */
      wifi_prov_mgr_deinit();
      break;
    default:
      break;
    }
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
    ESP_LOGI(TAG, "WiFi Connected");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

    xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_event_sta_disconnected_t *event =
        (wifi_event_sta_disconnected_t *)event_data;
    ESP_LOGI(TAG, "Disconnected. Reason: %d. Connecting to the AP again...",
             event->reason);
    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);

    // Standard reconnect logic
    esp_wifi_connect();
  }
}

static void wifi_init_sta(void) {
  /* Start Wi-Fi in station mode */
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

  ESP_ERROR_CHECK(esp_wifi_start());
  // Standard power save for light sleep compatibility
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
}

static void get_device_service_name(char *service_name, size_t max) {
  uint8_t eth_mac[6];
  const char *ssid_prefix = "agk radio:";
  esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
  snprintf(service_name, max, "%s%02X%02X%02X", ssid_prefix, eth_mac[3],
           eth_mac[4], eth_mac[5]);
}

#include "esp_timer.h" // Added for watchdog timer

/**
 * @brief Task to measure and log the data throughput in kbps.
 */
static void data_throughput_task(void *pvParameters) {
  uint64_t last_bytes_read = 0;

  uint64_t current_bytes_read;
  uint64_t bytes_read_in_interval;

  static uint64_t prev_idle_0 = 0;
  static uint64_t prev_idle_1 = 0;
  static uint64_t prev_total = 0;

  while (1) {
    vTaskDelay(pdMS_TO_TICKS(BITRATE_UPDATE_INTERVAL_MS));

    current_bytes_read = g_bytes_read;
    bytes_read_in_interval = current_bytes_read - last_bytes_read;
    last_bytes_read = current_bytes_read;

    // Calculate current bitrate and add to history
    int current_bitrate =
        (bytes_read_in_interval * 8) / BITRATE_UPDATE_INTERVAL_MS;
    g_bitrate_history[g_history_index] = current_bitrate;
    g_history_index = (g_history_index + 1) % BITRATE_HISTORY_SIZE;

    // Calculate weighted moving average
    long weighted_sum = 0;
    int total_weight = 0;
    for (int i = 0; i < BITRATE_HISTORY_SIZE; i++) {
      int weight = i + 1; // Simple linear weight (1 for oldest, 10 for newest)
      int data_index = (g_history_index + i) % BITRATE_HISTORY_SIZE;
      weighted_sum += g_bitrate_history[data_index] * weight;
      total_weight += weight;
    }

    g_bitrate_kbps = weighted_sum / total_weight;
    update_bitrate_label(g_bitrate_kbps);

    if (g_enable_sys_monitor) {
      // monitoring ram usage.  remove this for production
      size_t total_ram = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
      size_t free_ram = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
      size_t used_ram = total_ram - free_ram;

      // Log RAM usage
      ESP_LOGI(TAG, "RAM: Used: %zu, Free: %zu, Total: %zu", used_ram, free_ram,
               total_ram);
    }

    if (g_enable_sys_monitor) {
      // Core Load Monitoring
      TaskStatus_t status;
      vTaskGetInfo(xTaskGetIdleTaskHandleForCore(0), &status, pdFALSE,
                   eInvalid);
      uint64_t idle_0 = status.ulRunTimeCounter;

      vTaskGetInfo(xTaskGetIdleTaskHandleForCore(1), &status, pdFALSE,
                   eInvalid);
      uint64_t idle_1 = status.ulRunTimeCounter;

      uint64_t current_total_time = esp_timer_get_time();

      if (prev_total > 0) {
        uint64_t total_diff = current_total_time - prev_total;
        uint64_t idle_0_diff = idle_0 - prev_idle_0;
        uint64_t idle_1_diff = idle_1 - prev_idle_1;

        // Ensure we don't divide by zero or get negative load due to
        // overflow/timing glitches
        if (total_diff > 0) {
          float load_0 = 100.0f * (1.0f - ((float)idle_0_diff / total_diff));
          float load_1 = 100.0f * (1.0f - ((float)idle_1_diff / total_diff));

          // Clamp to 0-100 range
          if (load_0 < 0)
            load_0 = 0;
          else if (load_0 > 100)
            load_0 = 100;
          if (load_1 < 0)
            load_1 = 0;
          else if (load_1 > 100)
            load_1 = 100;

          ESP_LOGI(TAG, "Core Load: Core 0: %.2f%%, Core 1: %.2f%%", load_0,
                   load_1);
        }
      }

      prev_idle_0 = idle_0;
      prev_idle_1 = idle_1;
      prev_total = current_total_time;
    }

    // Watchdog check
    if (g_is_pipeline_running) {
      if (current_bitrate == 0) {
        g_consecutive_zero_count++;
      } else {
        g_consecutive_zero_count = 0;
      }

      if (g_consecutive_zero_count >= 30) {

        ESP_LOGE(TAG, "Watchdog triggered: 0 kbps for 30 consecutive seconds. "
                      "Restarting...");
        esp_restart();
      }
    } else {
      g_consecutive_zero_count = 0;
    }
  }
}

void reset_watchdog_counter(void) {
  g_consecutive_zero_count = 0;
  ESP_LOGI(TAG, "Watchdog counter reset");
}

void reset_throughput_history(void) {
  memset(g_bitrate_history, 0, sizeof(g_bitrate_history));
  g_history_index = 0;
  ESP_LOGI(TAG, "Throughput history reset");
}

void wait_for_wifi_connection(void) {
  ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");
  xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true,
                      portMAX_DELAY);
  ESP_LOGI(TAG, "Wi-Fi Connected.");
}

void set_wifi_sleep_mode(bool sleeping) {
  g_is_sleeping = sleeping;
  if (sleeping) {
    ESP_LOGI(TAG, "WiFi staying active for light sleep (WIFI_PS_MIN_MODEM)");
  } else {
    ESP_LOGI(TAG, "WiFi resuming full power");
  }
}

void app_main(void) {

  int initial_volume = INITIAL_VOLUME;
  int unmuted_volume = INITIAL_VOLUME;
  bool initial_mute = false;
  esp_log_level_set("*", ESP_LOG_DEBUG);

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  bool waked_by_button = false;
  switch (wakeup_reason) {
  case ESP_SLEEP_WAKEUP_EXT0:
    ESP_LOGI(
        TAG,
        "Wakeup caused by external signal using RTC_IO (EXT0) - Volume Button");
    waked_by_button = true;
    break;
  case ESP_SLEEP_WAKEUP_EXT1:
    ESP_LOGI(TAG, "Wakeup caused by external signal using RTC_CNTL (EXT1)");
    break;
  case ESP_SLEEP_WAKEUP_TIMER:
    ESP_LOGI(TAG, "Wakeup caused by timer");
    break;
  case ESP_SLEEP_WAKEUP_TOUCHPAD:
    ESP_LOGI(TAG, "Wakeup caused by touchpad");
    break;
  case ESP_SLEEP_WAKEUP_ULP:
    ESP_LOGI(TAG, "Wakeup caused by ULP program");
    break;
  case ESP_SLEEP_WAKEUP_GPIO:
    ESP_LOGI(TAG, "Wakeup caused by GPIO (Light Sleep)");
    break;
  default:
    ESP_LOGI(TAG, "Wakeup was not caused by deep sleep or light sleep: %d",
             wakeup_reason);
    break;
  }

  // esp_log_level_set(TAG, ESP_LOG_DEBUG);
  // esp_log_level_set("HTTP_STREAM", ESP_LOG_DEBUG);
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  load_app_config();

  init_station_data();

  nvs_handle_t nvs_handle;
  err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
  } else {
    ESP_LOGI(TAG, "Reading current_station from NVS");
    int32_t station_from_nvs = 0; // default to 0
    err = nvs_get_i32(nvs_handle, "station_idx", &station_from_nvs);
    switch (err) {
    case ESP_OK:
      ESP_LOGI(TAG, "Successfully read current_station = %d",
               (int)station_from_nvs);
      if (station_from_nvs >= 0 && station_from_nvs < station_count) {
        current_station = station_from_nvs;
      } else {
        ESP_LOGW(TAG, "Invalid station index %d found in NVS, defaulting to 0",
                 (int)station_from_nvs);
        current_station = 0;
      }
      break;
    case ESP_ERR_NVS_NOT_FOUND:
      ESP_LOGI(TAG, "The value 'station_idx' is not initialized yet!");
      break;
    default:
      ESP_LOGE(TAG, "Error (%s) reading!", esp_err_to_name(err));
    }

    // Read volume from NVS
    ESP_LOGI(TAG, "Reading volume from NVS");
    int32_t volume_from_nvs = INITIAL_VOLUME;
    err = nvs_get_i32(nvs_handle, "volume", &volume_from_nvs);
    switch (err) {
    case ESP_OK:
      ESP_LOGI(TAG, "Successfully read volume = %d", (int)volume_from_nvs);
      if (volume_from_nvs >= 0 && volume_from_nvs <= 100) {
        initial_volume = volume_from_nvs;
      } else {
        ESP_LOGW(TAG, "Invalid volume %d found in NVS, defaulting to %d",
                 (int)volume_from_nvs, INITIAL_VOLUME);
      }
      break;
    case ESP_ERR_NVS_NOT_FOUND:
      ESP_LOGI(TAG, "The value 'volume' is not initialized yet!");
      break;
    default:
      ESP_LOGE(TAG, "Error (%s) reading 'volume'!", esp_err_to_name(err));
    }

    // Read mute state from NVS
    ESP_LOGI(TAG, "Reading mute state from NVS");
    uint8_t mute_from_nvs = 0; // default to unmuted
    err = nvs_get_u8(nvs_handle, "mute_state", &mute_from_nvs);
    if (err == ESP_OK) {
      initial_mute = (mute_from_nvs != 0);
      ESP_LOGI(TAG, "Successfully read mute_state = %d", (int)initial_mute);
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGI(TAG, "The value 'mute_state' is not initialized yet!");
    } else {
      ESP_LOGE(TAG, "Error (%s) reading 'mute_state'!", esp_err_to_name(err));
    }

    // Override mute if waking from deep sleep by button
    if (waked_by_button) {
      ESP_LOGI(TAG, "Wakeup via button press: Overriding mute state to UNMUTED "
                    "and updating NVS.");
      initial_mute = false;
      err = nvs_set_u8(nvs_handle, "mute_state", 0);
      if (err == ESP_OK) {
        nvs_commit(nvs_handle);
      } else {
        ESP_LOGE(TAG, "Error (%s) updating 'mute_state' in NVS!",
                 esp_err_to_name(err));
      }
    }

    unmuted_volume = initial_volume;
    // initial_volume is preserved even if initial_mute is true

    nvs_close(nvs_handle);
  }
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 1, 0))
  ESP_ERROR_CHECK(esp_netif_init());
#else
  tcpip_adapter_init();
#endif
  // Create the UI message queue before any UI tasks are started
  g_ui_queue = xQueueCreate(10, sizeof(ui_update_message_t));

  display = lvgl_ssd1306_setup();
  screens_init(display);
  update_station_name(radio_stations[current_station].call_sign);
  update_station_origin(radio_stations[current_station].origin);

  // start oled display test task,  remove after debugging
  // xTaskCreate(task_test_ssd1306, "u8g2_task", 4096, NULL, 5, NULL);

  wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
                                             &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &event_handler, NULL));

  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  wifi_prov_mgr_config_t config = {
      .scheme = wifi_prov_scheme_ble,
      .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM};

  ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

  // If waking from deep sleep, we perform a full discovery
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 || wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
     ESP_LOGI(TAG, "Deep sleep wakeup: Starting full WiFi discovery.");
  }

  // Check for forced provisioning (Volume button held during boot)
  init_encoder_switches();
  if (is_volume_switch_pressed()) {
    ESP_LOGI(TAG, "Volume button held at boot - Forcing Reprovisioning!");
    wifi_prov_mgr_reset_provisioning();
  }

  bool provisioned = false;
  ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
  // provisioned = false; // force provisioning for testing comment this line
  // for production
  uint8_t custom_service_uuid[] = {
      /* LSB <---------------------------------------
       * ---------------------------------------> MSB */
      0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
      0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
  };

  if (!provisioned) {
    ESP_LOGI(TAG, "Starting provisioning");

    char service_name[20];
    get_device_service_name(service_name, sizeof(service_name));

    wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid);
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, NULL,
                                                     service_name, NULL));
  ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi");
  }

  // Initialize Power Management for Wi-Fi Light Sleep compatibility
  esp_pm_config_t pm_config = {
      .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
      .min_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
      .light_sleep_enable = true
  };
  ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

  // Start Wi-Fi (non-blocking scan/associate)
  ESP_LOGI(TAG, "Starting Wi-Fi...");
  wifi_init_sta();

  // Initialize hardware ONLY while Wi-Fi connects in the background
  esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
  periph_set = esp_periph_set_init(&periph_cfg);

  ESP_LOGI(TAG, "Start audio codec chip");
  board_handle = audio_board_init();
  board_handle->audio_hal->audio_codec_ctrl(AUDIO_HAL_CODEC_MODE_BOTH,
                                            AUDIO_HAL_CTRL_START);
  audio_hal_set_volume(board_handle->audio_hal, initial_volume);
  audio_hal_set_mute(board_handle->audio_hal, initial_mute);
  update_volume_slider(initial_volume);

  ESP_LOGI(TAG, "Set up event listener");
  audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
  evt = audio_event_iface_init(&evt_cfg);
  audio_event_iface_set_listener(esp_periph_set_get_event_iface(periph_set),
                                 evt);

#ifdef CONFIG_IR_REMOTE_ENABLED
  if (g_runtime_config.ir_is_enabled) {
    ESP_LOGI(TAG, "Initializing IR Remote Component");
    ir_remote_init((gpio_num_t)IR_TX_GPIO_NUM, IR_PROTOCOL_BOSE);
    ESP_LOGI(TAG, "Sending Audio ON signal");
    ir_remote_turn_audio_on();
  }
#endif

  ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");
  xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true,
                      portMAX_DELAY);
  ESP_LOGI(TAG, "Wi-Fi Connected.");

  // Start audio pipeline AFTER WiFi is confirmed connected
  ESP_LOGI(TAG, "Starting audio pipeline...");
  err = create_audio_pipeline(&audio_pipeline_components,
                              radio_stations[current_station].codec,
                              radio_stations[current_station].uri);
  if (err == ESP_OK) {
    reset_throughput_history();
    audio_pipeline_run(audio_pipeline_components.pipeline);
    if (evt) {
      audio_pipeline_set_listener(audio_pipeline_components.pipeline, evt);
    }
    g_is_pipeline_running = true;
  }

  start_web_server();

  xTaskCreate(data_throughput_task, "data_throughput_task", 3 * 1024, NULL, 5,
              NULL);

  //  start encoder pulse counters

  init_encoders(board_handle, initial_volume, initial_mute, unmuted_volume);

  while (1) {
    audio_event_iface_msg_t msg;
    esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "[ * ] Event interface error : %d", ret);
      continue;
    }
    ESP_LOGI(TAG, "Received event from element: %X, command: %d",
             (int)msg.source, msg.cmd);

    /* restart stream when the first pipeline element (http_stream_reader in
     * this case) receives stop event (caused by reading errors) */
    if (audio_pipeline_components.http_stream_reader &&
        msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT &&
        msg.source == (void *)audio_pipeline_components.http_stream_reader &&
        msg.cmd == AEL_MSG_CMD_REPORT_STATUS &&
        (int)msg.data == AEL_STATUS_ERROR_OPEN) {
      
      static int open_error_count = 0;
      open_error_count++;
      ESP_LOGW(TAG, "[ * ] Restart stream (attempt %d)", open_error_count);

      // Fallback logic removed as caching is disabled
      audio_pipeline_stop(audio_pipeline_components.pipeline);
      audio_pipeline_wait_for_stop(audio_pipeline_components.pipeline);
      audio_element_reset_state(audio_pipeline_components.codec_decoder);
      audio_element_reset_state(audio_pipeline_components.i2s_stream_writer);
      audio_pipeline_reset_ringbuffer(audio_pipeline_components.pipeline);
      audio_pipeline_reset_items_state(audio_pipeline_components.pipeline);
      vTaskDelay(pdMS_TO_TICKS(500)); // Brief delay before retry
      audio_pipeline_run(audio_pipeline_components.pipeline);
      continue;
    }

    // Reset open error count on successful start or other events
    // (Note: in a real app you might check for AEL_STATUS_STATE_RUNNING)
  }

  ESP_LOGI(TAG, "Stopping audio_pipeline");
  destroy_audio_pipeline(&audio_pipeline_components);

  if (periph_set) {
    esp_periph_set_stop_all(periph_set);
    if (evt) {
      audio_event_iface_remove_listener(
          esp_periph_set_get_event_iface(periph_set), evt);
    }
  }

  if (evt) {
    audio_event_iface_destroy(evt);
  }
  if (periph_set) {
    esp_periph_set_destroy(periph_set);
  }
}