---
title: "What's different from a Game Day Scoreboard"
description: "What you take on yourself when you run the Game Day Scoreboard firmware on your own board, and where to get help."
sidebar:
  order: 6
---

The firmware is the same, with every league and setting. The prebuilt firmware for the MoonHub75 pinout gets every release. The differences are in the hardware, the install and the support.

## You supply the hardware

You supply the board, the panels, the case, the stand and the power supply. Size the supply for your panels (see [Panels and power](/diy/panels/#power)).

## The same installer

A board on the MoonHub75 pinout installs from the same [installer](https://install.gameday-scoreboard.com/) as a Game Day Scoreboard, which asks for your layout before you install. See [Install the firmware](/diy/install/).

## Layout default

The Game Day Scoreboard ships with one 128x64 panel and starts two panels wide. The one-panel DIY build starts as one 64x64 panel, and that's also where Factory Reset returns it. If you installed the 128x64 choice and then reset, pick the 128x64 layout again on the device page.

## The boot button

On a Game Day Scoreboard the button is on the controller. On your board it's whatever you wired to GPIO0, usually the BOOT button. It does the same things: a click turns the panel on or off, a 1.5 second hold shows the panel's address and IP address, and a longer hold resets Wi-Fi. See [Buttons and resets](/using/buttons-and-resets/).

:::note[Coming in the next release]
Nothing resets until you let go of the button. Hold it and the screen counts down from 5 seconds. Let go after 10 seconds to reset Wi-Fi, or after 20 seconds for a factory reset. At 30 seconds it says "Release to cancel".
:::

## No factory check

Every Game Day Scoreboard runs a test screen at the factory before it ships. A board you built skips that, so check it yourself. Once your panel is on Wi-Fi, start the same test screen from a computer on your network:

```sh
curl -X POST "http://gameday-xxxxxx.local/gameday/action?do=qc"
```

Put your panel's name in place of `gameday-xxxxxx`. The panel lights red, green, blue and white for 2 seconds each, walks a white line down all 64 rows, then shows the firmware version, the PSRAM size, the Wi-Fi signal and the panel size. It runs round and round. Press the button once to pass the button test and finish, or it stops by itself after 2 minutes.

## Support is on GitHub

Email support is for Game Day Scoreboards that were sold. For a panel you built, [open an issue on GitHub](https://github.com/gameday-scoreboard/gameday-scoreboard/issues). Bug reports from DIY builds are welcome. The firmware version (the Firmware row on the device page), your board and your panel layout save a round of questions, and a photo of the panel helps more than anything. Issues are public, so leave out Wi-Fi passwords.

## Rather buy one?

The Game Day Scoreboard comes with the Game Day Scoreboard controller, a 128x64 panel and everything set up. It's at [gameday-scoreboard.com](https://gameday-scoreboard.com).
