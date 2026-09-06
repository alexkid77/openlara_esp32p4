# OpenLara is a header-only engine: the whole engine is compiled from
# main/openlara_esp32p4.cpp (the only TU that includes game.h).
# Only the libraries the engine references are compiled here:
#   - tinf      : zlib decompression of the levels (.PHD)
#   - minimp3   : MP3 decoder (referenced by sound.h, as in the SDL12 build)
#   - stb_vorbis: OGG decoder (referenced by sound.h, as in the SDL12 build)
set(OPENLARA_LIB_SRCS
    "src/libs/tinf/tinflate.c"
    "src/libs/minimp3/minimp3.cpp"
    "src/libs/stb_vorbis/stb_vorbis.c"
)

idf_component_register(
    SRCS "${OPENLARA_LIB_SRCS}"
    INCLUDE_DIRS "src"
    REQUIRES pthread
)

# __SDL12__ enables in core.h: _OS_SDL12 + _GAPI_SW (RGB565 software renderer),
# the same configuration as the reference SDL 1.2 port.
target_compile_definitions(${COMPONENT_LIB} PUBLIC __SDL12__)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -O2
    -Wall
    -Wno-error=all
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-sign-compare
    -Wno-unused-but-set-variable
    -Wno-maybe-uninitialized
    -Wno-format
    -Wno-misleading-indentation
    -Wno-calloc-transposed-args
    -ffunction-sections
    -fdata-sections
)
