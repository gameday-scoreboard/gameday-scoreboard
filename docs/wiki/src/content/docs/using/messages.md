---
title: "Your own messages"
description: "Put your own words on the panel: a banner over the board for a few seconds, or a message card that takes turns with the scoreboard between games."
sidebar:
  order: 12
---

:::note[Coming in the next release]
Everything on this page arrives with the next release.
:::

The panel can show your words as well as scores, in two ways:

- A **banner** goes over whatever is on the panel for a few seconds, then the board comes back. "Happy birthday, Sam!" or "Pizza's here!"
- A **message card** stays set. Between games it takes turns with the scoreboard, and it steps aside the moment a game you follow goes live. Tonight's specials, a list of names, a welcome.

Nothing shows until you set a message, so a panel you never set one on looks the same as before.

The panel draws letters, numbers, punctuation and accented letters like é and ñ. Emoji are left out. Text takes the biggest size that fits the screen, and a page too long for it rolls up slowly.

## A banner

On the device page, the **Message** card has a **Banner** box. Type your words, set **Seconds on screen** (10 unless you change it, up to 600), and press **Show**. **Take down** clears it early.

A banner covers everything, the clock page included, but it doesn't turn the panel on: if the panel is off, nobody sees it. It is never saved, so a restart clears it.

From Home Assistant, set the panel's **Message** text entity, which shows the banner for 10 seconds, or call the **show_message** action with the text and the seconds. See [Home Assistant](/using/home-assistant/#messages).

Anything else on your network can show one too, with a web request to the panel:

```
curl -X POST "http://gameday-2f6a70.local/gameday/action?do=say&text=Pizza%27s%20here&secs=15"
```

Use your panel's own name or address. Leaving `text` empty takes the banner down.

## A message card

In the **Message card** part of the same card, type your message and press **Save**. Each line is its own entry, so a list of specials can go one per line. The card holds about 200 characters (an accented letter counts as two).

**Show it** sets how often the card takes the board between games:

| Show it | What happens |
| --- | --- |
| **Every 2 minutes** (and the other times) | The card takes the board for about 10 seconds, then the scoreboard comes back until the next turn. Starts at every 2 minutes. |
| **Whenever no game is on** | The card holds the board between games, moving to the next page every 10 seconds. Good for a panel on a wall of names. |

Each turn shows up to four entries. With more than that, the next turn picks up where the last one stopped, and after the last entry it starts again at the top.

The card never covers a live game or a score celebration. In Favorite teams and Live games it takes turns with whatever the board shows between games, the finals and next games included.

**Clear** empties the text and the address, and the card stops.

### A list from a web address

For a long list, like every member of a club, put a plain text file somewhere the panel can read it and give the card its address under **Or a list from a web address**. The file has one entry per line: names, specials, anything. Blank lines are skipped, and a line longer than 64 characters is cut.

The address starts with `http://` or `https://`. A file on a computer at home works, and so does a file on the web, like a "raw" link to a GitHub Gist or a shared text file.

The panel never holds the whole list. It reads 100 entries at a time and fetches the next 100 when it gets to them, so the list can be as long as you like, up to about 128 KB of text (several thousand names). It reads the file again from the top every so often, so a name you add shows up the same day.

When there is an address, the card shows the list, not the text box. Under the address, the device page says how the last read went: how many entries the list has, or that the address couldn't be reached and the panel will try again in a few minutes.

The panel reads the file from your network like any other device, with no password. Don't point it at anything private.
