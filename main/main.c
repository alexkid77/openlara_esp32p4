/**
 * @file main.c
 * @brief OpenLara application launcher for ESP32-P4 and ESP32-S31.
 *        Board hardware is selected by the IDF target.
 */

#include "openlara_esp32p4.h"
#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_lcd_mipi_dsi.h"
#else
#include "esp_lcd_panel_rgb.h"
#endif
#include "esp_log.h"

static const char *TAG = "APP_MAIN";

static uint16_t *frame_buffer;

void app_main(void) {
  ESP_LOGI(TAG, "Starting OpenLara...");

#if CONFIG_IDF_TARGET_ESP32P4
  bsp_p4_handles_t bsp_handles = {0};
  ESP_ERROR_CHECK(bsp_p4_init_hardware(&bsp_handles));
  void *fb0 = NULL;
  ESP_ERROR_CHECK(
      esp_lcd_dpi_panel_get_frame_buffer(bsp_handles.panel_handle, 1, &fb0));
#else
  bsp_s31_handles_t bsp_handles = {0};
  ESP_ERROR_CHECK(bsp_s31_init_hardware(&bsp_handles));
  void *fb0 = NULL;
  ESP_ERROR_CHECK(
      esp_lcd_rgb_panel_get_frame_buffer(bsp_handles.panel_handle, 1, &fb0));
#endif
  frame_buffer = (uint16_t *)fb0;

  openlara_Start(frame_buffer);
}
