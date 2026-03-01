#ifndef LVGL_SSD1306_SETUP_H
#define LVGL_SSD1306_SETUP_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the SSD1306 display, LVGL, and related tasks.
 * @return A pointer to the created LVGL display.
 */
lv_display_t *lvgl_ssd1306_setup(void);

/**
 * @brief Wakes up the display after sleep by re-initializing the panel.
 */
void lvgl_ssd1306_wakeup(void);

#ifdef __cplusplus
}
#endif

#endif // LVGL_SSD1306_SETUP_H
