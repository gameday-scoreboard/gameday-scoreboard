---
title: "Soccer: MLS and the Premier League"
description: "What the panel shows for an MLS or Premier League match: the minute and stoppage time, the latest goal, red cards, cup games and the next three fixtures."
sidebar:
  order: 9
---

MLS and Premier League matches share one board. This page describes a 128x64 display and notes where a single 64x64 panel differs. See [Panel sizes and layouts](/using/panel-sizes/).

## Cup games

The panel follows your club into its cup competitions too: Premier League clubs into the Champions League, Europa League and Conference League, and MLS clubs into the CONCACAF Champions Cup. Whichever game comes first, league or cup, is the one on the board. A cup game's ticker names its stage, like "UEFA Champions League, League Phase".

## Before the match

The next-game card shows both logos, your club on the left, with each club's record under its logo as wins, draws and losses. A record too long to fit there shows as points instead, like "52 pts". The ticker carries:

- the kickoff day and time in the panel's timezone
- the cup stage, for a cup game
- each club's recent form
- the three-way odds, like "ARS -275, draw +425, LEE +700", when ESPN has them and **Odds, TV and win chance** is on
- the stadium

## During the match

- **The minute** in the clock row, like "67'". Stoppage time adds to the half's last minute, like "45'+3'".
- **Breaks**: "HT" at half time. Extra time reads "ET" and the minute, like "ET 105'", and "ET HT" at its break. A penalty shootout reads "PENS" with the score, like "PENS 3-2".
- **The latest goal** on the row under the clock, with the minute and the scorer, like "52' Saka". An own goal is marked "OG" and a penalty "(P)".
- **Red cards** as small red marks under the carded club's logo, one per red card, up to three.
- **The ticker** with the match stats and the latest goal or card. See below.

On a single 64x64 panel there is no room for the latest goal row, the red card marks or the records. The match stats in the ticker carry the goals and cards.

### Match stats in the ticker

With **Match stats** on under **Ticker** on the device page's Display tab, the ticker lists:

- the goals for each side with the minute and scorer, like "Goals ARS 58' Guimaraes, 90'+7' Saka (P); SUN 12' Isidor"
- yellow and red cards for each side, like "2Y 1R"
- possession and shots, with shots on target in brackets

**Last play** adds the latest goal or card. At half time the ticker shows the goals, cards, stats and records, whatever the switches say. See [The ticker](/using/ticker/).

## Goal splashes

A goal fills the screen for 4 seconds with fireworks on black, in the scoring club's color. Yours says "GOAL". The other side's says their abbreviation and "GOAL", like "LEE GOAL", unless you turn off **Opponent scores too** under **Celebrations** on the device page's Display tab.

At full time a win gets a victory splash with fireworks, your abbreviation and "WINS". A draw gets no splash. When a shootout decides a game, winning the shootout counts as the win. That includes the second leg of a two-legged cup tie, where the score shown is that leg's alone: a 2-1 loss on the night that goes to penalties and is won there is your win, with the shootout in brackets, like "1-2 (4-3)".

With **Celebrations** on **Full**, your goal ripples the net: the board becomes the back of the goal, the strands roll outward from where the ball hit and sparks fly in your club's colors under "GOAL!", for 4.5 seconds. The win holds for 10 seconds with the final score under your abbreviation and confetti in your colors. The other side's goals are a dim 2-second flash. **Subtle** keeps every goal to the quick flash; **Off** shows no splash at all.

:::note[Coming in the next release]
**Big plays.** With **Celebrations** on **Full** and **Big plays** on, some goals get their own 5-second pixel-art show in your club's colors in place of the plain net ripple:

- **BRACE!** A player's second goal of the match: the ball hits the net and it ripples, then the scorer's name, like "SAKA", with two balls bouncing in under it.
- **HAT TRICK!** That player's third: the same, with three balls.
- **PENALTY!** A penalty kick scored in the match (not the shootout): the ball on the spot, the keeper dives the wrong way, and the ball hits the other corner.
- **LATE GOAL!** A goal from the 85th minute on that puts you ahead (or in the last five minutes of extra time): the net ripples, then the minute, like "89'", flashing in a clock ring.

When a goal is more than one of these, the rarest wins: a hat trick, then a late goal, then a brace, then a penalty. An own goal never counts toward a player's brace. On a single 64x64 panel each show is drawn to fit: the titles split over two lines and a long name shows smaller or by its last word.

They show for your club's goals. The other side's big plays get the same dim flash as their other goals, and in **Live games**, where neither side is yours, the first club's show in its colors and the second club's do too with **Opponent scores too** on. Turn **Big plays** off under **Celebrations** to keep every goal to the net ripple; **Subtle** and **Off** never show them.
:::

## Full time

The clock row says "FT", "AET" after extra time, or "PENS" after a shootout. The ticker says "Full time", "After extra time" or who won on penalties, like "PSG win 4-3 on penalties", then the cup stage, the goals, any red cards and both records. With **Last play** on, it also adds ESPN's headline for the match when it has one. In Favorite teams the final stays up for the **After the final** time (see [Favorite teams](/using/favorites/#when-a-game-is-on)). When Live games shows your first favorite with nothing live, the final stays up for half an hour, then the panel looks up the next match.

A postponed or canceled match shows ESPN's word for it and gets no victory splash.

## The next three games

On the device page, the **Up next** list under the board shows your club's next three games after the one on the board, with the opponent, the day and time and the TV channel. It comes from the club's league schedule, so a cup game the panel is about to follow may not be in it.
