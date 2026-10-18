---
title: "Firmware updates"
description: "How the panel checks for new firmware, installing an update from the device page, and installing updates automatically overnight."
sidebar:
  order: 13
---

The panel updates its own firmware over Wi-Fi. There is nothing to plug in. What changed in each release is on the [What's new](/changelog/) page.

## How the panel checks

The panel checks for a new release every 6 hours, and once a few seconds after it first sets its clock when it starts. **Check for updates** in the Device card on the device page checks right away.

The **Firmware** box in the Device card says what it found:

| The box says | What it means |
| --- | --- |
| **You're up to date** and the version | The panel runs the newest release. |
| **Update available** and the version | A newer release is out. The box lists what's in it, with a **Release notes** link, and shows an **Install** button. |
| **You're ahead of the latest release** | The panel runs something newer than the published release, like a test build. Nothing to install. |
| **Not checked yet** | The panel hasn't heard from the update server since it started. Tap **Check for updates**. |

If the check can't reach the update server, the page says **Could not reach the update server. Try again in a minute.**

## Installing an update

1. Open the device page and tap **Install** in the Firmware box.
2. Confirm. The panel downloads the update and restarts.

While it installs, the box says **Installing** and the version, with **Keep the panel powered. It reboots when done and this page reconnects.** Leave the panel plugged in until it's back. The page reloads by itself once the panel answers on the new version.

If the power goes out or the download fails partway through, the panel keeps running the firmware it had. A new version only starts once it's fully downloaded and checked.

You can install an update at any time, during a game too. Your team, favorites and settings stay as they were.

## Install updates automatically

**Install updates automatically** in the Device card is off unless you turn it on. With it on, the panel installs a new release by itself overnight:

- only between 3 and 5 AM in the panel's timezone
- each panel at its own minute in the first hour, so a house with several panels doesn't download all at once
- never while a game the panel follows is live: it waits, and the second hour leaves room for a late game to finish
- one try a night; if that try fails, it tries again the next night

With the switch off, the panel still checks and the device page tells you when an update is out, but nothing installs until you tap **Install**. Home Assistant has the same switch; see [Home Assistant (optional)](/using/home-assistant/).

## An update that didn't take

If a new version crashes as it starts, the panel goes back to the firmware it had. It remembers that version and doesn't install it on its own again, so it never restarts into the same bad update every night. A later release still installs, and **Install** on the device page still tries the one that failed.

:::note[Coming in the next release]
The Firmware box says when this happened: **The update to** a version **didn't take and the panel went back to** the version it runs now, with **Install tries it again.** If the panel can't read the update server, it tries a second update server at once, so an outage at one doesn't hold up an update.
:::

## Beta versions

Beta versions are test builds of the next release, for test panels. They can have bugs. Panels you own run the published releases.
