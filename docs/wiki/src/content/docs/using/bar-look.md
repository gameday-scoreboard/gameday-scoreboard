---
title: "Your bar's look"
description: "On a bar's panel, one brand file gives the panel your logo, colors and plate list, and changes the look with no reflash."
sidebar:
  order: 12.2
---

:::note[Coming in the next release]
Brand files are new in the bar build. Release firmware for homes doesn't read them.
:::

A panel in a bar can wear the bar's own look. Everything about it comes from one small brand file at a web address: your logo, your colors, the name of your club and wall, and the address of your plate list. We set the file up with you, and you put its address into the panel once. When the file changes, the panel picks up the change on its own, with no reflash.

## What it does

Between games the panel opens its message card with your **opening card**: your logo at the top in your colors, your club's name under it and the name of your wall under that. Then it shows your plate list, a page at a time, and comes back to the opening card each time the list starts over. With no plate list, the opening card takes the message card's turns on its own.

Plate list lines read `number|name|quote|round`, like `112|Ann B.|Cheers to that|1`. For now the panel shows the number and the name, "#112 Ann B.".

If the message card has a list address of its own, the panel shows that list instead of the one in the brand file.

## Set the brand file

1. Open the panel's device page (see [The device page](/using/device-page/)).
2. Find **Bar look** and paste the address we gave you into **Brand file address**.
3. Press **Save**.

The line under the address says what the panel found: "Loaded:" and your bar's name, and a small copy of your logo. The opening card comes up at the message card's next turn between games.

To go back to the plain look, press **Clear**.

## When something's wrong

The panel reads the file again every half hour. If a read fails, or the file has a mistake in it, the panel keeps showing the last look it read, saved in its memory, and says why on the device page:

- **"The panel couldn't reach that address"** or **"answered with an error"**: the address is wrong or the site is down. The panel tries again every few minutes.
- **"The brand file isn't JSON"**: the file has a typing mistake in it. Send it to us and we'll fix it.
- **"Left out: ..."**: one part of the file (a color, the logo's address) couldn't be used. That part shows plain, white on black, and the rest of the look stays.
- **"The logo has to be a PNG file"**, **"can be up to 128 by 64 pixels"**: the logo file needs changing. The opening card shows your names without it until then.

When you change the address to another brand file, the old look goes away right away and the new file is read at once.

## What's in a brand file

For the curious, or if you keep the file yourself. It's a small text file, up to 2 KB, like this:

```json
{
  "name": "Main Street Taproom",
  "title": "MUG CLUB",
  "wall": "WALL OF FAME",
  "colors": {"rim": "#D4A017", "number": "#C8102E", "quote": "#4FA3D9", "background": "#000000"},
  "logo": "https://example.com/logo.png",
  "logo_color": "https://example.com/logo-color.png",
  "list": "https://example.com/plates.txt",
  "event": {"name": "GLASS NIGHT", "date": "2026-11-12", "time": "18:00"},
  "party": {"new": "NEW PLATE!", "round2": "ROUND TWO!"}
}
```

- `name` shows on the device page only.
- `title` and `wall` are the two lines on the opening card, up to 24 letters each. The title shows in the `rim` color, the wall's name in the `number` color.
- `colors` are web colors like `#D4A017`. `background` fills the opening card.
- `logo` is a PNG of your logo in one color, up to 128 by 64 pixels and 32 KB. The panel draws it in your `rim` color: the solid part of a logo on a see-through background, or the light part of a light logo on black. `logo_color`, if you give it, is drawn just as it is instead.
- `list` is the address of your plate list, a plain text file with one plate a line.
- `event` and `party` are read and kept for what's coming next on the bar build: a countdown to your event, and the words for a new plate.

Every part can be left out. The file only needs a `title` or a logo.
