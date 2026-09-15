/**
 * @file openlara_esp32p4.cpp
 * @brief OpenLara -> ESP32-P4 / ESP32-S31 adapter.
 *
 * Replaces src/platform/sdl12/main.cpp from the SDL 1.2 port:
 *   - Video  : 320x240 RGB565 framebuffer (software renderer) scaled to
 *              1024x600 on P4 or 800x480 on S31 by the PPA peripheral.
 *   - Input  : USB HID Host keyboard with a FreeRTOS queue (ESP32P4DOOM
 *              pattern), mapped to OpenLara's InputKey enum.
 *   - Data   : .PHD/.PCX levels and settings/saves from the SD card.
 *   - Audio  : Sound::fill() mixer pump (44100 Hz stereo int16) -> I2S ->
 *              ES8311 (P4) or ES8389 (S31), the equivalent of the SDL_OpenAudio callback
 *              of the SDL12 port.
 *
 * THIS IS THE ONLY TU THAT INCLUDES game.h (the engine is header-only:
 * contentDir/cacheDir/saveDir and the remaining globals live here).
 */

#include "openlara_esp32p4.h"

#include "driver/ppa.h"
#include "driver/gpio.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include "usb/usb_host.h"
#include <string.h>

// --- OpenLara engine (single translation unit) ---
#include "game.h"

static const char *TAG = "OPENLARA_ESP";

#define GAME_W 320
#define GAME_H 240

#define GAME_TASK_STACK (64 * 1024)

// ------------------------------------------------------------------
// Video buffers
// ------------------------------------------------------------------
static uint16_t *game_rb565;          // 320x240 RGB565: software renderer target and PPA input
static uint16_t *global_frame_buffer; // 1024x600 framebuffer del panel DPI
static ppa_client_handle_t ppa_client;
#if CONFIG_OPENLARA_TOUCH_CONTROL
static esp_lcd_touch_handle_t game_touch_handle;
static bool touch_overlay_visible = true;
static bool boot_button_ready;
#endif

// ------------------------------------------------------------------
// Engine OS hooks (the mutex/cache hooks come from utils.h:
// OS_PTHREAD_MT + OS_FILEIO_CACHE use pthread and fopen over cacheDir)
// ------------------------------------------------------------------
int osGetTimeMS() {
    return int(esp_timer_get_time() / 1000);
}

bool osJoyReady(int) { return false; }
void osJoyVibrate(int, float, float) {}

// ------------------------------------------------------------------
// Video: init + present (PPA scale 320x240 -> 1024x600, like ESP32P4DOOM)
// ------------------------------------------------------------------
static void video_init(uint16_t *frame_buffer) {
    global_frame_buffer = frame_buffer;

    game_rb565 = (uint16_t *)heap_caps_aligned_alloc(
        64, GAME_W * GAME_H * 2, MALLOC_CAP_INTERNAL);
    assert(game_rb565);
    memset(game_rb565, 0, GAME_W * GAME_H * 2);

    // same as initVideo() in the SDL12 port
    Core::width  = GAME_W;
    Core::height = GAME_H;
    GAPI::swColor = (GAPI::ColorSW *)game_rb565;
    GAPI::resize();

    ppa_client_config_t ppa_config = { .oper_type = PPA_OPERATION_SRM };
    ESP_ERROR_CHECK(ppa_register_client(&ppa_config, &ppa_client));
}

static void video_present() {
    esp_cache_msync(game_rb565, GAME_W * GAME_H * 2,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    ppa_srm_oper_config_t srm_config = {
        .in = { .buffer = game_rb565,
                .pic_w = GAME_W, .pic_h = GAME_H,
                .block_w = GAME_W, .block_h = GAME_H,
                .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
        .out = { .buffer = global_frame_buffer,
                 .buffer_size = LCD_H_RES * LCD_V_RES * 2,
                 .pic_w = LCD_H_RES, .pic_h = LCD_V_RES,
                 .srm_cm = PPA_SRM_COLOR_MODE_RGB565 },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = (float)LCD_H_RES / (float)GAME_W,
        .scale_y = (float)LCD_V_RES / (float)GAME_H,
    };

    ppa_do_scale_rotate_mirror(ppa_client, &srm_config);
    esp_cache_msync(global_frame_buffer, LCD_H_RES * LCD_V_RES * 2,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

// ------------------------------------------------------------------
// Health/oxygen bars overlay.
//
// The software rasterizer (_GAPI_SW + SPLIT_BY_TILE) never builds the
// CommonTex[CTEX_HEALTH] texture used by UI::renderBar(), so health/oxygen
// bars are invisible. Draw them directly on the 320x240 RGB565 framebuffer
// after Game::render(), at the same UI position scaled 640x480 -> 320x240.
// ------------------------------------------------------------------
static inline uint16_t sw_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return uint16_t(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static inline uint16_t sw_blend(uint16_t src, uint16_t dst) {
    // 50% alpha blend of two RGB565 pixels
    uint16_t r = ((src >> 11) + (dst >> 11)) >> 1;
    uint16_t g = (((src >> 5) & 0x3F) + ((dst >> 5) & 0x3F)) >> 1;
    uint16_t b = ((src & 0x1F) + (dst & 0x1F)) >> 1;
    return (r << 11) | (g << 5) | b;
}

static void sw_fill_rect(int x0, int y0, int w, int h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    for (int y = y0; y < y0 + h; y++) {
        if (y < 0 || y >= GAME_H) continue;
        uint16_t *row = game_rb565 + y * GAME_W;
        for (int x = x0; x < x0 + w; x++) {
            if (x < 0 || x >= GAME_W) continue;
            row[x] = color;
        }
    }
}

static void sw_blend_rect_half(int x0, int y0, int w, int h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    for (int y = y0; y < y0 + h; y++) {
        if (y < 0 || y >= GAME_H) continue;
        uint16_t *row = game_rb565 + y * GAME_W;
        for (int x = x0; x < x0 + w; x++) {
            if (x < 0 || x >= GAME_W) continue;
            row[x] = sw_blend(row[x], color);
        }
    }
}

static float sw_clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static void draw_health_bar() {
    if (!Game::level) return;
    Level *lvl = Game::level;

    // same early-outs as Level::renderUI()
    if (lvl->level.isTitle() || lvl->level.isCutsceneLevel()) return;
    if (inventory && inventory->titleTimer > 1.0f) return;
    if (inventory && inventory->active) return;

    Lara *player = lvl->players[0];
    if (!player) return;
    if (player->camera && player->camera->spectator) return;

    // UI coordinates are 640x480; the framebuffer is 320x240 (0.5x scale)
    const int w  = 180 / 2;   // bar width
    const int h  = 10 / 2;    // bar height
    const int x0 = (640 - 32 - 180) / 2;
    int y0 = 32 / 2;

    const uint16_t brTop = sw_rgb565(0x4C, 0x50, 0x4C); // brColor1
    const uint16_t brBot = sw_rgb565(0x74, 0x84, 0x74); // brColor2
    const uint16_t black = sw_rgb565(0x00, 0x00, 0x00);

    bool blink = (osGetTimeMS() / 500) & 1;

    float health = sw_clampf(player->health / float(LARA_MAX_HEALTH), 0.0f, 1.0f);
    if (blink && health <= 0.2f) health = 0.0f;

    // oxygen bar (above health bar, underwater only)
    float oxygen = 0.0f;
    bool showOxygen = !player->dozy &&
        (player->stand == Lara::STAND_ONWATER || player->stand == Lara::STAND_UNDERWATER);
    if (showOxygen) {
        oxygen = sw_clampf(player->oxygen / float(LARA_MAX_OXYGEN), 0.0f, 1.0f);
        if (blink && oxygen <= 0.2f) oxygen = 0.0f;
        int oy = y0 - 16 / 2; // UI pos.y += 16 -> 8 px up
        sw_fill_rect(x0 - 1, oy - 1, w + 2, 1, brTop);
        sw_fill_rect(x0 - 1, oy + h, w + 2, 1, brBot);
        sw_fill_rect(x0 - 1, oy - 1, 1, h + 2, brTop);
        sw_fill_rect(x0 + w, oy - 1, 1, h + 2, brBot);
        sw_blend_rect_half(x0, oy, w, h, black);
        int fw = int(float(w) * oxygen + 0.5f);
        if (fw > 0)
            sw_fill_rect(x0, oy, fw, h, sw_rgb565(0x64, 0x74, 0x64)); // oxygen fill
    }

    // health bar (original visibility: weapon w/ ammo, recent damage, or low health)
    bool showHealth = (player->wpnReady() && !player->emptyHands())
                   || player->damageTime > 0.0f
                   || health <= 0.2f;
    if (showHealth) {
        sw_fill_rect(x0 - 1, y0 - 1, w + 2, 1, brTop);
        sw_fill_rect(x0 - 1, y0 + h, w + 2, 1, brBot);
        sw_fill_rect(x0 - 1, y0 - 1, 1, h + 2, brTop);
        sw_fill_rect(x0 + w, y0 - 1, 1, h + 2, brBot);
        sw_blend_rect_half(x0, y0, w, h, black);

        int fw = int(float(w) * health + 0.5f);
        if (fw > 0) {
            sw_fill_rect(x0, y0, fw, 1, sw_rgb565(0x5E, 0x81, 0xAE));          // top highlight
            sw_fill_rect(x0, y0 + 1, fw, h - 2, sw_rgb565(0x2C, 0x5D, 0x71));  // main fill
            sw_fill_rect(x0, y0 + h - 1, fw, 1, sw_rgb565(0x16, 0x30, 0x4F));  // bottom shade
        }
    }
}

#if CONFIG_OPENLARA_TOUCH_CONTROL
// Software GAPI ignores shader uniform updates, so UI::renderTouch() cannot
// place its circle mesh at the button positions. Draw the controls on the
// RGB565 game buffer after Game::render(), before the PPA scales it.
static void sw_touch_disc(int cx, int cy, int radius, bool active) {
    const uint16_t black = sw_rgb565(0, 0, 0);
    const uint16_t rim = active ? sw_rgb565(255, 230, 135)
                                : sw_rgb565(125, 225, 255);
    int outer2 = radius * radius;
    int inner2 = (radius - 2) * (radius - 2);
    int x0 = cx - radius, x1 = cx + radius;
    int y0 = cy - radius, y1 = cy + radius;
    if (x0 < 0) x0 = 0;
    if (x1 >= GAME_W) x1 = GAME_W - 1;
    if (y0 < 0) y0 = 0;
    if (y1 >= GAME_H) y1 = GAME_H - 1;
    for (int y = y0; y <= y1; y++) {
        uint16_t *row = game_rb565 + y * GAME_W;
        for (int x = x0; x <= x1; x++) {
            int dx = x - cx, dy = y - cy;
            int dist2 = dx * dx + dy * dy;
            if (dist2 > outer2) continue;
            row[x] = dist2 >= inner2 ? rim : sw_blend(row[x], black);
        }
    }
}

static uint16_t sw_touch_glyph(char c) {
    // Five rows of three pixels, packed from top to bottom.
    switch (c) {
        case 'A': return 0b010101111101101;
        case 'C': return 0b011100100100011;
        case 'I': return 0b111010010010111;
        case 'J': return 0b001001001101010;
        case 'K': return 0b101110100110101;
        case 'L': return 0b100100100100111;
        case 'M': return 0b101111111101101;
        case 'N': return 0b101111111111101;
        case 'P': return 0b110101110100100;
        case 'V': return 0b101101101101010;
        case 'W': return 0b101101111111101;
        default:  return 0;
    }
}

static void sw_touch_label(int cx, int cy, const char *label) {
    const uint16_t white = sw_rgb565(255, 255, 255);
    for (int letter = 0; letter < 2; letter++) {
        uint16_t glyph = sw_touch_glyph(label[letter]);
        for (int row = 0; row < 5; row++)
            for (int col = 0; col < 3; col++)
                if (glyph & (1u << (14 - row * 3 - col)))
                    sw_fill_rect(cx - 7 + letter * 8 + col * 2,
                                 cy - 5 + row * 2, 2, 2, white);
    }
}

static void sw_touch_button(const vec2 &pos, int radius, bool active,
                            const char *label) {
    int x = int(pos.x + 0.5f), y = int(pos.y + 0.5f);
    sw_touch_disc(x, y, radius, active);
    sw_touch_label(x, y, label);
}

static void draw_touch_overlay() {
    if (!touch_overlay_visible || !game_touch_handle || !Game::level) return;

    const int radius = 14;
    if (Input::btnEnable[Input::bMove]) {
        float offset = Input::getTouchHeight() * 0.25f;
        float marker_offset = offset * 1.40710678f;
        vec2 base(marker_offset, Input::getTouchHeight() - marker_offset);
        InputKey key = Input::touchKey[Input::zMove];
        if (key != ikNone && Input::down[key]) {
            const Input::Touch &touch = Input::touch[key - ikTouchA];
            base = touch.start;
        }
        sw_touch_button(base, radius + 5, key != ikNone, "MV");
        if (key != ikNone && Input::down[key]) {
            const Input::Touch &touch = Input::touch[key - ikTouchA];
            sw_touch_disc(int(touch.pos.x + 0.5f), int(touch.pos.y + 0.5f), 8, true);
        }

        // OpenLara's middle third accepts look gestures but has no static marker.
        InputKey look_key = Input::touchKey[Input::zLook];
        vec2 look_base(GAME_W * 0.5f, GAME_H * 0.65f);
        if (look_key != ikNone && Input::down[look_key])
            look_base = Input::touch[look_key - ikTouchA].start;
        sw_touch_button(look_base, radius + 5, look_key != ikNone, "LK");
        if (look_key != ikNone && Input::down[look_key]) {
            const Input::Touch &touch = Input::touch[look_key - ikTouchA];
            sw_touch_disc(int(touch.pos.x + 0.5f), int(touch.pos.y + 0.5f), 8, true);
        }
    }

    static const char *labels[Input::bMAX] = {"MV", "WP", "WK", "AC", "JP", "IN"};
    for (int i = Input::bWeapon; i < Input::bMAX; i++) {
        if (!Input::btnEnable[i]) continue;
        int button_radius = i == Input::bInventory ? 11 : radius;
        bool active = Input::touchKey[Input::zButton] != ikNone && Input::btn == i;
        sw_touch_button(Input::btnPos[i], button_radius, active, labels[i]);
    }
}
#endif

// ------------------------------------------------------------------
// Input: USB HID keyboard -> OpenLara InputKey (via FreeRTOS queue)
// ------------------------------------------------------------------
typedef struct {
    int pressed; // 1 = DOWN, 0 = UP
    int key;     // InputKey
} key_event_t;

static QueueHandle_t key_queue;

// HID usage -> InputKey (equivalent to codeToInputKey in the SDL12 port)
static int hidToInputKey(unsigned char usage) {
    if (usage >= 0x04 && usage <= 0x1D) return ikA  + (usage - 0x04); // A..Z
    if (usage >= 0x1E && usage <= 0x26) return ik1  + (usage - 0x1E); // 1..9
    if (usage >= 0x3A && usage <= 0x45) return ikF1 + (usage - 0x3A); // F1..F12
    switch (usage) {
        case 0x27: return ik0;       // 0
        case 0x28: return ikEnter;
        case 0x29: return ikEscape;
        case 0x2A: return ikBack;    // Backspace
        case 0x2B: return ikTab;
        case 0x2C: return ikSpace;
        case 0x2D: return ikMinus;
        case 0x2E: return ikPlus;
        case 0x2F: return ikLSB;     // [
        case 0x30: return ikRSB;     // ]
        case 0x31: return ikBSlash;  // backslash
        case 0x33: return ikColon;   // ;
        case 0x34: return ikApos;    // '
        case 0x35: return ikTilda;   // `
        case 0x36: return ikComma;
        case 0x37: return ikDot;
        case 0x38: return ikSlash;
        case 0x49: return ikIns;
        case 0x4A: return ikHome;
        case 0x4B: return ikPrev;    // PageUp
        case 0x4C: return ikDel;
        case 0x4D: return ikEnd;
        case 0x4E: return ikNext;    // PageDown
        case 0x4F: return ikRight;
        case 0x50: return ikLeft;
        case 0x51: return ikDown;
        case 0x52: return ikUp;
    }
    return ikNone;
}

static void queue_key(int key, int pressed) {
    if (key == ikNone)
        return;
    key_event_t ev;
    ev.pressed = pressed;
    ev.key = key;
    xQueueSend(key_queue, &ev, 0);
}

static void hid_keyboard_report_callback(const uint8_t *const data,
                                         const int length) {
    hid_keyboard_input_report_boot_t *kb =
        (hid_keyboard_input_report_boot_t *)data;
    if (length < (int)sizeof(hid_keyboard_input_report_boot_t))
        return;

    static uint8_t prev_keys[HID_KEYBOARD_KEY_MAX] = { 0 };

    // released keys
    for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
        if (prev_keys[i]) {
            bool found = false;
            for (int j = 0; j < HID_KEYBOARD_KEY_MAX; j++)
                if (kb->key[j] == prev_keys[i])
                    found = true;
            if (!found)
                queue_key(hidToInputKey(prev_keys[i]), 0);
        }
    }
    // pressed keys
    for (int i = 0; i < HID_KEYBOARD_KEY_MAX; i++) {
        if (kb->key[i]) {
            bool found = false;
            for (int j = 0; j < HID_KEYBOARD_KEY_MAX; j++)
                if (prev_keys[j] == kb->key[i])
                    found = true;
            if (!found)
                queue_key(hidToInputKey(kb->key[i]), 1);
        }
    }

    // modifiers (as in ESP32P4DOOM)
    static uint8_t old_mods = 0;
    uint8_t mods = kb->modifier.val;

    if ((mods & 0x11) && !(old_mods & 0x11)) queue_key(ikCtrl, 1);
    if (!(mods & 0x11) && (old_mods & 0x11)) queue_key(ikCtrl, 0);
    if ((mods & 0x22) && !(old_mods & 0x22)) queue_key(ikShift, 1);
    if (!(mods & 0x22) && (old_mods & 0x22)) queue_key(ikShift, 0);
    if ((mods & 0x44) && !(old_mods & 0x44)) queue_key(ikAlt, 1);
    if (!(mods & 0x44) && (old_mods & 0x44)) queue_key(ikAlt, 0);

    memcpy(prev_keys, kb->key, HID_KEYBOARD_KEY_MAX);
    old_mods = mods;
}

static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle,
                                        const hid_host_interface_event_t event,
                                        void *arg) {
    if (event == HID_HOST_INTERFACE_EVENT_INPUT_REPORT) {
        size_t data_length;
        uint8_t data[64];
        ESP_ERROR_CHECK(hid_host_device_get_raw_input_report_data(
            hid_device_handle, data, 64, &data_length));
        hid_host_dev_params_t dev_params;
        hid_host_device_get_params(hid_device_handle, &dev_params);
        if (dev_params.proto == HID_PROTOCOL_KEYBOARD) {
            hid_keyboard_report_callback(data, data_length);
        }
    } else if (event == HID_HOST_INTERFACE_EVENT_DISCONNECTED) {
        hid_host_device_close(hid_device_handle);
    }
}

static void hid_host_device_callback(hid_host_device_handle_t hid_device_handle,
                                     const hid_host_driver_event_t event,
                                     void *arg) {
    if (event == HID_HOST_DRIVER_EVENT_CONNECTED) {
        hid_host_dev_params_t dev_params;
        hid_host_device_get_params(hid_device_handle, &dev_params);
        if (dev_params.proto == HID_PROTOCOL_KEYBOARD) {
            const hid_host_device_config_t dev_config = {
                .callback = hid_host_interface_callback,
                .callback_arg = NULL };
            ESP_ERROR_CHECK(hid_host_device_open(hid_device_handle, &dev_config));
            if (HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class) {
                hid_class_request_set_protocol(hid_device_handle,
                                               HID_REPORT_PROTOCOL_BOOT);
                hid_class_request_set_idle(hid_device_handle, 0, 0);
            }
            ESP_ERROR_CHECK(hid_host_device_start(hid_device_handle));
            ESP_LOGI(TAG, "USB keyboard configured and ready!");
        }
    }
}

static void usb_lib_task(void *arg) {
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));
    xTaskNotifyGive((TaskHandle_t)arg);
    while (true) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    }
    vTaskDelete(NULL);
}

static void usb_init() {
    ESP_LOGI(TAG, "Initializing USB Host (HID)...");

    BaseType_t task_created = xTaskCreatePinnedToCore(
        usb_lib_task, "usb_events", 4096,
        xTaskGetCurrentTaskHandle(), 2, NULL, 0);
    assert(task_created == pdTRUE);
    ulTaskNotifyTake(false, 1000);

    const hid_host_driver_config_t hid_host_driver_config = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = hid_host_device_callback,
        .callback_arg = NULL };
    ESP_ERROR_CHECK(hid_host_install(&hid_host_driver_config));
}

// Feed tracked panel touches into OpenLara's six touch input slots.
#if CONFIG_OPENLARA_TOUCH_CONTROL
static void boot_button_init() {
    if (!game_touch_handle) return;

    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << BSP_BOOT_BUTTON_GPIO;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    esp_err_t ret = gpio_config(&config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "BOOT button unavailable: %s", esp_err_to_name(ret));
        return;
    }
    boot_button_ready = true;
}

static void boot_button_poll() {
    if (!boot_button_ready) return;

    static int last_sample_ms = -20;
    static int candidate_since_ms;
    static int candidate_level = 1;
    static int stable_level = 1;
    static bool sampled;
    int now = osGetTimeMS();
    if (now - last_sample_ms < 20) return;
    last_sample_ms = now;

    int level = gpio_get_level(BSP_BOOT_BUTTON_GPIO);
    if (!sampled) {
        candidate_level = stable_level = level;
        candidate_since_ms = now;
        sampled = true;
        return;
    }
    if (level != candidate_level) {
        candidate_level = level;
        candidate_since_ms = now;
    } else if (level != stable_level && now - candidate_since_ms >= 40) {
        stable_level = level;
        if (level == 0) {
            touch_overlay_visible = !touch_overlay_visible;
            ESP_LOGI(TAG, "Touch overlay: %s", touch_overlay_visible ? "ON" : "OFF");
        }
    }
}

static void touch_poll() {
    if (!game_touch_handle) return;

    static int last_poll_ms = -16;
    static int last_error_log_ms = -2000;
    int now = osGetTimeMS();
    if (now - last_poll_ms < 16) return;
    last_poll_ms = now;

    esp_err_t ret = esp_lcd_touch_read_data(game_touch_handle);
    esp_lcd_touch_point_data_t points[6];
    uint8_t point_count = 0;
    if (ret == ESP_OK)
        ret = esp_lcd_touch_get_data(game_touch_handle, points, &point_count, 6);
    if (ret != ESP_OK) {
        // Keep the previous state on a failed sample; a later good sample
        // will report releases without turning an I2C glitch into a tap.
        if (now - last_error_log_ms >= 2000) {
            ESP_LOGW(TAG, "Touch read failed: %s", esp_err_to_name(ret));
            last_error_log_ms = now;
        }
        return;
    }

    // Release missing track IDs before allocating slots for new fingers.
    for (int slot = 0; slot < 6; slot++) {
        InputKey key = InputKey(ikTouchA + slot);
        if (!Input::down[key]) continue;
        bool present = false;
        for (int i = 0; i < point_count; i++)
            if (Input::touch[slot].id == points[i].track_id) {
                present = true;
                break;
            }
        if (!present) Input::setDown(key, false);
    }

    for (int i = 0; i < point_count; i++) {
        InputKey key = Input::getTouch(points[i].track_id);
        if (key == ikNone) continue;

        // The PPA stretches the 320x240 game buffer over the whole panel.
        float x = float(points[i].x) * GAME_W / LCD_H_RES;
        float y = float(points[i].y) * GAME_H / LCD_V_RES;
        x = sw_clampf(x, 0.0f, float(GAME_W - 1));
        y = sw_clampf(y, 0.0f, float(GAME_H - 1));
        Input::setPos(key, vec2(x, y));
        Input::setDown(key, true);
    }
}
#endif

// drains the keyboard queue into Input::setDown (equivalent to SDL_PollEvent)
static void input_poll() {
    key_event_t ev;
    while (xQueueReceive(key_queue, &ev, 0) == pdTRUE) {
        if (ev.key == ikF12 && ev.pressed) {
            UI::showFPS = !UI::showFPS;
            ESP_LOGI(TAG, "FPS overlay: %s", UI::showFPS ? "ON" : "OFF");
        }
        Input::setDown(InputKey(ev.key), ev.pressed != 0);
    }
#if CONFIG_OPENLARA_TOUCH_CONTROL
    boot_button_poll();
    touch_poll();
#endif
}

// ------------------------------------------------------------------
// Audio: Sound::fill() mixer pump -> I2S -> ES8311 (44100 Hz stereo)
// ------------------------------------------------------------------
static esp_codec_dev_handle_t codec_dev = NULL;
static SemaphoreHandle_t audio_done_sem = NULL;

#define AUDIO_TASK_STACK (8 * 1024)
#define AUDIO_FRAMES 256 // 256 frames x 4 bytes = ~5.8 ms @ 44100 Hz

// Equivalent to the SDL_OpenAudio callback of the SDL12 port: calls
// Sound::fill() (the engine mixer, thread-safe via its own mutex) and then
// blocks in esp_codec_dev_write until the I2S DMA consumes the buffer,
// which sets the natural pace of the loop.
static void audio_task(void *arg) {
    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = 16,
        .channel = 2,
        .channel_mask = 0,
        .sample_rate = 44100,
        .mclk_multiple = 0,
    };

    if (esp_codec_dev_open(codec_dev, &sample_info) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Could not open the audio codec: no sound");
    } else {
        esp_codec_dev_set_out_vol(codec_dev, 70);

        Sound::Frame mix_buf[AUDIO_FRAMES];
        while (!Core::isQuit) {
            Sound::fill(mix_buf, AUDIO_FRAMES);
            if (esp_codec_dev_write(codec_dev, mix_buf, sizeof(mix_buf)) != ESP_CODEC_DEV_OK) {
                ESP_LOGE(TAG, "I2S write error: sound stopped");
                break;
            }
        }
        esp_codec_dev_close(codec_dev);
    }

    xSemaphoreGive(audio_done_sem);
    vTaskDelete(NULL);
}

// ------------------------------------------------------------------
// SD: locate the directory with the game data (.PHD/.PCX)
// ------------------------------------------------------------------
static bool content_exists(const char *name) {
    return Stream::existsContent(name);
}

static bool game_data_found = false;

static bool probe_content_dir(const char *dir) {
    strcpy(contentDir, dir);
    bool found = content_exists("GYM.PHD") || content_exists("DATA/GYM.PHD") ||
                 content_exists("gym.phd") || content_exists("TITLE.PHD") ||
                 content_exists("DATA/TITLE.PHD") || content_exists("title.phd");
    if (found)
        game_data_found = true;
    return found;
}

static void sd_init_content() {
    cacheDir[0] = saveDir[0] = contentDir[0] = 0;

    if (bsp_sdcard_mount() != ESP_OK) {
        ESP_LOGE(TAG, "No SD card detected. OpenLara needs the game "
                      "data on the SD card.");
        strcpy(contentDir, "/sdcard/");
        return;
    }

    // look for the data in the usual locations
    static const char *candidates[] = {
        "/sdcard/OpenLara/", "/sdcard/openlara/", "/sdcard/DATA/",
        "/sdcard/data/", "/sdcard/"
    };
    for (size_t i = 0; i < COUNT(candidates); i++) {
        if (probe_content_dir(candidates[i])) {
            ESP_LOGI(TAG, "Game data at: %s", contentDir);
            // settings (cache) and saves in the same SD directory
            strcpy(cacheDir, contentDir);
            strcpy(saveDir, contentDir);
            return;
        }
    }

    ESP_LOGE(TAG, "SD mounted at /sdcard but GYM.PHD / TITLE.PHD not "
                  "found. Copy the .PHD and .PCX files to the SD card.");
    strcpy(contentDir, "/sdcard/");
}

// ------------------------------------------------------------------
// Game loop (equivalent to the while(!Core::isQuit) in the SDL12 port)
// ------------------------------------------------------------------
static void game_task(void *arg) {
    ESP_LOGI(TAG, "Renderer software 320x240 -> PPA -> %dx%d", LCD_H_RES,
             LCD_V_RES);

    Game::init((const char *)NULL);

    // audio pump: launched after Game::init() so Sound::init() and the
    // audio settings (with their defaults) are already applied
    if (codec_dev) {
        audio_done_sem = xSemaphoreCreateBinary();
        BaseType_t ok = xTaskCreatePinnedToCore(audio_task, "openlara_audio",
                                                AUDIO_TASK_STACK, NULL, 5, NULL, 0);
        assert(ok == pdTRUE);
    }

    int lastResLog = 0;

    while (!Core::isQuit) {
        input_poll();

        Game::update();
        Game::render();

        draw_health_bar();
#if CONFIG_OPENLARA_TOUCH_CONTROL
        draw_touch_overlay();
#endif

        video_present();

        // 1/s telemetry for diagnostics (internal heap, PSRAM, free stack)
        int now = osGetTimeMS();
        if (now - lastResLog >= 1000) {
            lastResLog = now;
            ESP_LOGI(TAG, "res: int=%u psram=%u stackHW=%u",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ESP_LOGW(TAG, "Game quit.");

    // stop the mixer before Game::deinit() -> Sound::deinit() frees the
    // channels the audio task could still be reading
    if (audio_done_sem) {
        if (!xSemaphoreTake(audio_done_sem, pdMS_TO_TICKS(2000)))
            ESP_LOGE(TAG, "audio task did not stop in time");
        vSemaphoreDelete(audio_done_sem);
        audio_done_sem = NULL;
    }

    Game::deinit();

    // if there was no game data, leave a visible pattern on screen
    if (!game_data_found) {
        for (int y = 0; y < GAME_H; y++)
            for (int x = 0; x < GAME_W; x++)
                game_rb565[y * GAME_W + x] =
                    (((x / 16) ^ (y / 16)) & 1) ? 0x001F : 0x0000;
        video_present();
    }

#if CONFIG_IDF_TARGET_ESP32S31
    vTaskDeleteWithCaps(NULL);
#else
    vTaskDelete(NULL);
#endif
}

// ------------------------------------------------------------------
// Entry point
// ------------------------------------------------------------------
extern "C" void openlara_Start(uint16_t *frame_buffer, esp_lcd_touch_handle_t touch_handle) {
#if CONFIG_OPENLARA_TOUCH_CONTROL
    ESP_LOGI(TAG, "Starting OpenLara (USB keyboard + touch)...");
    game_touch_handle = touch_handle;
#else
    ESP_LOGI(TAG, "Starting OpenLara (USB keyboard)...");
    (void)touch_handle;
#endif

    // 1. keyboard queue
    key_queue = xQueueCreate(32, sizeof(key_event_t));

    // 2. video (software framebuffer + PPA)
    video_init(frame_buffer);
#if CONFIG_OPENLARA_TOUCH_CONTROL
    boot_button_init();
#endif

    // 3. USB HID Host keyboard
    usb_init();

    // 4. SD: contentDir/cacheDir/saveDir
    sd_init_content();

    // 5. audio: I2S bus + board codec (the pump task is launched in
    //    game_task once Game::init() has run Sound::init())
    bsp_audio_init(NULL);
    codec_dev = bsp_audio_codec_speaker_init();
    if (!codec_dev)
        ESP_LOGE(TAG, "Audio codec init failed: continuing without sound");

    // 6. launch the game in its own task with a large stack (core 1)
#if CONFIG_IDF_TARGET_ESP32S31
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(
        game_task, "openlara", GAME_TASK_STACK, NULL, 5, NULL, 1,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    BaseType_t ok = xTaskCreatePinnedToCore(game_task, "openlara",
                                            GAME_TASK_STACK, NULL, 5, NULL, 1);
#endif
    assert(ok == pdTRUE);
}
