#ifndef INTERNET_RADIO_ADF_H
#define INTERNET_RADIO_ADF_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Changes the radio station, handling pipeline destruction and creation.
 * @param new_station_index The index of the new station to play.
 */
void change_station(int new_station_index);

/**
 * @brief Resets the watchdog counter to avoid spurious restarts after sleep.
 */
void reset_watchdog_counter(void);

/**
 * @brief Waits for the Wi-Fi connection to be established.
 */
void wait_for_wifi_connection(void);

/**
 * @brief Sets the Wi-Fi sleep mode (disconnects if true, connects if false).
 */
void set_wifi_sleep_mode(bool sleeping);

#endif // INTERNET_RADIO_ADF_H