---
title: "Brightness, panel on and off, and the clock"
description: "Set the panel's brightness, turn it off and on, dim it or turn it off at night on a schedule, and switch between the scoreboard and the clock."
sidebar:
  order: 17
---

The Panel card on the device page holds the settings for the screen itself: **Panel on**, **Brightness**, **Ticker speed** and **Showing**. The Game Day app and Home Assistant have the same settings.

## Brightness

**Brightness** runs from 1 (dimmest) to 255 (brightest). A new panel starts at 128. The panel keeps your setting through restarts.

A WizMote remote's brightness buttons move the same setting, a tenth of full at a time, and **NIGHT** dims the panel right down to a night level, about 8% of full. See [WizMote remote](/using/remote/).

## Night schedule

The night schedule dims the panel, or turns its screen off, between two times you pick, so a panel in the living room isn't lighting up the house at 2 AM. It's off on a new panel. Set it in the Panel card on the device page:

1. Turn on **Night schedule**.
2. Set **Starts at** and **Ends at**. A new panel starts at 11 PM to 7 AM. The night can run past midnight.
3. Pick **Screen at night**. On keeps the panel lit at the **Night brightness** below it, 1 to 255 like **Brightness** (16 to start). Off turns the screen dark for those hours.

A live game for your team or one of your favorites keeps the panel at its normal brightness until the game ends, so a late game still shows. Then the night setting comes back until the end time.

While night mode is on, the device page says so under the board, like **Night mode until 7:00 AM**, so a dark panel doesn't look broken. The panel keeps following your games all night, and the board is up to date the moment it lights up again.

:::note[Coming in the next release]
You can always get past the schedule. During its hours, any of these turns night mode off for the rest of that night and brings the panel back to its normal brightness:

- **ON**, the brighter button, **NIGHT** or a numbered button on the [WizMote](/using/remote/)
- picking a team or a mode in the app or on the device page
- **Panel on** on the device page
- **Turn on for tonight** next to **Night mode until** on the device page

The device page then says **Night mode off until 11:00 PM**, the next start time. To go back to the schedule before then, press **Back to night mode** there, or **NIGHT** on the remote. The next night the schedule runs as usual. Home Assistant automations don't turn night mode off, so one that runs at night can't wake the panel by mistake.
:::

The schedule runs on the panel's own clock, set from the internet, in the timezone under Setup. Until the panel has the time after it starts, it stays at its normal brightness. The heat and power protection below still hold the night brightness down when they need to.

## Turning the panel off and on

**Panel on** turns the panel's lights off and on. While it's off, the panel keeps following the game, so the board is up to date the moment it comes back on.

You can also turn it off and on with:

- a quick press of the boot button on the controller
- **ON** and **OFF** on a WizMote
- **Power** in Home Assistant

A few things turn a dark panel back on by themselves, since they mean someone wants to look at it: picking a team or a mode in the app, on the device page or in Home Assistant, a numbered button or **NIGHT** on the remote, and the brighter button on the remote, which brings back the brightness the panel had. During the night schedule's hours the panel comes back at its night setting instead: at the night brightness, or with **Screen at night** off, still dark until the hours end or a game of yours goes live.

:::note[Coming in the next release]
During the night schedule's hours, the remote, a team or mode picked in the app or on the device page, and **Panel on** on the device page turn night mode off for the rest of the night, so the panel comes back at its normal brightness. Home Assistant still brings it back at its night setting. See [Night schedule](#night-schedule).
:::

The panel always comes back on after a restart or a power cut.

## Showing: the scoreboard or the clock

**Showing** picks the page the panel shows: the **Scoreboard**, or a **Clock** page. The scoreboard is the page that follows your games; the clock page is a plain clock for when you want one. Home Assistant has the same choice as **Select Page**.

### The off-season clock

Separate from the Clock page, the scoreboard shows a clock of its own when there's no game to follow, so the panel isn't stuck on "No upcoming game" for months between seasons:

- **In Favorite teams**, when none of your favorites has a game scheduled, the clock is in your first favorite's colors.
- **In Live games** with nothing live, while the board shows your first favorite, between seasons the clock is in that team's colors. Under the time is its next game once it's on the schedule, like "Next: at PHI Mar 26 7:05 PM", or the date until then. Football teams get it only when ESPN lists no game at all, so a bye week never shows it; other leagues get it once the next game is more than 10 days away.
- **In Live games** with no favorites, when there has been no game in the last week and none is coming, the clock is gray.

Favorite teams follows the same rule as Live games' first favorite: the clock comes up once every favorite (in the playlist, when one is playing) is between seasons, with no game listed or the next one more than 10 days away (football only with no game listed). A favorite whose game is about to start still takes the panel. It's in the colors of the favorite whose game comes first, with that game under the time, like "Next: at PHI Mar 26 7:05 PM". While the clock is up, the panel checks for a new date every 6 hours instead of every 15 minutes, so it uses less data.

On a single 64x64 panel the date goes under the time, and the next game goes in the ticker. See [Show: Favorite teams, Live games](/using/modes/).

## Heat and power protection

**Too warm.** If the controller's chip reads above 75 C, in a sunny window or a closed cabinet for example, the panel dims itself a step at a time: to 70%, then 45%, then 25% of your brightness, one step every two minutes while it stays hot, and straight to 25% at 85 C. A small thermometer pulses slowly in the top right corner, and every two minutes the bottom line says **Too warm, dimming** in orange for 15 seconds. Once the chip is back under 70 C, the panel steps back up every five minutes until it's at your brightness again. Keep it out of direct sun and give it some air behind it.

**Weak power supply.** If the panel restarts twice in a row because its power dipped, as on a phone charger or a supply too weak for it, it holds its brightness at about a quarter of full so it stays on. A small plug pulses slowly in the top right corner, and every two minutes the bottom line says **Check power supply** in red. Use the power supply that came with the panel, not a phone charger or a USB port, then press **Clear** in the notice on the device page.

The device page shows both notices under the board, and the chip temperature under Diagnostics.
