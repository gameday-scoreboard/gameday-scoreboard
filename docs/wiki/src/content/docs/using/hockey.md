---
title: "Hockey: NHL"
description: "What the panel shows for an NHL game: the period and clock, overtime and shootouts, goal splashes and the playoff series."
sidebar:
  order: 8
---

NHL games get their own board. This page describes a 128x64 display and notes where a single 64x64 panel differs. See [Panel sizes and layouts](/using/panel-sizes/).

## Before the game

The next-game card shows both logos, your team on the left, with the records under them. The ticker carries the puck drop day and time in the panel's timezone, the playoff series when there is one, the betting line, over/under and TV channel when ESPN has them and **Odds, TV and win chance** is on, and the arena.

Before a playoff game the ticker starts with the round, like "East 1st Round - Game 2".

After the odds the ticker shows each team's points leader for the season, like "McDavid 9 pts". **Pre-game season leaders** on the device page turns them off.

NHL teams have no Up next list on the device page: their season schedules are too large for the panel to read.

## During the game

- **The clock and period** under the scores, like "12:34 2nd".
- **Overtime** reads "OT". In the playoffs, where overtime goes on until someone scores, later periods read "2OT", "3OT" and so on.
- **A shootout**, in the regular season, reads "SO".
- **Intermissions** show ESPN's words, like "End of 1st", and the ticker shows the series, or the records outside the playoffs.
- **The ticker** with the last play. Turn it off with **Last play** in the Ticker card. See [The ticker](/using/ticker/).


:::note[Coming in the next release]
On a 128-wide board the row under the clock shows the **score margin** over the game: a small chart of the lead, built from every read the panel has made since it started showing the game. Your team's lead fills up from the middle in your color, the other team's fills down in theirs, and a white pixel marks the latest read. A three-goal lead fills the row. The chart keeps the last two games the board showed, so Live games switching away and back picks up where it left off; a restart starts over. Between periods nothing changes, and after the final the row is left to the line score and leaders cards. **Score margin** in the Live pictures card on the device page turns it off. ESPN publishes no win chance for hockey, so there is no win chance bar.
:::

The board doesn't show power plays. ESPN's NHL data has no power play field, and working one out from the penalties would be wrong when penalties offset.

On a single 64x64 panel the clock takes the abbreviations' place during play, and there is no room for the records.

## Goal splashes

A goal fills the screen for 4 seconds with fireworks on black, in the scoring team's color. Yours says "GOAL". The other side's says their abbreviation and "GOAL", like "PIT GOAL", unless you turn off **Opponent scores too** in the Celebrations card. The winning goal in a shootout splashes like any other.

When your team wins, a victory splash with fireworks says your abbreviation and "WINS".

## The playoff series

In the playoffs the series, like "CAR leads series 2-0", is in the ticker before the game, and takes the records' place during intermissions and at the final. It isn't on the board itself.

## The final

The ticker says "Final", or "Final/OT", "Final/SO" or "Final/2OT", with the series or the records. In Favorite teams the final stays up for the **After the final** time (see [Favorite teams](/using/favorites/#when-a-game-is-on)). When Live games shows your first favorite with nothing live, the final stays up for half an hour, then the panel looks up the team's next game.

At the final the board takes turns, 10 seconds each, with the line score: each team's goals by period and the total, with one OT column after overtime (SO after a shootout). On a 64x64 panel the teams sit side by side under their abbreviations, with the periods down the left.

:::note[Coming in the next release]
A **Game leaders** card joins the turns: each team's logo and abbreviation with its goal and assist leaders, like "Johnson 2 goals" and "Werenski 2 assists". It shows once at the second intermission and takes its turn at the final. On a 64x64 panel it's the goal leader only, under each abbreviation. Turn it off under **Between games** on the [device page](/using/device-page/).
:::

With **Last play** on, the final's ticker also runs the goals the panel saw, like "P2 12:04 Crosby goal, PIT 2-1", and ESPN's headline for the game when it has one.

A postponed, canceled or suspended game shows ESPN's word for it and gets no victory splash.
