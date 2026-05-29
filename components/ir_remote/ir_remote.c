#include "ir_remote.h"
#include "app_config.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define IR_RESOLUTION_HZ 1000000 // 1MHz resolution, 1 tick = 1us

static const char* TAG = "ir_remote";

// ----------------------------------------------------------------------------
// BOSE Protocol Data (Migrated from legacy ir_rmt.c)
// ----------------------------------------------------------------------------
static const rmt_symbol_word_t bose_aux_signal[] = {
    {.duration0 = 1104, .level0 = 1, .duration1 = 1467, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1428, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1447, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 434, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 435, .level1 = 0},
    {.duration0 = 571, .level0 = 1, .duration1 = 434, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 454, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 435, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 434, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 435, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 454, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1417, .level1 = 0},
    {.duration0 = 9531, .level0 = 1, .duration1 = 4580, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 613, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1716, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1735, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1735, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 634, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 1735, .level1 = 0},
    {.duration0 = 494, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 1716, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 633, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 613, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 634, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 634, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1705, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 0, .level1 = 0},
};

static const rmt_symbol_word_t bose_on_off_signal[] = {
    {.duration0 = 1103, .level0 = 1, .duration1 = 1466, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 433, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 433, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 1447, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 433, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 434, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 453, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 432, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 452, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 433, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 1417, .level1 = 0},
    {.duration0 = 9537, .level0 = 1, .duration1 = 4566, .level1 = 0},
    {.duration0 = 506, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 494, .level0 = 1, .duration1 = 1712, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 494, .level0 = 1, .duration1 = 1734, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 609, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 588, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 588, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 1734, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 609, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 588, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 588, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 608, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 608, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 588, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 496, .level0 = 1, .duration1 = 1703, .level1 = 0},
    {.duration0 = 496, .level0 = 1, .duration1 = 0, .level1 = 0},
};

// ----------------------------------------------------------------------------
// DUBAS_RADIO Protocol Data (Duplicate of Bose for now)
// ----------------------------------------------------------------------------
static const rmt_symbol_word_t dubas_radio_aux_signal[] = {
    {.duration0 = 1104, .level0 = 1, .duration1 = 1467, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1428, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1447, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 434, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 435, .level1 = 0},
    {.duration0 = 571, .level0 = 1, .duration1 = 434, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 454, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 435, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 434, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 435, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 454, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1417, .level1 = 0},
    {.duration0 = 9531, .level0 = 1, .duration1 = 4580, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 613, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1716, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1735, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1735, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 634, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 1735, .level1 = 0},
    {.duration0 = 494, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 1716, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 633, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 613, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 634, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 634, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 614, .level1 = 0},
    {.duration0 = 492, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1715, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1705, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 0, .level1 = 0},
};

static const rmt_symbol_word_t dubas_radio_on_off_signal[] = {
    {.duration0 = 1103, .level0 = 1, .duration1 = 1466, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 433, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 433, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 1447, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 433, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 434, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 453, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 432, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 452, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 574, .level0 = 1, .duration1 = 1427, .level1 = 0},
    {.duration0 = 573, .level0 = 1, .duration1 = 433, .level1 = 0},
    {.duration0 = 572, .level0 = 1, .duration1 = 1417, .level1 = 0},
    {.duration0 = 9537, .level0 = 1, .duration1 = 4566, .level1 = 0},
    {.duration0 = 506, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 494, .level0 = 1, .duration1 = 1712, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 494, .level0 = 1, .duration1 = 1734, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 609, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 588, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 588, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 1734, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 609, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 588, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 588, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 608, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 495, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 589, .level1 = 0},
    {.duration0 = 516, .level0 = 1, .duration1 = 608, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 588, .level1 = 0},
    {.duration0 = 517, .level0 = 1, .duration1 = 1714, .level1 = 0},
    {.duration0 = 496, .level0 = 1, .duration1 = 1703, .level1 = 0},
    {.duration0 = 506, .level0 = 1, .duration1 = 1704, .level1 = 0},
    {.duration0 = 496, .level0 = 1, .duration1 = 0, .level1 = 0},
};

// ----------------------------------------------------------------------------

typedef struct {
    esp_err_t (*turn_audio_on)(void);
    esp_err_t (*turn_audio_off)(void);
    esp_err_t (*toggle_audio)(void);
} ir_remote_ops_t;

static rmt_channel_handle_t g_tx_channel = NULL;
static ir_protocol_t g_current_protocol = IR_PROTOCOL_NONE;
static const ir_remote_ops_t* g_current_ops = NULL;

static esp_err_t send_signal(const rmt_symbol_word_t* signal_data, size_t signal_size);

static esp_err_t bose_turn_audio_on(void);
static esp_err_t bose_turn_audio_off(void);
static esp_err_t bose_toggle_audio(void);

static const ir_remote_ops_t bose_ops = {
    .turn_audio_on = bose_turn_audio_on,
    .turn_audio_off = bose_turn_audio_off,
    .toggle_audio = bose_toggle_audio,
};

static esp_err_t dubas_radio_turn_audio_on(void);
static esp_err_t dubas_radio_turn_audio_off(void);
static esp_err_t dubas_radio_toggle_audio(void);

static const ir_remote_ops_t dubas_radio_ops = {
    .turn_audio_on = dubas_radio_turn_audio_on,
    .turn_audio_off = dubas_radio_turn_audio_off,
    .toggle_audio = dubas_radio_toggle_audio,
};

static void update_current_ops(ir_protocol_t protocol) {
    if (protocol == IR_PROTOCOL_BOSE) {
        g_current_ops = &bose_ops;
    } else if (protocol == IR_PROTOCOL_DUBAS_RADIO) {
        g_current_ops = &dubas_radio_ops;
    } else {
        g_current_ops = NULL;
    }
}

esp_err_t ir_remote_init(gpio_num_t tx_gpio_num, ir_protocol_t protocol)
{
    if (g_tx_channel != NULL) {
        ESP_LOGW(TAG, "IR remote already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing IR remote on GPIO %d, protocol %d", tx_gpio_num, protocol);
    
    rmt_tx_channel_config_t tx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .gpio_num = tx_gpio_num,
    };
    
    esp_err_t ret = rmt_new_tx_channel(&tx_channel_cfg, &g_tx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT TX channel: %d", ret);
        return ret;
    }

    rmt_carrier_config_t carrier_cfg = {
        .duty_cycle = 0.33,
        .frequency_hz = 38000, // 38KHz
    };
    
    ret = rmt_apply_carrier(g_tx_channel, &carrier_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply RMT carrier: %d", ret);
        return ret;
    }

    ret = rmt_enable(g_tx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RMT TX channel: %d", ret);
        return ret;
    }

    g_current_protocol = protocol;
    update_current_ops(protocol);

    return ESP_OK;
}

esp_err_t ir_remote_deinit(void)
{
    if (g_tx_channel == NULL) {
        return ESP_OK;
    }
    
    rmt_disable(g_tx_channel);
    esp_err_t ret = rmt_del_channel(g_tx_channel);
    g_tx_channel = NULL;
    
    return ret;
}

esp_err_t ir_remote_set_protocol(ir_protocol_t protocol)
{
    ESP_LOGI(TAG, "Changing IR protocol from %d to %d", g_current_protocol, protocol);
    g_current_protocol = protocol;
    update_current_ops(protocol);
    return ESP_OK;
}

static esp_err_t send_signal(const rmt_symbol_word_t* signal_data, size_t signal_size)
{
    if (g_runtime_config.ir_protocol == IR_PROTOCOL_NONE) {
        ESP_LOGI(TAG, "IR transmission disabled per config, ignoring request");
        return ESP_FAIL;
    }
    if (g_tx_channel == NULL) {
        ESP_LOGE(TAG, "IR remote not initialized");
        return ESP_FAIL;
    }

    rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
    };

    rmt_encoder_handle_t copy_encoder = NULL;
    rmt_copy_encoder_config_t copy_encoder_config = {};
    esp_err_t ret = rmt_new_copy_encoder(&copy_encoder_config, &copy_encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT copy encoder: %d (%s)", ret, esp_err_to_name(ret));
        return ret;
    }

    ret = rmt_transmit(g_tx_channel, copy_encoder, signal_data, signal_size, &transmit_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to transmit RMT signal: %d (%s)", ret, esp_err_to_name(ret));
    } else {
        ret = rmt_tx_wait_all_done(g_tx_channel, -1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "RMT wait all done failed: %d (%s)", ret, esp_err_to_name(ret));
        }
    }
    rmt_del_encoder(copy_encoder);

    return ret;
}

static esp_err_t bose_turn_audio_on(void)
{
    ESP_LOGI(TAG, "Transmitting BOSE Audio ON (AUX) signal...");
    return send_signal(bose_aux_signal, sizeof(bose_aux_signal));
}

static esp_err_t bose_turn_audio_off(void)
{
    ESP_LOGI(TAG, "Transmitting BOSE Audio OFF sequence (AUX -> 100ms -> TOGGLE)...");
    esp_err_t ret = send_signal(bose_aux_signal, sizeof(bose_aux_signal));
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    return send_signal(bose_on_off_signal, sizeof(bose_on_off_signal));
}

static esp_err_t bose_toggle_audio(void)
{
    ESP_LOGI(TAG, "Transmitting BOSE Audio TOGGLE (ON/OFF base) signal...");
    return send_signal(bose_on_off_signal, sizeof(bose_on_off_signal));
}

static esp_err_t dubas_radio_turn_audio_on(void)
{
    ESP_LOGI(TAG, "Transmitting DUBAS_RADIO Audio ON (AUX) signal...");
    return send_signal(dubas_radio_aux_signal, sizeof(dubas_radio_aux_signal));
}

static esp_err_t dubas_radio_turn_audio_off(void)
{
    ESP_LOGI(TAG, "Transmitting DUBAS_RADIO Audio OFF sequence (AUX -> 100ms -> TOGGLE)...");
    esp_err_t ret = send_signal(dubas_radio_aux_signal, sizeof(dubas_radio_aux_signal));
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    return send_signal(dubas_radio_on_off_signal, sizeof(dubas_radio_on_off_signal));
}

static esp_err_t dubas_radio_toggle_audio(void)
{
    ESP_LOGI(TAG, "Transmitting DUBAS_RADIO Audio TOGGLE (ON/OFF base) signal...");
    return send_signal(dubas_radio_on_off_signal, sizeof(dubas_radio_on_off_signal));
}

esp_err_t ir_remote_turn_audio_on(void)
{
    if (g_current_ops && g_current_ops->turn_audio_on) {
        esp_err_t ret = g_current_ops->turn_audio_on();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to turn Audio ON with ir_remote: %s",
                esp_err_to_name(ret));
        }
        return ret;
    }
    
    ESP_LOGW(TAG, "Turn audio ON not supported for protocol %d", g_current_protocol);
    return ESP_OK;
}

esp_err_t ir_remote_turn_audio_off(void)
{
    if (g_current_ops && g_current_ops->turn_audio_off) {
        return g_current_ops->turn_audio_off();
    }
    
    ESP_LOGW(TAG, "Turn audio OFF not supported for protocol %d", g_current_protocol);
    return ESP_OK;
}

esp_err_t ir_remote_toggle_audio(void)
{
    if (g_current_ops && g_current_ops->toggle_audio) {
        return g_current_ops->toggle_audio();
    }
    
    ESP_LOGW(TAG, "Toggle audio not supported for protocol %d", g_current_protocol);
    return ESP_OK;
}
