---
title: "Hockey: NHL"
description: "What the panel shows for an NHL game: the period and clock, overtime and shootouts, goal splashes and the playoff series."
sidebar:
  order: 8
---

NHL games get their own board. This page describes a 128x64 display and notes where a single 64x64 panel differs. See [Panel sizes and layouts](/using/panel-sizes/).

## Before the game

The next-game card shows both logos, your team on the left, with the records under them. A record too long for the space under a logo, like 30-12-10, shows as the team's points instead: two for a win and one for an overtime loss, so **70 pts**. The ticker carries the puck drop day and time in the panel's timezone, the playoff series when there is one, the betting line, over/under and TV channel when ESPN has them and **Odds, TV and win chance** is on, and the arena.

Before a playoff game the ticker starts with the round, like "East 1st Round - Game 2".

After the odds the ticker shows each team's points leader for the season, like "McDavid 9 pts". **Pre-game season leaders** on the device page turns them off.

NHL teams have no Up next list on the device page: their season schedules are too large for the panel to read.

## During the game

- **The clock and period** under the scores, like "12:34 2nd".
- **Overtime** reads "OT". In the playoffs, where overtime goes on until someone scores, later periods read "2OT", "3OT" and so on.
- **A shootout**, in the regular season, reads "SO".
- **Intermissions** show ESPN's words, like "End of 1st", and the ticker shows the series, or the records outside the playoffs.
- **The ticker** with the last play. Turn it off with **Last play** under **Ticker** on the device page's Display tab. See [The ticker](/using/ticker/).


On a 128-wide board the row under the clock shows the **score margin** over the game: a small chart of the lead, built from every read the panel has made since it started showing the game. Your team's lead fills up from the middle in your color, the other team's fills down in theirs, and a white pixel marks the latest read. A three-goal lead fills the row. The chart keeps the last two games the board showed, so Live games switching away and back picks up where it left off; a restart starts over. Between periods nothing changes, and after the final the row is left to the line score and leaders cards. **Score margin** under **Live pictures** on the device page's Display tab turns it off. ESPN publishes no win chance for hockey, so there is no win chance bar.

:::note[Coming in the next release]
The score margin chart goes, and so does its switch. In its place the row under the clock spells out **shots on goal**, like "Shots on goal 12-16", your team's shots first, the way the scores sit. After the first goal it adds **the latest goal**: when it went in and who scored it, like "Shots 12-16  12:04 Kane". A goal from an earlier period shows its period instead of the clock, like "Shots 21-19  1st Kane", since the clock row above already says which period it is now. A long name shows on its own, like "Shots 6-16  MacKinnon", and one too long to fit leaves the row to the shots. The goal stays on the row until the next one; a goal taken back on review comes off. Now and then ESPN never shows a goal on its own, often one that went to video review; the row then shows just the shots until the next goal. Shots come from ESPN's saves and goals, so shootout attempts don't count. Between periods and after the final the row is left to the line score and leaders cards, as before. A panel that starts mid-game shows the shots right away and the goal from the next one on.
:::

The board doesn't show power plays. ESPN's NHL data has no power play field, and working one out from the penalties would be wrong when penalties offset.

On a single 64x64 panel the clock takes the abbreviations' place during play, and there is no room for the records.

## Goal splashes

A goal fills the screen for 4 seconds with fireworks on black, in the scoring team's color. Yours says "GOAL". The other side's says their abbreviation and "GOAL", like "PIT GOAL", unless you turn off **Opponent scores too** under **Celebrations** on the device page's Display tab. The winning goal in a shootout splashes like any other.

When your team wins, a victory splash with fireworks says your abbreviation and "WINS".

With **Celebrations** on **Full**, your goal lights the goal lamp: a red beam spins from the middle of the board, the red glow pulses and the frame flashes, with "GOAL!" in white, for 4.5 seconds. The win holds for 10 seconds with the final score under your abbreviation and confetti in your colors. The other side's goals are a dim 2-second flash. **Subtle** keeps every goal to the quick flash; **Off** shows no splash at all.

### Big plays

:::note[Coming in the next release]
With **Celebrations** on **Full** and **Big plays** on (the default), a **hat trick**, one player's third goal of the game, gets a pixel-art show in your team's colors in place of the plain goal lamp: the lamp spins, "HAT TRICK!" drops in letter by letter and hats rain down onto the ice. 4.5 seconds.

Your team's hat tricks show; in the Live games mode either team's do, the second team's only with **Opponent scores too** on. Turn **Big plays** off under **Celebrations** on the device page to keep the plain goal lamp. On **Subtle** the goal flashes like any other.

The panel counts goals from ESPN's goal count for each player during the game, so it can miss a hat trick when it started showing the game after the player's second goal, but it never shows one that isn't. When ESPN's count catches up a read after the goal, the goal lamp plays first and the hat trick on the next read, about 10 seconds later.

Power play and shorthanded goals get the plain goal lamp: ESPN's live game data doesn't say which goals they are.
:::

## The playoff series

In the playoffs the series, like "CAR leads series 2-0", is in the ticker before the game, and takes the records' place during intermissions and at the final. It isn't on the board itself.

## The final

The ticker says "Final", or "Final/OT", "Final/SO" or "Final/2OT", with the series or the records. In Favorite teams the final stays up for the **After the final** time (see [Favorite teams](/using/favorites/#when-a-game-is-on)). When Live games shows your first favorite with nothing live, the final stays up for half an hour, then the panel looks up the team's next game.

At the final the board takes turns, 10 seconds each, with the line score: each team's goals by period and the total, with one OT column after overtime (SO after a shootout). On a 64x64 panel the teams sit side by side under their abbreviations, with the periods down the left.

A **Game leaders** card joins the turns: each team's logo and abbreviation with its goal and assist leaders, like "Johnson 2 goals" and "Werenski 2 assists". It shows once at the second intermission and takes its turn at the final. On a 64x64 panel it's the goal leader only, under each abbreviation. Turn it off under **Between games** on the [device page](/using/device-page/).

With **Last play** on, the final's ticker also runs the goals the panel saw, like "P2 12:04 Crosby goal, PIT 2-1", and ESPN's headline for the game when it has one.

A postponed, canceled or suspended game shows ESPN's word for it and gets no victory splash.
