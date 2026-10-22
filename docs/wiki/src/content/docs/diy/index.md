---
title: "DIY: run it on your own board"
description: "Run the open-source Game Day Scoreboard firmware on an ESP32-S3 HUB75 board and panel you put together yourself."
sidebar:
  order: 1
---

The Game Day Scoreboard firmware is open source under the MIT license. If you have an ESP32-S3 HUB75 controller and a panel, you can run it for free, with every league and setting a Game Day Scoreboard has.

These pages are for makers who build their own. If you bought a Game Day Scoreboard, start at [Set up your scoreboard](/get-started/setup/) instead.

## What you need

- An ESP32-S3 board with 16 MB of flash and 8 MB of octal PSRAM, like a dev board with an N16R8 module, on the MoonHub75 pinout. A MoonHub75 board is already wired that way.
- A button from GPIO0 to ground. The BOOT button on most dev boards is already wired that way.
- One 64x64 HUB75 panel, two 64x64 panels side by side, or one 128x64 panel.
- A 5 V power supply rated for the panel.
- A 2.4 GHz Wi-Fi network. The panel doesn't join 5 GHz networks.
- A USB cable and a computer with Chrome or Edge for the first install.

## The path

1. Choose a board. [Boards](/diy/boards/) covers the MoonHub75, a bare ESP32-S3 wired to the same pins, and other boards.
2. Wire the panel and power it. [Panels and power](/diy/panels/) covers sizes, chaining two panels and the supply.
3. Install from the browser installer. [Install the firmware](/diy/install/) walks through it and explains how updates reach your board.
4. Set it up like any owner. Joining Wi-Fi, picking a team and the Game Day app work the same as on a Game Day Scoreboard. Follow [Set up your scoreboard](/get-started/setup/).

## More for builders

- [Build it yourself](/diy/build-from-source/) compiles the firmware from the YAML with ESPHome, for a board with its own pins or a change of your own.
- [What's different from a Game Day Scoreboard](/diy/whats-different/) lists what you take on yourself, and where to get help.
