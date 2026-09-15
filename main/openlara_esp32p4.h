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
 * @brief Initializes OpenLara (USB HID, SD, PPA) and launches the game task.
 * @param frame_buffer RGB565 panel framebuffer (1024x600 on P4, 800x480 on S31)
 */
void openlara_Start(uint16_t *frame_buffer);

#ifdef __cplusplus
}
#endif

#endif
