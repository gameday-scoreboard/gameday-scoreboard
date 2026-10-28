---
title: "The Game Day app"
description: "What the Game Day app for iPhone and Android does, where its settings are, how the panel's QR code opens it, and how to manage without a phone app."
sidebar:
  order: 4
---

The Game Day app is the companion app for your panel, for iPhone and Android. It has no account and no cloud service. The app talks to the panel directly, over Bluetooth during setup and over your Wi-Fi after that.

## What the app does

- **Finds your panels.** A new panel shows up as a new panel nearby, over Bluetooth. A panel that's already on your Wi-Fi shows up under **Found on your network**.
- **Sets a new panel up over Bluetooth.** The panel sends the app the Wi-Fi networks it can see, strongest first. You pick yours and enter the password. See [Set up your panel](/get-started/setup/).
- **Picks your teams** and the mode, the same choices as the device page. See [Pick your team](/get-started/pick-your-team/).
- **Changes the panel's settings**: the ticker, celebrations, favorites, brightness, the remote and the rest. They are the same settings as on the device page, and a change in one shows in the other.

## On iPhone: Board, Teams and Settings

The iPhone app has three tabs along the bottom. They show the panel you picked last. The app runs on iPad too, with the same tabs.

### Board

What you reach for during a game:

- The game on the panel, and what's up next.
- The mode, **Favorites** or **Live games**, with the one switch that goes with it: **Only today's games** for Favorites, the leagues for Live games. The device page calls the first one **Favorite teams**.
- **Message**, for a banner or a message card on the panel. See [Your own messages](/using/messages/).
- **Brightness**, **Screen** to turn the panel's screen off and on, and **Refresh now** to read the scores again right away.

The panel's name is at the top. Tap it to switch to another panel, to add one, or, with two or more, to manage them.

If the app loses its connection to the panel for a moment while the panel still answers, **Reconnecting…** shows at the top of the Board until it's back. If the panel doesn't answer at all, the Board says **Can't reach** and the panel's name, with what to check.

### Teams

- **Favorites**, in priority order. **Add a favorite** opens the team chooser. Each row has arrows to move the team up or down, **Edit** lets you drag them, and a swipe takes one out. See [Favorite teams](/using/favorites/).
- **Playing** and **Playlists**, for groups of your favorites. See [Playlists](/using/favorites/#playlists).
- **When favorites play**: how early a favorite's game takes the panel, how long it stays after the final, and what happens when two play at once.
- **Live games**: the leagues it follows, **Switch every**, and the other Live games switches. Choosing leagues here doesn't change the panel's mode. See [Modes](/using/modes/).

### Settings

- **Display**: ticker speed, **Showing** (the scoreboard or the clock) and the night schedule, then the ticker, celebrations and **Between games**, which has the countdown to the next game and your own countdown. See [Brightness and the clock](/using/brightness-and-clock/), [The ticker](/using/ticker/) and [Countdowns](/using/countdowns/).
- **Device**: the panel's **Nickname**, **Setup** (the walkthrough again, the timezone, and showing how to connect when the panel starts), **Backup**, firmware updates, **Reboot**, **Reset Wi-Fi**, **Wi-Fi networks**, **Diagnostics**, and **Advanced** for the panel layout, the remote and the panel's memory. See [Back up and restore settings](/using/backup/) and [Wi-Fi](/using/wifi/).
- **Panels**: every panel saved in the app, and new ones nearby.
- **Team logos**, the app's version, links to help and **Copy diagnostics**, which puts what support needs on the clipboard to paste into an email. It never includes passwords.

## On Android

The Android app has the same favorites, Live games, ticker, celebrations, remote, Wi-Fi networks and device settings, laid out its own way.

These are on iPhone only for now: playlists, the countdowns, the night schedule, messages, and backing up and restoring settings. On Android, use the [device page](/using/device-page/) for them.

## Your panels

The panel list shows each panel's nickname, with a dot for online and **Offline** next to a panel the app can't reach. On iPhone the list is **Settings > Panels**, and it's what the Board shows until you have a panel.

- **Nickname**: give each panel a name like "Living room". On iPhone it's in **Settings > Device**; on Android it's at the top of the panel's **Panel** settings. The nickname is saved on the panel, so every phone and the device page see the same name.
- **Order**: with two or more panels, drag the list into your own order (**Edit** on iPhone, the handle on each row on Android). The order is kept on each phone.

## If the app doesn't list your panel

Add it by address. On the panel list, tap **+** and enter the panel's name, like `gameday-2f6a70.local`, or its IP address, like `192.168.1.50`. Both show on the panel when it starts. To see them again, hold the button on the controller for about 1.5 seconds.

The phone has to be on the same Wi-Fi network as the panel. A guest network can keep the two apart. More in [Troubleshooting](/help/troubleshooting/).

## The QR code on the panel

The QR code the panel shows during setup, when it has lost Wi-Fi and right after it joins, opens `https://app.gameday-scoreboard.com/setup`. On an iPhone, point the Camera at it. It opens the Game Day app if you have it installed, and the setup page in your browser if you don't.

## Without the app

You don't need the app to use the panel.

- **To put it on Wi-Fi**, join the panel's own hotspot from any phone or laptop. It's named **Game Day** plus six letters and numbers, like `Game Day 2f6a70`, and a setup page opens when you join it. See [Set up your panel](/get-started/setup/#without-the-app-use-the-panels-hotspot).
- **To pick your team and change settings**, open the panel's device page in any browser on the same Wi-Fi, at the `gameday-xxxxxx.local` name or IP address the panel shows. Everything the app sets is there. See [The device page](/using/device-page/).
