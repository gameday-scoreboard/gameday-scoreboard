---
title: "Panels in bars"
description: "The bar build: how a panel in a bar or restaurant gets it, how it updates itself overnight, and how to tell us when something's wrong."
sidebar:
  order: 13.5
---

:::note[Coming in the next release]
Everything on this page arrives with the next release. Until then, bar panels run beta versions, and the device page calls the build **Bar beta**.
:::

Panels in bars and restaurants run the **bar build**. It's the same scoreboard firmware every panel runs, with the same releases on the same day, plus a few things for a panel nobody on staff should have to look after.

## Getting the bar build

Panels we set up for a bar come with the bar build on them. To move a panel you already have to the bar build, write to [support](/help/support/) with the panel's name (like gameday-2f6a70) and we'll take care of it.

If you build your own panel for a bar on a MoonHub75-pinout board, each release has the bar build next to the others: flash the `.factory.bin` inside `firmware-moonhub75-bar.zip` over USB, or build `firmware/gameday-bar.yaml` yourself (see [Build from source](/diy/build-from-source/)).

A panel stays on the build it has. A bar panel only ever installs bar releases, and a home panel never installs one.

## How a bar panel updates

There's nothing to turn on. A bar panel installs each new release by itself overnight:

- only between 3 and 5 AM in the panel's timezone
- never while a game the panel follows is live: it waits for the game to end
- one try a night; if that try fails, the panel keeps the firmware it had and tries again the next night

Your team, favorites and settings stay as they were. **Install** on the device page still installs a waiting release right away. The rest works as on any panel; see [Firmware updates](/using/updates/).

Leave **Follow beta** off. With it on, the panel installs test builds of the next release, which can have bugs.

## Telling it's a bar panel

On the device page's Device tab:

- **Bar build** says the panel installs bar updates on its own overnight. The **Install updates automatically** switch isn't there, since the bar build always does.
- **Diagnostics** shows the firmware version followed by **bar**.
- **Something wrong?** opens an email to us with the panel's name already in it. Say what you saw, and we'll take it from there.
