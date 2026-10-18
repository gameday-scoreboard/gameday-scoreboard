---
title: "Get support"
description: "How to reach Game Day Scoreboard support by email, what to include, and where DIY builders and bug reports go."
sidebar:
  order: 5
---

Try [Troubleshooting](/help/troubleshooting/) and [What the panel is telling you](/help/panel-messages/) first. Most problems have a fix there.

## Email support

For a Game Day Scoreboard you bought, email [support@gameday-scoreboard.com](mailto:support@gameday-scoreboard.com). We reply within one business day.

Include what you can of this:

- **What happened.** What the panel or the app was doing, and what it said on the screen.
- **The firmware version.** It's in the Device card on the device page, next to **Firmware**.
- **The controller.** The Game Day Scoreboard controller, or the board you built it on.
- **The panel layout.** Which picture is picked under **Matrix** in the Setup card: **One 128x64 panel** or **One 64x64 panel**.

A photo of the panel showing the problem helps too.

## Built your own, or found a bug?

Email support is for Game Day Scoreboards we sold. For a panel you built on a MoonHub75 board or your own ESP32-S3 board, open an issue on [GitHub](https://github.com/gameday-scoreboard/gameday-scoreboard/issues). Bug reports from any panel are welcome there.

Include the same details as above: what happened, the firmware version, the board and the panel layout.

:::note[Coming in the next release]
If the panel restarted by itself, it keeps a crash report until it's cleared, even across an unplug. Open `http://<your panel's address>/gameday/crash` in a browser, like `http://gameday-2f6a70.local/gameday/crash`, and paste what it shows into your email or issue. A `count` of 0 means it hasn't restarted unexpectedly.

Built from source? `python3 scripts/crash_report.py <panel address>` in the firmware repo prints the same report and turns its addresses into file and line numbers, using the matching release's firmware or `--elf` for your own build.
:::

## Orders and returns

For orders, shipping and returns, go to the store at [gameday-scoreboard.com](https://gameday-scoreboard.com).
