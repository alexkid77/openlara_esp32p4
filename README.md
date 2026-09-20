# OpenLara ESP32-P4

A port of [OpenLara](https://github.com/XProger/OpenLara) to the **ESP32-P4-Function-EV-Board**.
Runs the classic Tomb Raider 1 engine with software rendering, audio, and USB HID keyboard input on Espressif's latest RISC-V SoC.

**Author:** [Alejandro Villegas Alonso](https://www.linkedin.com/in/alejandro-villegas-alonso-825041b1/)

## Features

- Software RGB565 renderer at 320x240, hardware-scaled to 1024x600 via the PPA (Pixel Processing Accelerator)
- 44.1 kHz stereo audio via I2S + ES8311 codec
- USB HID keyboard input 
- MicroSD card  for game data
- On-screen FPS counter (F12 toggle)


## Demo

![OpenLara ESP32-P4 Demo](openlara_demo.gif)

## Hardware Requirements

| Component | Details |
|-----------|---------|
| Board | ESP32-P4-Function-EV-Board |
| SoC | ESP32-P4, RISC-V dual-core @ 400 MHz (chip rev < 3.0) |
| Flash | 16 MB |
| PSRAM | 32 MB (SPIRAM, HEX mode, 200 MHz) |
| Display | 1024x600 MIPI DSI LCD (EK79007) |
| Audio | ES8311 codec via I2S |
| Storage | MicroSD card  |
| Input | USB HID keyboard (required) |

## Building

### Prerequisites

- ESP-IDF v5.4+ (tested with v5.4.4 / v5.5.5)

### Build & Flash

```bash

# Build
idf.py build

# Flash (adjust port as needed)
idf.py -p /dev/ttyUSB0 flash monitor
```

## Game Data

You must provide your own **Tomb Raider 1** data files (`.PHD` levels, `.PCX` images, cutscenes) inside a **`data`** folder on the MicroSD card. The game will not run without them.

## Controls

A **USB keyboard** must be connected to the board.

| Key | Action |
|-----|--------|
| Arrow keys | Movement / Camera |
| Ctrl | Action (draw weapon, grab, interact) |
| Shift | Walk |
| Alt | Step / Look |
| Space | Jump |
| F12 | Toggle FPS counter |

## Technical Notes

- Software rendering is based on the **MS-DOS** flavor of OpenLara (not the GL renderer), using the header-only `gapi/sw.h` engine compiled as C++11
- Near-plane clipping patch applied for the software renderer
- FreeRTOS dual-core with a 1000 Hz tick; the audio pump task runs on core 0
- All game memory is allocated from PSRAM; internal SRAM is reserved for DMA and task stacks
- Saves and cache are written to the SD card

## Credits

- [XProger/OpenLara](https://github.com/XProger/OpenLara) — the original open-source Tomb Raider 1 engine

## License

OpenLara is licensed under the [GPL-3.0](https://github.com/XProger/OpenLara/blob/master/LICENSE).
