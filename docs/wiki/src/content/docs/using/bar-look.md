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

Between games the panel opens its message card with your **opening card**: your logo at the top in your colors, your club's name under it and the name of your wall under that. When your bar has an event coming up, its page comes next. Then the panel walks your plate wall, one member at a time, and comes back to the opening card each time the list starts over. With no plate list, the opening card (and the event page) take the message card's turns on their own.

If the message card has a list address of its own, the panel shows that list, a few lines a page, instead of the plate wall.

### The plate wall

Each member gets a page of their own:

1. Your sprite (a small picture, like a mug or a spaceship) flies across the panel from left to right and uncovers the member's name in a big oval plate, with their plate number over it. With no sprite, the plate is uncovered from left to right.
2. After a few seconds the oval folds into a round plate on one panel, and the member's quote rises from the bottom of the other panel, a line at a time. A short quote stops in the middle and holds; a long one keeps rising off the top so every line gets read.
3. The next member's plate goes on the other side, so the plate and the quote trade places from one member to the next.

A member with no quote keeps the big oval for their whole page. Each page lasts as long as it takes to read: about 6 seconds with no quote, longer for a long quote.

On a single panel, the round plate fills the screen first, and then the quote rises on its own.

The name goes in as big as it fits inside the rim, on one line or split onto two at the last space. A very long name that doesn't fit even in small type is cut at the rim.

Plates use your colors: the rim in your `rim` color, the number in your `number` color, the quote in your `quote` color, all on your `background`. Names are white.

### Second-round plates

A member on their second plate (`2` in the list's round column) gets a double rim: an outer ring in your `quote` color and an inner ring in your `rim` color. In place of the number it says your round-two words, "ROUND TWO" unless your brand file names them something else.

### The plate party

Your plate list comes newest first. When a new plate shows up at the top of the list, the panel throws it a party once, at the message card's next turn: your sprite flies in and hovers with its lights blinking, your new-plate words ("NEW PLATE!") flash in your `rim` color with the name of your wall under them, fireworks go off behind in your colors, and then the new member's big oval comes up. It lasts about 12 seconds. A new second-round plate gets the round-two words instead.

The very first time the panel reads your list (and each time you change the brand file's address), it only notes which plate is on top, so the wall doesn't throw a party for a plate that's already up.

To throw the party again, for the newest plate, any time:

- press **4** on the panel's WizMote remote, or
- press **Plate party** under **Bar look** on the device page.

A party you ask for goes up right away, even over a game, and the game comes back after it. While the panel shows your plate wall, the remote's button 4 always throws the party, whatever it was set to do before.

### The event page

When your brand file has an event, the panel gives it a page after the opening card from two weeks before the day through the day itself: the event's name big in your `rim` color, when it is ("THURSDAY 6 PM", "TOMORROW 6 PM", "TONIGHT 6 PM", or the date when it's more than a week out), and your wall's name small in your `number` color. The day comes from the panel's own clock and time zone. The day after, the page stops on its own.

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
- **"The sprite has to be a PNG file"**, **"can be up to 64 by 32 pixels"**: the sprite file needs changing. The plate wall uncovers each plate without it until then.

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
  "sprite": "https://example.com/sprite.png",
  "list": "https://example.com/plates.txt",
  "event": {"name": "GLASS NIGHT", "date": "2026-11-12", "time": "18:00"},
  "party": {"new": "NEW PLATE!", "round2": "ROUND TWO!"}
}
```

- `name` shows on the device page only.
- `title` and `wall` are the two lines on the opening card, up to 24 letters each. The title shows in the `rim` color, the wall's name in the `number` color.
- `colors` are web colors like `#D4A017`. `background` fills the opening card.
- `logo` is a PNG of your logo in one color, up to 128 by 64 pixels and 32 KB. The panel draws it in your `rim` color: the solid part of a logo on a see-through background, or the light part of a light logo on black. `logo_color`, if you give it, is drawn just as it is instead.
- `sprite` is a small PNG in one color, up to 64 by 32 pixels, lit the same way as the logo and drawn in your `rim` color. It flies across the plate wall and hovers over the plate party and the event page.
- `list` is the address of your plate list, a plain text file with one plate a line, newest first: `number|name|quote|round`, like `112|Ann B.|Cheers to that|`. The quote and the round can be left empty; a `2` in the round marks a second-round plate. Anything after the round is ignored. A line holds up to about 200 letters.
- `event` gives the event page its name, its day (`YYYY-MM-DD`) and, if you like, its time (24-hour, `18:00`). With no day, there is no event page.
- `party` holds your words for the plate party: `new` for a new plate, `round2` for a second-round plate. The round-two words show on those plates too, without the "!".

Every part can be left out. The file only needs a `title` or a logo.
