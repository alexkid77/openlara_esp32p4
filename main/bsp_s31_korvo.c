#include "bsp_s31_korvo.h"
#include "driver/gpio.h"
#include "driver/i2s_tdm.h"
#include "driver/sdmmc_host.h"
#include "esp_codec_dev_defaults.h"
#include "esp_check.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BSP_S31_KORVO";
static i2c_master_bus_handle_t s_i2c_bus;
static i2s_chan_handle_t s_i2s_tx_chan;

esp_err_t bsp_s31_init_hardware(bsp_s31_handles_t *handles) {
    if (!handles) return ESP_ERR_INVALID_ARG;

    // Same RGB565 timing and wiring as esp-board-manager/esp32_s31_korvo_1.
    esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = 18000000,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_pulse_width = 1,
            .hsync_back_porch = 40,
            .hsync_front_porch = 20,
            .vsync_pulse_width = 1,
            .vsync_back_porch = 10,
            .vsync_front_porch = 5,
            .flags.pclk_active_neg = true,
        },
        .data_width = 16,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 1,
        .dma_burst_size = 64,
        .hsync_gpio_num = GPIO_NUM_44,
        .vsync_gpio_num = GPIO_NUM_45,
        .de_gpio_num = GPIO_NUM_43,
        .pclk_gpio_num = GPIO_NUM_40,
        .disp_gpio_num = GPIO_NUM_NC,
        .data_gpio_nums = {8, 9, 10, 11, 12, 13, 14, 15,
                          16, 17, 18, 19, 33, 34, 35, 36},
        .flags.fb_in_psram = true,
    };
    esp_err_t ret = esp_lcd_new_rgb_panel(&cfg, &handles->panel_handle);
    if (ret != ESP_OK) return ret;
    ESP_ERROR_CHECK(esp_lcd_panel_reset(handles->panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(handles->panel_handle));

    i2c_master_bus_config_t i2c_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = 0,
        .sda_io_num = GPIO_NUM_0,
        .scl_io_num = GPIO_NUM_1,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ret = i2c_new_master_bus(&i2c_cfg, &s_i2c_bus);
    handles->i2c_bus = s_i2c_bus;
    if (ret != ESP_OK) ESP_LOGE(TAG, "I2C initialization failed: %s", esp_err_to_name(ret));
    return ret;
}

esp_err_t bsp_sdcard_mount(void) {
    // Active-low SD path power on GPIO39; enable it before SDMMC probes.
    gpio_config_t power_cfg = {
        .pin_bit_mask = 1ULL << GPIO_NUM_39,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&power_cfg), TAG, "SD power GPIO");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_39, 0), TAG, "SD power on");
    vTaskDelay(pdMS_TO_TICKS(20));

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16384,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = GPIO_NUM_24;
    slot.cmd = GPIO_NUM_25;
    slot.d0 = GPIO_NUM_20;
    slot.d1 = GPIO_NUM_21;
    slot.d2 = GPIO_NUM_22;
    slot.d3 = GPIO_NUM_23;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    sdmmc_card_t *card = NULL;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mount_cfg, &card);
    if (ret != ESP_OK) ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(ret));
    return ret;
}

void bsp_audio_init(void *arg) {
    (void)arg;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_i2s_tx_chan, NULL));
    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO,
            I2S_TDM_SLOT0 | I2S_TDM_SLOT1),
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = GPIO_NUM_3,
            .ws = GPIO_NUM_4,
            .dout = GPIO_NUM_5,
            .din = GPIO_NUM_NC,
        },
    };
    tdm_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_ERROR_CHECK(i2s_channel_init_tdm_mode(s_i2s_tx_chan, &tdm_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_i2s_tx_chan));
}

esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void) {
    if (!s_i2c_bus || !s_i2s_tx_chan) return NULL;
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = 0,
        .addr = ES8389_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .tx_handle = s_i2s_tx_chan,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    es8389_codec_cfg_t codec_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_TYPE_OUT,
        .pa_pin = GPIO_NUM_7,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = false,
        .hw_gain = {.pa_voltage = 5.0, .codec_dac_voltage = 3.3},
    };
    const audio_codec_if_t *codec_if = es8389_codec_new(&codec_cfg);
    if (!codec_if || !data_if) return NULL;
    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    return esp_codec_dev_new(&dev_cfg);
}
