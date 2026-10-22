---
title: "Boards"
description: "Which ESP32-S3 boards run the Game Day Scoreboard firmware, the MoonHub75 pinout, and the flash and PSRAM it needs."
sidebar:
  order: 2
---

The prebuilt firmware for DIY builds targets one pinout, the [MoonHub75](https://github.com/MoonModules/Hardware/tree/main/MOONHUB75)'s. Any board wired to those pins runs it. A board with different pins needs its own build.

## MoonHub75

The MoonHub75 has a prebuilt firmware. Install it from your browser with the [installer](/diy/install/) and it takes updates over Wi-Fi like any other panel.

Its pin map in the firmware also sets up an I2S microphone on GPIO10, GPIO11 and GPIO12, as the MoonHub75 carries one. The scoreboard pages don't use it.

## An ESP32-S3 board wired to the MoonHub75 pinout

The same prebuilt firmware runs on a bare ESP32-S3 board if you wire it to the same pins. It needs:

- An ESP32-S3 with 16 MB of flash and 8 MB of octal PSRAM, like a dev board with an N16R8 module.
- A button from GPIO0 to ground. The BOOT button on most dev boards is already wired that way. A click turns the panel on or off, and holding it shows the panel's address and resets Wi-Fi. See [Buttons and resets](/using/buttons-and-resets/).
- The panel wired to the pins below.

These are the GPIO numbers under the names ESPHome uses. The MoonHub75 README calls the upper half R0, G0 and B0, so its R0, G0 and B0 are R1, G1 and B1 here.

| R1 | G1 | B1 | R2 | G2 | B2 | A | B | C | D | E | LAT | OE | CLK |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 5 | 6 | 7 | 13 | 9 | 16 | 48 | 47 | 21 | 38 | 8 | 4 | 18 |

R1, G1 and B1 carry the colors for the top half of the panel, and R2, G2 and B2 the bottom half.

:::tip
Many panels read the ESP32's 3.3 V signals fine. If yours flickers or shows the wrong colors, put 5 V level shifters (74AHCT245 or similar) between the board and the panel.
:::

## Flash and PSRAM

The board settings come from `firmware/controllers/moonhub75.yaml`:

| Setting | Value |
| --- | --- |
| Board | `esp32-s3-devkitc-1` |
| Flash | 16 MB |
| PSRAM | octal, 80 MHz |
| CPU | 240 MHz |
| Framework | ESP-IDF |

The firmware leans on PSRAM. It runs code from PSRAM (`execute_from_psram`), and `firmware/gameday-common.yaml` moves the Bluetooth stack and the HTTPS buffers there to keep internal RAM free for the ESPN and update downloads. The build is set for octal PSRAM and 16 MB of flash, so pick a board that has both. An N16R8 module does.

## Other boards

Any other ESP32-S3 HUB75 board with at least 16 MB of flash and octal PSRAM, the Waveshare ESP32-S3 RGB Matrix among them, needs its own build. There's no prebuilt firmware for these boards and they haven't been tested. To make one:

1. Copy `firmware/controllers/moonhub75.yaml` and put your board's pins in it.
2. Copy `firmware/gameday.yaml` and point it at your controller file.
3. Build that with ESPHome. [Build it yourself](/diy/build-from-source/) has the steps.

Give your copy its own `variant` name in the top-level file. The variant picks the folder the panel checks for updates. A build that keeps `moonhub75` installs the MoonHub75 release over itself when an update comes out, with the wrong pins. With its own name, the panel updates when you rebuild it.

A pin map in `firmware/controllers/` and a line on the DIY page make a good first pull request. Say which panel you used and how long it ran. See [CONTRIBUTING.md](https://github.com/gameday-scoreboard/gameday-scoreboard/blob/dev/CONTRIBUTING.md#where-help-is-wanted).

## The scoreboard75 test controllers

The early Game Day Scoreboard test controllers use different pins, the scoreboard75 pinout. Their pin map is `firmware/controllers/scoreboard75.yaml`, their build is `firmware/gameday-scoreboard75.yaml`, and they have their own [scoreboard75 installer](https://install.gameday-scoreboard.com/install/scoreboard75/).

:::caution
On a MoonHub75-pinout board, use the [main installer](https://install.gameday-scoreboard.com/) or build `gameday.yaml`. Never flash `gameday-scoreboard75.yaml` or the scoreboard75 installer's firmware to it. The panel pins won't match, and that build takes its updates from the scoreboard75 feed.
:::
