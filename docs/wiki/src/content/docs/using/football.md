---
title: "Football: NFL and college"
description: "What the panel shows for an NFL or college football game: the next-game card, the live board, the scoring splashes, halftime and the final."
sidebar:
  order: 5
---

NFL and college football games share one board. What you see depends on your panel size; this page describes a 128x64 display and notes where a single 64x64 panel differs. See [Panel sizes and layouts](/using/panel-sizes/).

## Before the game

The next-game card shows both team logos, your team on the left, with each team's abbreviation in its own color and the season records under the logos.

The ticker along the bottom carries:

- the game's name for bowl and playoff games, like "Rose Bowl" or "NFC Wild Card"
- the kickoff day and time in the panel's timezone, like "Sat 3:30 PM" or "Tomorrow 7:15 PM"
- the betting line, the over/under ("O/U") and the TV network, when ESPN has them and **Pre-game odds and TV** is on
- the venue

A game whose kickoff time isn't set yet, like a college game a couple of weeks out, shows the day and "TBD", like "Sat TBD".

## During the game

At kickoff the board switches to the live scoreboard:

- **Scores** in large type in the middle.
- **The game clock and quarter** under them, like "12:34 2nd", or "OT" in overtime.
- **Down and distance** under the clock, like "3rd & 4". It turns red in the red zone.
- **Possession**: a gold line under the abbreviation of the team with the ball. The abbreviations stay in team colors.
- **Records** under each logo.
- **Timeout pips** under each logo: three bars per team, lit gold for each timeout left and dim for each one used.
- **The ticker** with the down, distance and where the ball is, like "3rd & 4 at FIU 31", and the last play. Turn either off in the Ticker card on the device page. See [The ticker](/using/ticker/).

:::note[Coming in the next release]
After a touchdown or field goal, the ticker adds how the drive went next to the scoring play, like "Drive: 4 plays, 58 yards, 1:36". It follows **Last play**. When ESPN has a win chance for the game, the ticker ends with the side it favors, like "KC 72% to win", until the final. It follows **Pre-game odds and TV**.
:::

On a single 64x64 panel, the logos sit side by side with the scores under them. During play the clock takes the abbreviations' place and the gold possession line goes under the score. There is no room for records, timeout pips or the down and distance row, so down and distance is only in the ticker.

## Scoring splashes

When the score changes, the panel fills the screen with a splash for 4 seconds, in the scoring team's color. Your team's splashes say:

| Score | Splash |
| --- | --- |
| Touchdown (6 to 8 points at once) | TOUCHDOWN, with fireworks |
| Field goal | FIELD GOAL |
| Extra point | EXTRA POINT |
| Two points (a conversion or a safety) | 2-POINT |
| Any other score | SCORE |

A touchdown gets fireworks on black with the words in your team's color. The smaller scores flash the screen in your team's color with white words.

The other side's scores show the same way in their color with their abbreviation in front, like "PHI TOUCHDOWN", unless you turn off **Opponent scores too** in the Celebrations card.

The panel compares each score with the one before, about every 10 seconds, so a touchdown and the extra point that land in the same update show as one TOUCHDOWN.

When your team wins, a victory splash with fireworks says your abbreviation and "WINS".

## Halftime and the final

At halftime the clock row says "Halftime" and the ticker shows both teams' records.

At the final the ticker says "Final" with the records. The final stays up for half an hour, then the panel moves on to your team's next game. In Live games the panel moves to another game instead, and in Favorite teams **After the final** sets how long it stays; see [Favorite teams](/using/favorites/).

A postponed, canceled or suspended game shows ESPN's word for it instead of "Final", and gets no victory splash.

:::note[Coming in the next release]
At halftime and the final the board takes turns, 10 seconds each, with the line score: each team's points by quarter and the total, with one OT column after overtime. On a 64x64 panel it's by half, the teams side by side under their abbreviations.

With **Last play** on, the ticker at halftime and the final also runs the scoring plays the panel saw, like "Q1 7:12 Lamb 12 yd pass, DAL 7-0", and at the final ESPN's headline for the game when it has one. A score the panel didn't see as it happened, because it started or switched to the game later, isn't in the list; the line score still counts it.
:::
