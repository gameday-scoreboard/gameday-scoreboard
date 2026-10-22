---
title: "Install or reinstall the firmware"
description: "Reinstall the Game Day Scoreboard firmware from a desktop browser over USB, and when you would need to."
sidebar:
  order: 5
---

Your panel comes with the firmware installed, and it updates itself over Wi-Fi. You don't need this page to set up a panel. See [Updates](/using/updates/) for the normal way new versions arrive.

## When you need it

- **The panel won't start.** It stays dark or keeps restarting.
- **You want to start over** with a clean install.

## What a USB install erases

A normal update over Wi-Fi keeps your team, favorites, Wi-Fi and every other setting.

A USB install from the browser installer is different. It erases whatever was on the controller and starts fresh, so you set the panel up again afterwards: Wi-Fi, team and settings.

## Install from your browser

You need a desktop or laptop computer with Chrome or Edge, and a USB cable.

1. Open the installer at [install.gameday-scoreboard.com](https://install.gameday-scoreboard.com/).
2. Click the picture that matches your panels: **One 64x64 panel**, or **One 128x64 panel** (or two 64x64 side by side). The Install button appears once you pick.
3. Plug the controller into the computer with the USB cable.
4. Click **Install Game Day Scoreboard** and pick the controller's port when the browser asks.
5. When the install finishes, click **Next**. The controller restarts, and within about half a minute the same window asks for your Wi-Fi. Pick your network and enter the password.

If you skip the Wi-Fi step, the panel shows **SETUP** with the QR code for the app and starts its **Game Day** hotspot. Carry on from [Set up your panel](/get-started/setup/).

If the installer says the browser can't talk to the controller over USB, switch to Chrome or Edge on a desktop computer.

## Built your own?

The same installer works on a MoonHub75 board, or your own ESP32-S3 board wired to the MoonHub75 pinout. See [Install on your own board](/diy/install/).
