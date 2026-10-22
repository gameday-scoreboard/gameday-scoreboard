---
title: "Back up and restore settings"
description: "Save your panel's settings to a file and load them back after a factory reset, or onto a second panel."
sidebar:
  order: 18
---

Your teams, favorites, ticker switches and the rest take a while to set up. Save them to a file once, and after a factory reset, a replacement panel or a second panel for another room, load the file and the panel picks up where it was.

## Save a backup

1. Open the [device page](/using/device-page/).
2. In the **Setup** card, under **Backup**, tap **Export settings**.

Your browser saves a small file named after the panel and the day, like `gameday-2f6a70-settings-2026-10-21.json`. Keep it somewhere you'll find it, like your phone's Files app or a cloud drive.

## Restore a backup

1. Open the device page of the panel you want to set up.
2. In the **Setup** card, under **Backup**, tap **Import settings** and pick the file.
3. The page asks before it changes anything. Tap **OK**.

The page says **Restored** when the panel has the settings. They're saved on the panel at once, so you can unplug it and carry it to its room straight away.

A file from another panel works the same way; the page tells you which panel it came from before you say yes. The panel keeps its own nickname and panel layout, so a second panel doesn't take the first one's name, and a 64x64 panel's backup doesn't turn a 128x64 panel into half a board. The page says which of the two it kept, like **This panel kept its own nickname**.

A backup this panel saved itself, restored after a factory reset, brings back its nickname and layout too. If the layout is different from the one it has now, the panel restarts with it and the page reconnects when it's back.

:::note[Coming in the next release]
Keeping the nickname and layout on a backup from another panel, and saving a restore on the panel at once.
:::

## What's in the file

- The panel's **Nickname**, restored only onto the same panel
- Your team and what the panel shows: **My team**, **Favorite teams** or **Live games**
- Your favorites in their order, with the WizMote's buttons 1 to 4 on the same teams
- **Only today's games**, **Jump to the action**, **Score banner** and **Favorites take over**
- The Live games leagues, **Switch every** and **Pick the best game**
- **Take over**, **After the final** and **Two games at once**
- Every ticker switch and **Opponent scores too**
- The timezone, and whether it follows your browser
- The night schedule: its times, the screen and the night brightness
- The message card's text, list address and timing
- The panel layout (restored only onto the same panel), **Brightness** and **Ticker speed**
- **Show how to connect when the panel starts**, **Install updates automatically** and **Follow beta**

:::note[Coming in the next release]
The file also has your playlists, with their teams and their order, and the one Favorite teams is playing. See [Playlists](/using/favorites/#playlists).
:::

## What isn't

- **Wi-Fi networks.** Passwords never leave the panel. A new or reset panel joins Wi-Fi through the Game Day app or its own setup hotspot first, then you restore. See [Wi-Fi](/using/wifi/).
- **The WizMote pairing.** Pair the remote again. See [WizMote remote](/using/remote/).

## When it won't restore

The panel checks the whole file before it changes anything. If a setting in it has the wrong kind of value, like text where a number goes, or a team or time the panel can't read, nothing changes and the page says which one, like **Not restored: mode should be a whole number. Nothing was changed.** That happens to a file that was edited by hand or damaged; restore an untouched copy instead.

**That file isn't a settings backup** means the file isn't one the panel saved.

**This backup is from newer firmware** means the file came from a panel on a release that saves backups differently. [Update the panel](/using/updates/), then restore it.

## When something is left out

A backup from a panel on a later release usually restores fine. If it has something this panel's firmware doesn't have yet, like a team, a league, a timezone, a kind of message list address or a setting, or a number past what this panel takes, the page lists what it left out, like **This panel's firmware doesn't have: favs=epl:9999** or **mode=6**, and restores everything else. A favorite it leaves out leaves its slot empty, so the teams after it keep their remote buttons.

## For scripts and Home Assistant

The backup is a plain web address on the panel, so a script can save and restore it too:

```
curl -o backup.json http://gameday-2f6a70.local/gameday/settings
curl -X POST -H "Content-Type: application/json" --data-binary @backup.json http://gameday-2f6a70.local/gameday/settings
```

The restore answers `{"ok":true,"restart":false,"skipped":[],"kept":[]}`, or an `error` with the reason and nothing changed. `kept` lists `nick` and `panels` when the file came from another panel and this one kept its own. Send it as `application/json`, as above.
