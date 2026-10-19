---
title: "Build it yourself"
description: "Compile the Game Day Scoreboard firmware from its YAML with ESPHome, flash it over USB, and run the host tests."
sidebar:
  order: 5
---

Build from the YAML when your board has its own pins, when you want to change something, or when you want your own keys on the panel. Everything you need is in the [firmware repo](https://github.com/gameday-scoreboard/gameday-scoreboard).

## What's in the repo

| Path | What it holds |
| --- | --- |
| `firmware/gameday.yaml` | The release build for the MoonHub75 pinout. It names the build and picks the pin map. |
| `firmware/gameday-2panel.yaml` | `gameday.yaml` starting two panels wide. The installer offers it for a 128x64 display. |
| `firmware/gameday-scoreboard75.yaml` | The release build for the Game Day Scoreboard controller. Not for DIY boards. |
| `firmware/controllers/` | One pin map per board: `moonhub75.yaml` and `scoreboard75.yaml`. |
| `firmware/gameday-common.yaml` | Everything else: version, update feed, Wi-Fi, the panel layout, the pages and settings. Don't build it directly. |
| `firmware/pages/` | The pages drawn on the panel. The scoreboard is `gameday-live.yaml`. |
| `firmware/web/` | The device page. |
| `components/` | The firmware's own ESPHome components. |
| `tests/` | Host tests for the parser and game logic, and browser tests for the device page. |

The components in `components/` are:

- `gameday`: ESPN fetching, parsing and game logic, in C++.
- `panel_layout`: the one-panel or two-panel layout, applied at boot from a saved choice, so one binary serves both layouts.
- `improv_ble`: Bluetooth Wi-Fi setup for the Game Day app.
- `saved_networks`: the up to five Wi-Fi networks the panel remembers.

`gameday-common.yaml` also pulls in [lvgl-page-manager](https://github.com/pavlov-net/lvgl-page-manager) and [lvgl-canvas-fx](https://github.com/pavlov-net/lvgl-canvas-fx) at fixed versions, and the theme and clock packages from [hub75-studio](https://github.com/pavlov-net/hub75-studio), pinned to a commit so a build always gets the same thing.

## Install ESPHome

Use the ESPHome version CI builds with. It's `ESPHOME_VERSION` at the top of `.github/workflows/build.yml`, 2026.10.0b3 on the `dev` branch. Install it in a Python virtual environment.

```sh
python -m venv esphome-venv
```

On macOS or Linux:

```sh
source esphome-venv/bin/activate
```

On Windows:

```powershell
esphome-venv\Scripts\activate
```

Then install the pinned version:

```sh
pip install esphome==2026.10.0b3
```

The `dev` and `beta` branches may build on an ESPHome beta. `main`, which every release and the update feed come from, builds only on a final ESPHome release.

:::note[Coming in the next release]
CI checks the rule: a pull request into `main` or a release tag fails when the pin isn't a final release like 2026.10.0.
:::

## Get the code

```sh
git clone https://github.com/gameday-scoreboard/gameday-scoreboard
```

```sh
cd gameday-scoreboard
```

## Compile

```sh
esphome compile firmware/gameday.yaml
```

Use `compile` to check a change, not `config`. `config` doesn't catch C++ errors. For a 128x64 display, compile `firmware/gameday-2panel.yaml` instead. It has its own build folder, so switching between the two keeps both caches.

## Flash over USB

Plug the board in and run:

```sh
esphome run firmware/gameday.yaml
```

ESPHome builds the firmware and asks which port to flash. Pick the USB port. Joining Wi-Fi and picking a team then work the same as after the [browser installer](/diy/install/).

:::caution
A build that keeps the variant `moonhub75` reads the MoonHub75 release feed. When the next release comes out, the device page offers it, and installing it replaces your build. For a board with its own pins, give your copy its own `variant` name. See [Other boards](/diy/boards/#other-boards).
:::

## Your own keys

The prebuilt firmware has no password on updates, no key on the ESPHome API, and an open device page, because the panel trusts your home network. If you want a key or a password on your own panel, add it to your copy of the YAML and build that. Pull requests that add them to the default build aren't merged.

## Host tests

The parser and game logic have host tests. They need g++.

```sh
make -C tests
```

The device page has browser tests against a mock panel, in `tests/web`. They need Node and Python 3. Run these once:

```sh
npm ci
```

```sh
npx playwright install chromium
```

Then run the tests from `tests/web`:

```sh
npm test
```

## Edit the device page

The device page is plain HTML and JavaScript in `firmware/web`. The firmware embeds a generated bundle, so after you edit `app.js` or `app.css`, rebuild it:

```sh
python scripts/build_web.py
```

## Contributing

Bug reports, test fixtures from odd games, pin maps for new boards and device page fixes are all welcome. [CONTRIBUTING.md](https://github.com/gameday-scoreboard/gameday-scoreboard/blob/dev/CONTRIBUTING.md) says where help is wanted, how to branch and what CI checks.
