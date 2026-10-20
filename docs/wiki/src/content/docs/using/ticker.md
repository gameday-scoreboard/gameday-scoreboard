---
title: "The ticker"
description: "The scrolling line along the bottom of the panel: what it shows before, during and after a game, the switches that control it, and its speed."
sidebar:
  order: 10
---

The ticker is the line that scrolls along the bottom of the panel. It carries what doesn't fit on the board: the kickoff time and odds before a game, the situation and the last play during it, and the result after.

## What it shows

| When | What scrolls |
| --- | --- |
| Before the game | The kickoff day and time in the panel's timezone, the odds and TV channel, the venue, and each sport's extras: a bowl or playoff name in football, the probable pitchers in baseball, the playoff series in basketball and hockey, form and the cup stage in soccer. |
| During the game | The game situation and the last play, each with its own switch. |
| At a break | Halftime in football, the hits and errors between half innings in baseball, the series or records in basketball and hockey, and the goals, cards and stats at half time in soccer. |
| After the final | "Final" with the records, or the sport's result line, like hits and errors in baseball or the goals in soccer. |

:::note[Coming in the next release]
At halftime and the final, with **Last play** on, the ticker also runs the scoring plays the panel saw in football, baseball and hockey, like "Q1 7:12 Lamb 12 yd pass, DAL 7-0", and at the final ESPN's headline for the game when it has one. The board takes turns with the line score at the same time, whatever the switches say.
:::

Each sport's page has the details: [Football](/using/football/), [Baseball](/using/baseball/), [Basketball](/using/basketball/), [Hockey](/using/hockey/) and [Soccer](/using/soccer/).

:::note[Coming in the next release]
Accented letters in names, like the é in Atlético or the ñ in Peña, show in the ticker and the panel's other small text. Before, they came out as a gap, like "Atl tico".
:::

## The switches

The Ticker card on the device page has a switch for each part. All of them start on.

| Switch | What it adds |
| --- | --- |
| **Down and distance** | Football: the down, yards to go and where the ball is, like "3rd & 4 at FIU 31". |
| **Count and at-bat** | Baseball: the count, outs, pitcher and batter. |
| **Match stats** | Soccer: goals, cards, possession and shots. |
| **Last play** | ESPN's latest play in every sport. In soccer, the latest goal or card. A very long play is cut short. |
| **Pre-game odds and TV** | The line, over/under and TV channel before the start, in every league, when ESPN has them. |

:::note[Coming in the next release]
In football, **Last play** also adds how the drive went after a touchdown or field goal, like "Drive: 4 plays, 58 yards, 1:36", once, with the scoring play. **Pre-game odds and TV** also adds ESPN's win chance during the game, like "KC 72% to win", for the side it favors, when ESPN has one.
:::

These three, Down and distance, Count and at-bat and Match stats, are the game situation for their sport. Hockey and basketball have no situation line, so they have no switch of their own. The card only shows the switches for sports your panel follows, from your team, your favorites and the leagues you picked for Live games, so turning off football's line leaves baseball's count alone.

With every switch off during play, the ticker shows ESPN's short status for the game, like the clock.

The game clock always has its own row on the board, so it never goes in the ticker.

## Messages in the ticker

The ticker also says when something needs your attention:

- **Pick a team in the Game Day app or at** and the panel's IP address: the panel has never had a team, mode or favorite picked. It goes away once you pick one, from the app, the device page or Home Assistant. The IP address is there for phones that can't open `.local` names. See [Pick your team](/get-started/pick-your-team/).
- **no update for** and a number of minutes, or **no update yet**: the panel has missed three reads from ESPN in a row. The score may be old. It clears by itself when a read comes through.
- **Favorites: add teams in the app or on the page**: the panel is in Favorite teams with no favorites.
- **No live games, showing** and your team, like "No live NFL games, showing DAL": Live games found nothing on, so it shows your team until a game starts. See [Show: My team, Favorite teams, Live games](/using/modes/#when-nothing-is-live).

For the messages that take over the whole board, see [Panel messages](/help/panel-messages/).

:::note[Coming in the next release]
A panel that is too warm or on a weak power supply says so on its bottom line every two minutes, for 15 seconds: "Too warm, dimming" in orange or "Check power supply" in red. See [Brightness, panel on and off, and the clock](/using/brightness-and-clock/#heat-and-power-protection).
:::

## Ticker speed

**Ticker speed** in the Panel card sets how fast the ticker scrolls, from 1 (slowest) to 10 (fastest). A new panel starts at 5. The speed stays the same whatever the length of the line, so a long last play scrolls at the same pace as a short one.
