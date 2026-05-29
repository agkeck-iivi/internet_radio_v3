#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the IR remote component.
 * 
 * @param tx_gpio_num GPIO pin connected to the IR transmitter.
 * @param protocol The default IR protocol to use.
 * @return ESP_OK on success, otherwise an error code.
 */
esp_err_t ir_remote_init(gpio_num_t tx_gpio_num, ir_protocol_t protocol);

/**
 * @brief De-initializes the IR remote component and frees resources.
 * 
 * @return ESP_OK on success, otherwise an error code.
 */
esp_err_t ir_remote_deinit(void);

/**
 * @brief Sets the active IR protocol dynamically.
 * 
 * @param protocol The new IR protocol to use.
 * @return ESP_OK on success, otherwise an error code.
 */
esp_err_t ir_remote_set_protocol(ir_protocol_t protocol);

/**
 * @brief Sends the "Turn Audio On" signal for the active protocol.
 * 
 * @return ESP_OK on success, otherwise an error code.
 */
esp_err_t ir_remote_turn_audio_on(void);

/**
 * @brief Sends the "Turn Audio Off" signal for the active protocol.
 * 
 * @return ESP_OK on success, otherwise an error code.
 */
esp_err_t ir_remote_turn_audio_off(void);

/**
 * @brief Sends a toggle or power signal depending on the active protocol.
 * 
 * @return ESP_OK on success, otherwise an error code.
 */
esp_err_t ir_remote_toggle_audio(void);

#ifdef __cplusplus
}
#endif
