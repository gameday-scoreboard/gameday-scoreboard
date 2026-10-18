---
title: "Brightness, panel on and off, and the clock"
description: "Set the panel's brightness, turn it off and on, dim it for the night, and switch between the scoreboard and the clock."
sidebar:
  order: 17
---

The Panel card on the device page holds the settings for the screen itself: **Panel on**, **Brightness**, **Ticker speed** and **Showing**. The Game Day app and Home Assistant have the same settings.

## Brightness

**Brightness** runs from 1 (dimmest) to 255 (brightest). A new panel starts at 128. The panel keeps your setting through restarts.

A WizMote remote's brightness buttons move the same setting, a tenth of full at a time, and **NIGHT** dims the panel right down to a night level, about 8% of full. See [WizMote remote](/using/remote/).

## Turning the panel off and on

**Panel on** turns the panel's lights off and on. While it's off, the panel keeps following the game, so the board is up to date the moment it comes back on.

You can also turn it off and on with:

- a quick press of the boot button on the controller
- **ON** and **OFF** on a WizMote
- **Power** in Home Assistant

A few things turn a dark panel back on by themselves, since they mean someone wants to look at it: picking a team or a mode in the app, on the device page or in Home Assistant, a numbered button or **NIGHT** on the remote, and the brighter button on the remote, which brings back the brightness the panel had.

The panel always comes back on after a restart or a power cut.

## Showing: the scoreboard or the clock

**Showing** picks the page the panel shows: the **Scoreboard**, or a **Clock** page. The scoreboard is the page that follows your games; the clock page is a plain clock for when you want one. Home Assistant has the same choice as **Select Page**.

### The off-season clock

Separate from the Clock page, the scoreboard shows a clock of its own when there's no game to follow, so the panel isn't stuck on "No upcoming game" for months between seasons:

- **In My team**, between seasons, the clock is in your team's colors. Under the time is your team's next game once it's on the schedule, like "Next: at PHI Mar 26 7:05 PM", or the date until then. Football teams get it only when ESPN lists no game at all, so a bye week never shows it; other leagues get it once the next game is more than 10 days away.
- **In Favorite teams**, when none of your favorites has a game scheduled, the clock is in your first favorite's colors.
- **In Live games** with no team picked, when there has been no game in the last week and none is coming, the clock is gray.

On a single 64x64 panel the date goes under the time, and the next game goes in the ticker. See [Show: My team, Favorite teams, Live games](/using/modes/#when-your-team-has-no-game-coming).

## Heat and power protection

:::note[Coming in the next release]
**Too warm.** If the controller's chip reads above 75 C, in a sunny window or a closed cabinet for example, the panel dims itself a step at a time: to 70%, then 45%, then 25% of your brightness, one step every two minutes while it stays hot, and straight to 25% at 85 C. A small thermometer pulses slowly in the top right corner, and every two minutes the bottom line says **Too warm, dimming** in orange for 15 seconds. Once the chip is back under 70 C, the panel steps back up every five minutes until it's at your brightness again. Keep it out of direct sun and give it some air behind it.

**Weak power supply.** If the panel restarts twice in a row because its power dipped, as on a phone charger or a supply too weak for it, it holds its brightness at about a quarter of full so it stays on. A small plug pulses slowly in the top right corner, and every two minutes the bottom line says **Check power supply** in red. Use the power supply that came with the panel, not a phone charger or a USB port, then press **Clear** in the notice on the device page.

The device page shows both notices under the board, and the chip temperature under Diagnostics.
:::
