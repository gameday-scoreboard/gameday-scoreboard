---
title: "Install the firmware"
description: "Flash the Game Day Scoreboard firmware onto a MoonHub75-pinout board from your browser, and how updates reach it."
sidebar:
  order: 4
---

The [installer](https://install.gameday-scoreboard.com/) flashes the prebuilt firmware for the MoonHub75 pinout from your browser. It's the same installer the Game Day Scoreboard controller uses. You don't need ESPHome or any other software.

:::caution
Installing erases whatever firmware was on the board. The installer is for boards on the MoonHub75 pinout only. A board with other pins needs its own build; see [Boards](/diy/boards/).
:::

## Before you start

- A desktop computer with Chrome or Edge. The installer talks to the board over USB, and the page tells you if your browser can't.
- A USB cable from the computer to the board.
- Your panel wired to the board. [Panels and power](/diy/panels/) covers it.

## Install

1. Open the [installer](https://install.gameday-scoreboard.com/) in Chrome or Edge.
2. Click the picture that matches your panels: **One 64x64 panel**, or **One 128x64 panel** (or two 64x64 side by side). This sets the layout the scoreboard starts with. The Install button appears once you pick.
3. Plug the board into the computer over USB.
4. Click **Install Game Day Scoreboard** and pick the board's port when the browser asks.
5. When the install finishes, click **Next**. The board restarts, and within about half a minute the same window asks for your Wi-Fi.

The two pictures install two builds of the same firmware. The 64x64 choice installs the build from `firmware/gameday.yaml`. The 128x64 choice installs the build from `firmware/gameday-2panel.yaml`, which is the same firmware starting two panels wide. You can change the layout later on the device page either way.

## If you skip the Wi-Fi dialog

If you close the dialog, the panel shows a QR code for the Game Day app and starts a hotspot named Game Day plus six letters and numbers, like Game Day 2f6a70. Set it up from the app, or join the hotspot and a setup page opens. The panel only joins 2.4 GHz networks.

## Set it up

From here a board you built works like any Game Day Scoreboard. Follow [Set up your scoreboard](/get-started/setup/) to finish joining Wi-Fi, then [Pick your team](/get-started/pick-your-team/).

## How updates work

A DIY board gets every release, built for its pinout, from the same release feed as a Game Day Scoreboard. It checks for a new release every 6 hours. When one is out, an Install button appears on the device page and the update goes over Wi-Fi. Turn on **Install updates automatically** and the panel installs a new release on its own between 3 and 5 AM, never during a game it is showing. It's off unless you turn it on. See [Updates](/using/updates/).

Each build has a variant name, and the variant names the folder the panel reads updates from:

| Variant | Build | Used for | Updates from |
| --- | --- | --- | --- |
| `moonhub75` | `firmware/gameday.yaml` | Installer, 64x64 choice | `moonhub75` |
| `moonhub75-2panel` | `firmware/gameday-2panel.yaml` | Installer, 128x64 choice | `moonhub75` |
| `scoreboard75` | `firmware/gameday-scoreboard75.yaml` | Test controllers on the scoreboard75 pinout only | `scoreboard75` |

The 128x64 build reads its updates from the `moonhub75` folder too. The panel saves its layout the first time it starts, so that update keeps it two panels wide.

Each release on [GitHub](https://github.com/gameday-scoreboard/gameday-scoreboard/releases) also carries the MoonHub75 factory image, `gameday-scoreboard-<version>.factory.bin`, and a zip of each variant's folder.

:::note[Coming in the next release]
When the release feed can't be read, the panel tries the same files on a second host, updates.gameday-scoreboard.com. A **Follow beta** switch on the device page gets test versions of the next release before everyone else. They can have bugs. Turned off, the panel stays on its beta until the next release.
:::
