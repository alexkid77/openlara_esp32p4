/**
 * @file bsp_p4_eval.h
 * @brief Simplified BSP replacement for ESP32-P4-Function-EV-Board.
 * 
 * WHY THIS FILE?
 * Originally, the development kit uses a BSP (Board Support Package) library that 
 * abstracts the hardware. However, for total control over performance, 
 * memory, and debugging, we have extracted the essential logic and implemented 
 * direct calls to the ESP-IDF drivers.
 */

#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_lcd_types.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"
#include "esp_ldo_regulator.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Hardware Configurations and Justification --- */

/**
 * RESOLUTION: The LCD panel of this EVB is 1024x600.
 * This is a high resolution for a microcontroller, requiring 
 * a high-speed interface like MIPI DSI.
 */
#define LCD_H_RES              1024
#define LCD_V_RES              600

/**
 * MIPI DSI: High speed serial interface.
 * LCD_BITRATE_MBPS: 1000 Mbps is the standard for this screen to maintain 60Hz.
 * LCD_DSI_LANES: 2 data lanes are used to split the bandwidth.
 */
#define LCD_BITRATE_MBPS       1000
#define LCD_DSI_LANES          2

/**
 * GPIO PINS:
 * LCD_BACKLIGHT_GPIO (26): Controls the brightness via PWM.
 * LCD_RESET_GPIO (27): Physical pin to reset the LCD panel.
 * TOUCH_I2C_SCL/SDA (8/7): Dedicated I2C bus for the GT911 touch controller.
 */
#define LCD_BACKLIGHT_GPIO     GPIO_NUM_26
#define LCD_RESET_GPIO         GPIO_NUM_27
#define TOUCH_I2C_SCL          GPIO_NUM_8
#define TOUCH_I2C_SDA          GPIO_NUM_7

/**
 * POWER SUPPLY (LDO):
 * The MIPI DPHY block (the physical layer of DSI) on the ESP32-P4 requires 
 * a specific power supply managed by an internal LDO regulator.
 * MIPI_DPHY_LDO_CHAN (3): Channel assigned by the board design.
 * VOLTAGE (2500mV): Voltage level required for the PHY to work.
 */
#define MIPI_DPHY_LDO_CHAN     3
#define MIPI_DPHY_LDO_VOLTAGE_MV 2500

/**
 * @brief Handle Structure.
 * Groups all the "handles" necessary to manage the hardware lifecycle.
 */
typedef struct {
    esp_lcd_panel_handle_t panel_handle;    // The logical "brain" of the LCD panel
    esp_lcd_panel_io_handle_t io_handle;    // The communication path (DSI commands)
    esp_lcd_touch_handle_t touch_handle;    // The object to read coordinates from GT911
    esp_ldo_channel_handle_t ldo_handle;    // The power regulator control
    i2c_master_bus_handle_t i2c_bus;        // Master I2C bus for Touch and ES8311
} bsp_p4_handles_t;

/**
 * @brief Unified Initialization.
 * Configures step by step: Power -> Backlight -> DSI Bus -> LCD Panel -> Touch.
 */
esp_err_t bsp_p4_init_hardware(bsp_p4_handles_t *handles);

/**
 * @brief Custom Sound and SD Initialization
 */
esp_err_t bsp_sdcard_mount(void);
void bsp_audio_init(void *arg); // Dummy arg to retain signature compatibility
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void);

#ifdef __cplusplus
}
#endif
