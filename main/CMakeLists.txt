idf_component_register(SRCS "main.c" "bsp_p4_eval.c" "openlara_esp32p4.cpp"
                       PRIV_REQUIRES esp_lcd esp_lcd_touch esp_driver_ppa esp_timer esp_lcd_ek79007 esp_lcd_touch_gt911 vfs usb esp_codec_dev driver fatfs esp_mm openlara pthread
                       INCLUDE_DIRS "")

# The OpenLara engine is C++11 (like the original SDL12 build). In gnu++2b
# the global lerp() from utils.h collides with std::lerp from <math.h>.
# Also the engine warnings (memset on non-trivial types, ASSERTV, etc.)
# must not be fatal: IDF forces -Werror=all and -Wno-error=all does NOT
# undo it on GCC, -Wno-error=<specific> must be used instead.
set_source_files_properties(openlara_esp32p4.cpp PROPERTIES
    COMPILE_OPTIONS "-std=gnu++11;-Wno-error=class-memaccess;-Wno-error=unused-value;-Wno-error=misleading-indentation;-Wno-error=calloc-transposed-args;-Wno-error=nonnull-compare;-Wno-error=maybe-uninitialized")
