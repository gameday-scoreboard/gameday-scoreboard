---
title: "Basketball: NBA, WNBA and college"
description: "What the panel shows for an NBA, WNBA or men's college basketball game: the period and clock, the playoff series and the victory splash."
sidebar:
  order: 7
---

NBA, WNBA and men's college basketball games share one board. This page describes a 128x64 display and notes where a single 64x64 panel differs. See [Panel sizes and layouts](/using/panel-sizes/).

## Before the game

The next-game card shows both logos, your team on the left, with the records under them. A men's college basketball team in the AP Top 25 has its rank on the outer top corner of its logo, small white digits on a black patch, before and during the game; see [Football](/using/football/#before-the-game) for the details. The ticker carries the tip-off day and time in the panel's timezone, the playoff series when there is one, the betting line, over/under and TV channel when ESPN has them and **Odds, TV and win chance** is on, and the arena.

:::note[Coming in the next release]
A team ESPN has no logo for, like a Division III school playing a Division I team, shows its abbreviation in the logo's place, in its team color. A team color too dark to read on the panel uses the team's second color, or white when that is dark too or ESPN has no color for the team.
:::

Before a playoff game the ticker starts with the round, like "East 1st Round - Game 2".

After the odds the ticker shows each team's scoring leader for the season in points per game, like "Allen 21.0 PPG", once ESPN has one (it lists none in the preseason). **Pre-game season leaders** on the device page turns them off.

Basketball teams have no Up next list on the device page: their season schedules are too large for the panel to read.

## During the game

- **The clock and period** under the scores, like "1:04 4th", or "45.3 4th" in the last minute, when ESPN drops the minutes. Overtime reads "OT", then "2OT" and so on. College games play halves, so the clock reads "12:34 1st" or "12:34 2nd".
- **Breaks**: "Halftime", or "End" and the period, like "End 3rd".
- **The playoff series** on the row under the clock, like "NY lead 2-1" or "Series 2-2", when the game is a playoff game. It takes the score margin chart's place there.
- **The ticker** with the last play. During a break it shows the series, or the records outside the playoffs.


On a 128-wide board the row under the clock shows the **score margin** over the game: a small chart of the lead, built from every read the panel has made since it started showing the game. Your team's lead fills up from the middle in your color, the other team's lead fills down in theirs, and a white pixel marks the latest read. A 10-point lead fills the row; smaller leads are proportionally shorter. In a playoff game the row shows the series instead. The chart keeps the last two games the board showed, so Live games switching away and back, or two favorites alternating, picks up where it left off; a restart starts over. At halftime and after the final the row is left to the line score and leaders cards. A one-pixel **win chance bar** just above it shows ESPN's chance for each side, your color from the left. The panel samples both from the game document it already reads, so nothing extra is downloaded. **Score margin** and **Win chance bar** under **Live pictures** on the device page's Display tab turn them off.

:::note[Coming in the next release]
The chart and the bar go. The row under the clock shows words instead, one line at a time, about 4 seconds each, using the full width of the board:

- **The last basket**, like "McDaniels 3", or "Nesmith FT" for a free throw. It stays until the next basket, and a new one shows right away. It follows **Last play**.
- **Each team's points leader** in this game, like "Edwards 24".
- **The playoff series**, like "NY lead 2-1", in a playoff game.
- **The win chance**, like "MIN 56% to win", for the team ESPN favors. **Win chance** under **Live pictures** turns it off.

Every line is in white. At halftime only the series stays, and the line score and leaders cards take their turns as before.
:::

Basketball has no game situation line, so there is no ticker switch for one. **Last play** under **Ticker** on the device page's Display tab turns the last play on or off. See [The ticker](/using/ticker/).

On a single 64x64 panel the clock takes the abbreviations' place during play. There is no room for the series row or the records.

## Splashes

Basketball doesn't splash every basket: a game has around a hundred scoring plays, so that would keep the screen covered. It celebrates the win, and on **Full** a three. When your team wins, a victory splash with fireworks says your abbreviation and "WINS".

With **Celebrations** on **Full**, a three gets 3 seconds: the ball arcs up and drops through the hoop, the net swings and sparks pop in your team's colors under "THREE!". It takes a made three that is the last play in ESPN's play-by-play and the only change between two reads, so an and-one, three free throws, or a three read together with another basket stay quiet, and so does a three that ESPN logs with another play after it (a missed shot, a foul) before the panel reads the game. The other side's threes stay quiet too. The win holds for 10 seconds with the final score under your abbreviation and confetti in your colors. **Subtle** keeps the win to a quick flash and leaves baskets alone; **Off** shows no splash at all.

:::note[Coming in the next release]
**Big plays.** With **Celebrations** on **Full** and **Big plays** on (the default), two baskets get a pixel-art scene of their own, drawn in your team's colors:

- **Dunk**: a player dribbles in, rises and slams it through. The rim bends and shakes, the net snaps, the crowd lights up and "DUNK!" drops in. 2.5 seconds.
- **Alley-oop**: the lob floats high to the rim (thrown by a teammate on a two-panel board), the player catches it at the top of the leap and throws it down, then "ALLEY-OOP!" drops in. On a single 64x64 panel the words come one at a time, "ALLEY" then "OOP!". 3.5 seconds.

The panel goes by the play type ESPN gives the basket: any dunk (running, cutting, driving, tip, putback) is a dunk, and an alley-oop, dunked or laid in, is an alley-oop. Like the three, it takes a made basket that is the last play ESPN lists and the only change between two reads, so a dunk with an and-one free throw read together, or one ESPN follows with another play before the panel reads the game, stays quiet. A team gets about four of these a game. Your team's show; in the Live games mode either team's do, the second team's only with **Opponent scores too** on.

Turn **Big plays** off under **Celebrations** on the device page to keep every score's and the win's show and drop only these. **Subtle** and **Off** never show them.
:::

## The final

The ticker says "Final", or "Final/OT" after overtime, with the series or the records. In Favorite teams the final stays up for the **After the final** time (see [Favorite teams](/using/favorites/#when-a-game-is-on)). When Live games shows your first favorite with nothing live, the final stays up for half an hour, then the panel looks up the team's next game.

At halftime and the final the board takes turns, 10 seconds each, with the line score: each team's points by quarter (by half in men's college basketball) and the total, with one OT column after overtime. On a 64x64 panel it's by half, the teams side by side under their abbreviations. With **Last play** on, the final's ticker adds ESPN's headline for the game when it has one. Basketball has no list of scoring plays: nearly every trip down the floor scores.

A **Game leaders** card joins the turns: each team's logo and abbreviation with its top scorer and top rebounder, like "Tatum 40 pts" and "George 7 reb". It shows once at halftime and takes its turn at the final. On a 64x64 panel it's the top scorer only, under each abbreviation. Turn it off under **Between games** on the [device page](/using/device-page/).

A postponed, canceled or suspended game shows ESPN's word for it and gets no victory splash.
