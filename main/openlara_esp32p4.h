#ifndef _OPENLARA_ESP32P4_H_
#define _OPENLARA_ESP32P4_H_

#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32P4
#include "bsp_p4_eval.h"
#else
#include "bsp_s31_korvo.h"
#endif
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes OpenLara (USB HID, touch, SD, PPA) and launches the game task.
 * @param frame_buffer RGB565 panel framebuffer (1024x600 on P4, 800x480 on S31)
 * @param touch_handle Board touch controller handle
 */
void openlara_Start(uint16_t *frame_buffer, esp_lcd_touch_handle_t touch_handle);

#ifdef __cplusplus
}
#endif

#endif
