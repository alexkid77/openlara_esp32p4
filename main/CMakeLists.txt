if(CONFIG_IDF_TARGET_ESP32P4)
    set(board_src "bsp_p4_eval.c")
    set(board_requires esp_lcd_touch esp_lcd_ek79007 esp_lcd_touch_gt911)
else()
    set(board_src "bsp_s31_korvo.c")
    set(board_requires)
endif()

idf_component_register(SRCS "main.c" "${board_src}" "openlara_esp32p4.cpp"
                       PRIV_REQUIRES esp_lcd esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_ledc esp_driver_ppa esp_driver_sdmmc esp_timer ${board_requires} vfs usb esp_codec_dev driver fatfs esp_mm openlara pthread
                       INCLUDE_DIRS "")

# The OpenLara engine is C++11 (like the original SDL12 build). In gnu++2b
# the global lerp() from utils.h collides with std::lerp from <math.h>.
# Also the engine warnings (memset on non-trivial types, ASSERTV, etc.)
# must not be fatal: IDF forces -Werror=all and -Wno-error=all does NOT
# undo it on GCC, -Wno-error=<specific> must be used instead.
set_source_files_properties(openlara_esp32p4.cpp PROPERTIES
    COMPILE_OPTIONS "-std=gnu++11;-Wno-error=class-memaccess;-Wno-error=unused-value;-Wno-error=misleading-indentation;-Wno-error=calloc-transposed-args;-Wno-error=nonnull-compare;-Wno-error=maybe-uninitialized;-Wno-error=type-limits;-Wno-error=implicit-fallthrough;-Wno-error=array-bounds;-Wno-error=empty-body;-Wno-error=unused-but-set-parameter;-Wno-error=strict-aliasing;-Wno-error=missing-field-initializers")
