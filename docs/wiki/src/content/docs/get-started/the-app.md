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

## Board, Teams and Settings

Both apps have three tabs: **Board**, **Teams** and **Settings**. They show the panel you picked last. The iPhone app runs on iPad too, with the same tabs.

A few things are on iPhone only for now: the night schedule, and the **Pick the best game** and **Favorites take over** switches. They're marked below. On Android, use the [device page](/using/device-page/) for them.

### Board

What you reach for during a game:

- The game on the panel, and what's up next.
- The mode, **Favorites** or **Live games**, with the one switch that goes with it: **Only today's games** for Favorites, the leagues for Live games. The device page calls the first one **Favorite teams**.
- **Message**, for a banner or a message card on the panel. See [Your own messages](/using/messages/).
- **Brightness**, **Screen** to turn the panel's screen off and on, and **Refresh now** to read the scores again right away.

The panel's name is at the top. Tap it to switch to another panel, to add one, or, with two or more, to manage them.

If the app loses its connection to the panel for a moment while the panel still answers, **Reconnecting…** shows at the top of the Board until it's back. If the panel doesn't answer at all, the Board says **Can't reach** and the panel's name, with what to check.

### Teams

- **Favorites**, in priority order. **Add a favorite** opens the team chooser. Each team can move up or down or come out of the list. See [Favorite teams](/using/favorites/).
- **Playing** and **Playlists**, for groups of your favorites. See [Playlists](/using/favorites/#playlists).
- **When favorites play**: how early a favorite's game takes the panel, how long it stays after the final, and what happens when two play at once.
- **Live games**: the leagues it follows and **Switch every**, and on iPhone **Pick the best game** and **Favorites take over**. Choosing leagues here doesn't change the panel's mode. See [Modes](/using/modes/).

### Settings

- **Display**: ticker speed, **Showing** (the scoreboard or the clock), the ticker and celebrations. It also has **Between games**, with the countdown to the next game and your own countdown, and on iPhone the night schedule. See [Brightness and the clock](/using/brightness-and-clock/), [The ticker](/using/ticker/) and [Countdowns](/using/countdowns/).
- **Device**: the panel's **Nickname**, **Setup**, firmware updates, **Reboot**, **Reset Wi-Fi**, the panel's saved Wi-Fi networks and the remote. **Setup** has the timezone, and on iPhone the walkthrough again. **Backup** saves and restores the panel's settings. On iPhone, **Advanced** has the panel layout, the remote and the panel's memory. On Android the panel layout is under **Setup** and the remote under **Remote**. See [Back up and restore settings](/using/backup/), [Wi-Fi](/using/wifi/) and [WizMote remote](/using/remote/).
- **Panels**: every panel saved in the app, and new ones nearby.
- **Team logos**, the app's version, links to help, **Diagnostics** and **Copy diagnostics**, which puts what support needs on the clipboard to paste into an email. It never includes passwords.

## Your panels

The panel list is **Settings > Panels**, and it's what the Board shows until you have a panel. It shows each panel's nickname, with a dot for online and **Offline** next to a panel the app can't reach.

- **Nickname**: give each panel a name like "Living room", in **Settings > Device > Nickname**. The nickname is saved on the panel, so every phone and the device page see the same name.
- **Order**: with two or more panels, put the list in your own order (**Edit** on iPhone, the handle on each row on Android). The order is kept on each phone.

## Try the app before your panel arrives

With no panel saved yet, the panel list has **Try a demo panel**. It opens a demo panel inside the app, with made-up teams playing a game on a loop, so you can look around every tab. Nothing you change there reaches a real panel, and the demo isn't saved to your list. To leave it, tap its name at the top of the Board and choose **Close the demo**.

## If the app doesn't list your panel

Add it by address. On the panel list, tap **+** and enter the panel's name, like `gameday-2f6a70.local`, or its IP address, like `192.168.1.50`. Both show on the panel when it starts. To see them again, hold the button on the controller for about 1.5 seconds.

The phone has to be on the same Wi-Fi network as the panel. A guest network can keep the two apart. More in [Troubleshooting](/help/troubleshooting/).

## The QR code on the panel

The QR code the panel shows during setup, when it has lost Wi-Fi and right after it joins, opens `https://app.gameday-scoreboard.com/setup`. On an iPhone, point the Camera at it. It opens the Game Day app if you have it installed, and the setup page in your browser if you don't.

## Without the app

You don't need the app to use the panel.

- **To put it on Wi-Fi**, join the panel's own hotspot from any phone or laptop. It's named **Game Day** plus six letters and numbers, like `Game Day 2f6a70`, and a setup page opens when you join it. See [Set up your panel](/get-started/setup/#without-the-app-use-the-panels-hotspot).
- **To pick your team and change settings**, open the panel's device page in any browser on the same Wi-Fi, at the `gameday-xxxxxx.local` name or IP address the panel shows. Everything the app sets is there. See [The device page](/using/device-page/).
