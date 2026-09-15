#pragma once

#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "driver/i2c_master.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LCD_H_RES 800
#define LCD_V_RES 480
#define BSP_BOOT_BUTTON_GPIO GPIO_NUM_61

typedef struct {
    esp_lcd_panel_handle_t panel_handle;
    esp_lcd_touch_handle_t touch_handle;
    i2c_master_bus_handle_t i2c_bus;
} bsp_s31_handles_t;

esp_err_t bsp_s31_init_hardware(bsp_s31_handles_t *handles);
esp_err_t bsp_sdcard_mount(void);
void bsp_audio_init(void *arg);
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void);

#ifdef __cplusplus
}
#endif
