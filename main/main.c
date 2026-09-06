/**
 * @file main.c
 * @brief OpenLara (ESP32-P4) application launcher.
 *        Video, keyboard and audio/SPIFFS patterns taken from ESP32P4DOOM.
 */

#include "bsp_p4_eval.h"
#include "openlara_esp32p4.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_log.h"

static const char *TAG = "APP_MAIN";

static bsp_p4_handles_t bsp_handles;
static uint16_t *frame_buffer;

void app_main(void) {
  ESP_LOGI(TAG, "Starting OpenLara for ESP32-P4...");

  // Hardware (1024x600 MIPI DSI display + touch)
  ESP_ERROR_CHECK(bsp_p4_init_hardware(&bsp_handles));
  void *fb0 = NULL;
  ESP_ERROR_CHECK(
      esp_lcd_dpi_panel_get_frame_buffer(bsp_handles.panel_handle, 1, &fb0));
  frame_buffer = (uint16_t *)fb0;

  // Launch OpenLara (320x240 video scaled by PPA, USB HID keyboard, SD)
  openlara_Start(bsp_handles, frame_buffer);
}
