#ifndef _OPENLARA_ESP32P4_H_
#define _OPENLARA_ESP32P4_H_

#include "bsp_p4_eval.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes OpenLara (USB HID, SD, PPA) and launches the game task.
 * @param bsp_handles BSP handles (display)
 * @param frame_buffer 1024x600 RGB565 DPI panel framebuffer
 */
void openlara_Start(bsp_p4_handles_t bsp_handles, uint16_t *frame_buffer);

#ifdef __cplusplus
}
#endif

#endif
