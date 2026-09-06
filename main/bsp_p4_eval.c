/**
 * @file bsp_p4_eval.c
 * @brief Detailed implementation to initialize the ESP32-P4-EV kit hardware.
 */

#include "bsp_p4_eval.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"
#include "driver/sdmmc_host.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_lcd_ek79007.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2s_chan_handle_t s_i2s_tx_chan = NULL;
static i2s_chan_handle_t s_i2s_rx_chan = NULL;

static const char *TAG = "BSP_P4_EVAL";

/**
 * PWM FOR BACKLIGHT (LEDC):
 * We use the LEDC peripheral because it allows generating precise and
 * stable PWM signals without CPU intervention. 5000Hz (5kHz) is high
 * enough so that the human eye does not perceive flicker.
 */
static esp_err_t init_backlight(void) {
  ESP_LOGI(TAG, "Configuring backlight PWM (LEDC) to 5kHz...");

  // The 'timer' defines the time base (frequency and resolution).
  const ledc_timer_config_t bkg_timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = LEDC_TIMER_10_BIT, // 2^10 = 1024 brightness levels
      .timer_num = LEDC_TIMER_1,
      .freq_hz = 5000,
      .clk_cfg = LEDC_AUTO_CLK};
  esp_err_t ret = ledc_timer_config(&bkg_timer);
  if (ret != ESP_OK)
    return ret;

  // The 'channel' links the timer with a physical pin (GPIO).
  const ledc_channel_config_t bkg_chan = {
      .gpio_num = LCD_BACKLIGHT_GPIO,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LEDC_CHANNEL_1,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = LEDC_TIMER_1,
      .duty = 1023, // Maximum value (100% brightness)
      .hpoint = 0};
  return ledc_channel_config(&bkg_chan);
}

/**
 * POWER MANAGEMENT (LDO):
 * The ESP32-P4 has internal voltage regulators (LDOs). Channel 3
 * is wired internally to the MIPI DSI circuitry. Without this,
 * the DSI bus cannot transmit differential voltages.
 */
static esp_err_t enable_dsi_phy_power(esp_ldo_channel_handle_t *chan) {
  ESP_LOGI(TAG, "Enabling internal LDO regulator for MIPI DPHY...");

  esp_ldo_channel_config_t ldo_cfg = {
      .chan_id = MIPI_DPHY_LDO_CHAN,
      .voltage_mv = MIPI_DPHY_LDO_VOLTAGE_MV, // 2.5V is nominal for DDR/DSI
  };
  return esp_ldo_acquire_channel(&ldo_cfg, chan);
}

esp_err_t bsp_p4_init_hardware(bsp_p4_handles_t *handles) {
  if (handles == NULL)
    return ESP_ERR_INVALID_ARG;
  esp_err_t ret;

  // 1. Power: Without this there is no electrical signal on the bus.
  ret = enable_dsi_phy_power(&handles->ldo_handle);
  if (ret != ESP_OK)
    return ret;

  // 2. Backlight: To make the panel visible.
  ret = init_backlight();
  if (ret != ESP_OK)
    return ret;

  // 3. MIPI DSI Bus: Configures the physical layer (lanes, speed).
  ESP_LOGI(TAG, "Configuring MIPI DSI bus at 1000 Mbps...");
  esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
  esp_lcd_dsi_bus_config_t bus_config = {
      .bus_id = 0,
      .num_data_lanes = LCD_DSI_LANES,
      .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
      .lane_bit_rate_mbps = LCD_BITRATE_MBPS,
  };
  ret = esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus);
  if (ret != ESP_OK)
    return ret;

  // 4. DBI IO: Used to send initialization commands to the internal
  // display controller (EK79007) over the DSI bus.
  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_dbi_io_config_t dbi_config = {
      .virtual_channel = 0,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
  };
  ret = esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io_handle);
  if (ret != ESP_OK)
    return ret;
  handles->io_handle = io_handle;

  // 5. LCD Panel: We define the DPI timings (Sync, Porch, Pixel clock).
  ESP_LOGI(TAG, "Initializing EK79007 controller and DPI timings...");
  esp_lcd_dpi_panel_config_t dpi_config =
      EK79007_1024_600_PANEL_60HZ_CONFIG_CF(LCD_COLOR_FMT_RGB565);
  dpi_config.num_fbs = 1;

  ek79007_vendor_config_t vendor_config = {
      .mipi_config =
          {
              .dsi_bus = mipi_dsi_bus,
              .dpi_config = &dpi_config,
          },
  };
  esp_lcd_panel_dev_config_t lcd_dev_config = {
      .bits_per_pixel = 16,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .reset_gpio_num = LCD_RESET_GPIO,
      .vendor_config = &vendor_config,
  };

  esp_lcd_panel_handle_t panel_handle = NULL;
  ret = esp_lcd_new_panel_ek79007(io_handle, &lcd_dev_config, &panel_handle);
  if (ret != ESP_OK)
    return ret;

  // Sequential process: Reset -> Init -> On
  esp_lcd_panel_reset(panel_handle);
  esp_lcd_panel_init(panel_handle);
  esp_lcd_panel_io_tx_param(io_handle, 0x29, NULL, 0); // "Display ON"
  handles->panel_handle = panel_handle;

  // 6. Touch (GT911): I2C Communication.
  ESP_LOGI(TAG, "Configuring GT911 touch controller over I2C...");

  // I2C Master: We define and initialize it here
  if (!s_i2c_bus) {
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = 0,
        .scl_io_num = TOUCH_I2C_SCL,
        .sda_io_num = TOUCH_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &s_i2c_bus));
  }
  handles->i2c_bus = s_i2c_bus;

  // IO Interface for the GT911: I2C address and protocol.
  esp_lcd_panel_io_handle_t tp_io_handle = NULL;
  esp_lcd_panel_io_i2c_config_t tp_io_cfg = {
      .dev_addr = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS,
      .scl_speed_hz = 400000,
      .control_phase_bytes = 1,
      .lcd_cmd_bits = 16,
      .flags.disable_control_phase = 1,
  };
  ret = esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_cfg, &tp_io_handle);
  if (ret != ESP_OK)
    return ret;

  // Logical touch configuration: Coordinate mapping and mirroring.
  esp_lcd_touch_config_t tp_cfg = {
      .x_max = LCD_H_RES,
      .y_max = LCD_V_RES,
      .rst_gpio_num = GPIO_NUM_NC,
      .int_gpio_num = GPIO_NUM_NC,
      .levels = {.reset = 0, .interrupt = 0},
      .flags = {.swap_xy = 0, .mirror_x = 1, .mirror_y = 1},
  };

  esp_lcd_touch_handle_t touch_handle = NULL;
  ret = esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &touch_handle);
  if (ret != ESP_OK)
    return ret;
  handles->touch_handle = touch_handle;

  ESP_LOGI(TAG, "Visual and touch hardware ready.");
  return ESP_OK;
}

// ---------------------------------------------------------
// RE-IMPLEMENTATION OF OFFICIAL BSP ABSTRACTIONS
// ---------------------------------------------------------

void bsp_audio_init(void *arg) {
  ESP_LOGI(TAG, "Initializing Native I2S Audio Bus...");
  // 1. Amplificador (Power Amp)
  gpio_config_t pa_conf = {
      .pin_bit_mask = (1ULL << GPIO_NUM_53),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = 0,
      .pull_down_en = 0,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&pa_conf);
  gpio_set_level(GPIO_NUM_53, 1);

  // 2. I2S STD Controller
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_i2s_tx_chan, &s_i2s_rx_chan));

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = GPIO_NUM_13,
              .bclk = GPIO_NUM_12,
              .ws = GPIO_NUM_10,
              .dout = GPIO_NUM_9,
              .din = GPIO_NUM_11,
              .invert_flags =
                  {
                      .mclk_inv = false,
                      .bclk_inv = false,
                      .ws_inv = false,
                  },
          },
  };
  std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s_tx_chan, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s_rx_chan, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(s_i2s_tx_chan));
}

esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void) {
  ESP_LOGI(TAG, "Initializing ES8311 Codec Natively...");
  if (!s_i2c_bus)
    return NULL;

  // GPIO Interface
  const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

  // I2C Interface
  audio_codec_i2c_cfg_t i2c_cfg = {
      .port = 0,
      .addr = ES8311_CODEC_DEFAULT_ADDR,
      .bus_handle = s_i2c_bus,
  };
  const audio_codec_ctrl_if_t *i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);

  // I2S Data Interface
  audio_codec_i2s_cfg_t i2s_cfg = {
      .port = I2S_NUM_0,
      .rx_handle = s_i2s_rx_chan,
      .tx_handle = s_i2s_tx_chan,
  };
  const audio_codec_data_if_t *i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);

  // ES8311 Codec initialization
  esp_codec_dev_hw_gain_t gain = {
      .pa_voltage = 5.0,
      .codec_dac_voltage = 3.3,
  };

  es8311_codec_cfg_t es8311_cfg = {
      .ctrl_if = i2c_ctrl_if,
      .gpio_if = gpio_if,
      .codec_mode = ESP_CODEC_DEV_TYPE_OUT,
      .pa_pin = GPIO_NUM_53,
      .pa_reverted = false,
      .master_mode = false,
      .use_mclk = true,
      .digital_mic = false,
      .invert_mclk = false,
      .invert_sclk = false,
      .hw_gain = gain,
  };
  const audio_codec_if_t *es8311_dev = es8311_codec_new(&es8311_cfg);

  // Create final esp_codec_dev handle
  esp_codec_dev_cfg_t codec_dev_cfg = {
      .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
      .codec_if = es8311_dev,
      .data_if = i2s_data_if,
  };
  return esp_codec_dev_new(&codec_dev_cfg);
}

esp_err_t bsp_sdcard_mount(void) {
  ESP_LOGI(TAG, "Mounting SDMMC Native Driver...");
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 5,
      .allocation_unit_size = 64 * 1024};

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

  // Power control for SD Card on the P4-EV-Board
  esp_ldo_channel_config_t ldo_sdc_config = {
      .chan_id = 4,
      .voltage_mv = 3300,
  };
  esp_ldo_channel_handle_t sd_ldo_handle = NULL;
  if (esp_ldo_acquire_channel(&ldo_sdc_config, &sd_ldo_handle) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to acquire LDO for SD Card");
    return ESP_FAIL;
  }

  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 4;
  slot_config.clk = GPIO_NUM_43;
  slot_config.cmd = GPIO_NUM_44;
  slot_config.d0 = GPIO_NUM_39;
  slot_config.d1 = GPIO_NUM_40;
  slot_config.d2 = GPIO_NUM_41;
  slot_config.d3 = GPIO_NUM_42;
  slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  sdmmc_card_t *card;
  esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config,
                                          &mount_config, &card);
  if (ret != ESP_OK) {
    if (sd_ldo_handle) {
      esp_ldo_release_channel(sd_ldo_handle);
    }
  }
  return ret;
}
