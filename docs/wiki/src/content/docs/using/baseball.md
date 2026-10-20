---
title: "Baseball: MLB"
description: "What the panel shows for an MLB game: the inning, the count, outs, the runners on a diamond, run and home run splashes, and the final."
sidebar:
  order: 6
---

MLB games get their own board. This page describes a 128x64 display and notes where a single 64x64 panel differs. See [Panel sizes and layouts](/using/panel-sizes/).

## Before the game

The next-game card shows both logos, your team on the left, with the records under them. The ticker carries:

- the first pitch day and time in the panel's timezone
- the series, when ESPN has one
- the probable starting pitchers for both teams, when ESPN lists both
- the betting line, over/under and TV channel, when ESPN has them and **Pre-game odds and TV** is on
- the ballpark

MLB has no Up next list on the device page: a team's season schedule is too large for the panel to read.

## During the game

- **The inning** in the clock row, in ESPN's words, like "Top 5th", "Mid 5th" or "Rain Delay".
- **The count and outs**: the balls and strikes to the left of a small diamond and the outs, like "2 OUT", to its right.
- **The runners**: the diamond's bases light gold for each runner. With the bases loaded they turn red.
- **The ticker** with the count, the outs, the pitcher and the batter, and the last play. Turn them off with **Count and at-bat** and **Last play** in the Ticker card. See [The ticker](/using/ticker/).

Between half innings the ticker shows each team's hits and errors instead, with the series when there is one.

On a single 64x64 panel the diamond sits between the two scores, with the outs as three dots under it. The count is in the ticker.

## Runs and home runs

When the score changes, the panel fills the screen for 4 seconds in the scoring team's color:

| Score | Your team | The other side |
| --- | --- | --- |
| One run | RUN | the abbreviation and SCORES, like "NYY SCORES" |
| Two or more runs in one update | the number and RUNS, like "2 RUNS" | the abbreviation and the runs, like "NYY 2 RUNS" |
| Home run | HOME RUN, with fireworks | the abbreviation and HOME RUN, with fireworks |

A home run gets fireworks on black with the words in the team's color. Other runs flash the screen in the team's color. The other side's splashes need **Opponent scores too** on.

When your team wins, a victory splash with fireworks says your abbreviation and "WINS".

## The final

The ticker says "Final", or "Final/10" after extra innings, with each team's hits and errors and the series or the records. The final stays up for ten minutes in My team, shorter than other sports because game two of a doubleheader can start soon after game one. Then the panel looks up your team's next game.

:::note[Coming in the next release]
At the final the board takes turns, 10 seconds each, with the line score: runs by inning in groups of three, then runs, hits and errors, like a box score. An inning the home team didn't need to bat in shows an x. A long extra-inning game drops the groups, and a very long one shows runs, hits and errors alone. On a 64x64 panel it's runs, hits and errors, the teams side by side under their abbreviations.

With **Last play** on, the final's ticker also runs the scoring plays the panel saw, like "T6 Riley homered to left, Acuna scored, ATL 2-0", and ESPN's headline for the game when it has one.
:::

A postponed, canceled or suspended game shows ESPN's word for it and gets no victory splash.
