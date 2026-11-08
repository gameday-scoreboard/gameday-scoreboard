---
title: "Football: NFL and college"
description: "What the panel shows for an NFL or college football game: the next-game card, the live board, the scoring splashes, halftime and the final."
sidebar:
  order: 5
---

NFL and college football games share one board. What you see depends on your panel size; this page describes a 128x64 display and notes where a single 64x64 panel differs. See [Panel sizes and layouts](/using/panel-sizes/).

## Before the game

The next-game card shows both team logos, your team on the left, with each team's abbreviation in its own color and the season records under the logos.

:::note[Coming in the next release]
A team ESPN has no logo for, like a small-division school on a college schedule, shows its abbreviation in the logo's place, in its team color. A team color too dark to read on the panel, like navy or maroon, uses the team's second color, or white when that is dark too or ESPN has no color for the team.
:::

A college team in the AP Top 25 has its rank on the outer top corner of its logo, small white digits on a black patch, like **6** for the sixth-ranked team. It stays there during the game and at the final, and it's on the Later today and final score cards [Live games](/using/modes/#when-nothing-is-live) shows when nothing is live. An unranked team's logo is untouched. The rank comes from ESPN with the game, so it changes when the poll does. It is always drawn; the **College Top 25** switch on the device page is for the [Top 25 card](/using/top-25/) between games, not for this.

The ticker along the bottom carries:

- the game's name for bowl and playoff games, like "Rose Bowl" or "NFC Wild Card"
- the kickoff day and time in the panel's timezone, like "Sat 3:30 PM" or "Tomorrow 7:15 PM"
- the betting line, the over/under ("O/U") and the TV network, when ESPN has them and **Odds, TV and win chance** is on
- the venue

In the regular season the ticker starts with the week, like "Week 8", where a bowl or playoff game has its name. At an open-air stadium the venue comes with ESPN's forecast for the game, like "76F Cloudy at Hard Rock Stadium". A dome, or a game with no forecast yet, shows just the venue.

A game whose kickoff time isn't set yet, like a college game a couple of weeks out, shows the day and "TBD", like "Sat TBD".

The day changes at midnight in the panel's timezone, so a game that read "Tomorrow 1:30 PM" in the evening reads "Today 1:30 PM" from 12:00 AM.

After the odds, the ticker also shows each team's passing leader for the season with yards and touchdowns, like "Prescott 1,381 yds 9 TD", when ESPN has one. Early in the season, before a team has a passing line, it shows the rushing or receiving leader instead. **Pre-game season leaders** on the device page turns them off.

On a single 64x64 panel, which has no room for the records under the logos, the ticker shows both teams' records after the kickoff time, like "DAL 2-3, GB 3-2".

## During the game

At kickoff the board switches to the live scoreboard:

- **Scores** in large type in the middle.
- **The game clock and quarter** under them, like "12:34 2nd", or "OT" in overtime.
- **Down and distance** under the clock, like "3rd & 4". It turns red in the red zone.
- **Possession**: a gold line under the abbreviation of the team with the ball. The abbreviations stay in team colors.
- **Records** under each logo.
- **A college team's AP rank** on the outer top corner of its logo.
- **Timeout pips** under each logo: three bars per team, lit gold for each timeout left and dim for each one used.
- **The ticker** with the down, distance and where the ball is, like "3rd & 4 at FIU 31", and the last play. Turn either off under **Ticker** on the device page's Display tab. See [The ticker](/using/ticker/).

On a 128-wide board the down and distance row becomes a **field strip**: a picture of the field across the board with your end zone on the left under your logo and the other team's on the right, each in its team's color. A white marker shows where the ball is, with a tip pointing the way the offense is going, and a gold line marks the line to gain. In the red zone the field between the ball and the goal line glows red and the marker turns red. Each team's timeouts left show as dots in its end zone (three, two, one or none), in place of the timeout pips. The down and distance text, like "3rd & 4", sits on the half of the strip away from the ball.

Just above the strip, a one-pixel **win chance bar** runs the width of the board: your color from the left for ESPN's chance of your team winning, the other team's color for the rest, with a white pixel at the split. It moves with every play.

Both are on to start. **Field strip** and **Win chance bar** under **Live pictures** on the device page's Display tab turn them off; with the strip off, the row shows the down and distance and the timeout pips as before. A single 64x64 panel has no room for either.

:::note[Coming in the next release]
The one-pixel win chance bar goes. The win chance is in words instead, on the field strip where the down and distance sits: the down and distance for about 8 seconds, then the team ESPN favors for about 4, like "ND 84% to win", in white like the down and distance, shortened to "ND 84% win" or "ND 84%" when the half of the strip is too narrow. A new down shows right away. The switch is now called **Win chance**, and it turns that line on or off. On a 128-wide board the ticker leaves the win chance out, so it isn't said twice; a single 64x64 panel keeps it in the ticker.

In the red zone the whole last 20 yards glow red, from the 20 to the goal line, with the ball and the line to gain drawn on top, so it stands out even at the 1.
:::

After a touchdown or field goal, the ticker adds how the drive went next to the scoring play, like "Drive: 4 plays, 58 yards, 1:36". It follows **Last play**. When ESPN has a win chance for the game, the ticker ends with the side it favors, like "KC 72% to win", until the final. It follows **Odds, TV and win chance**.

On a single 64x64 panel, the logos sit side by side with the scores under them. During play the clock takes the abbreviations' place and the gold possession line goes under the score. There is no room for records, timeout pips or the down and distance row, so down and distance is only in the ticker. A ranked college team's rank still sits on its logo's corner.

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

The other side's scores show the same way in their color with their abbreviation in front, like "PHI TOUCHDOWN", unless you turn off **Opponent scores too** under **Celebrations** on the device page's Display tab.

With **Celebrations** on **Full**, your touchdown gets 5 seconds of fireworks in your team's colors over a gold goalpost, with the ball sailing through the uprights. A field goal, an extra point or a two-point score sweeps a bar of light across the board twice. The win holds for 10 seconds: your abbreviation and "WINS", the final score under it, and confetti falling in your colors. The other side's scores are a dim 2-second flash. **Subtle** keeps every score to the quick flash; **Off** shows no splash at all.

The panel compares each score with the one before, about every 10 seconds, so a touchdown and the extra point that land in the same update show as one TOUCHDOWN.

When your team wins, a victory splash with fireworks says your abbreviation and "WINS".

## Halftime and the final

At halftime the clock row says "Halftime" and the ticker shows both teams' records.

At the final the ticker says "Final" with the records. The final stays up for half an hour, then the panel moves on to your team's next game. In Live games the panel moves to another game instead, and in Favorite teams **After the final** sets how long it stays; see [Favorite teams](/using/favorites/).

A postponed, canceled or suspended game shows ESPN's word for it instead of "Final", and gets no victory splash.

At halftime and the final the board takes turns, 10 seconds each, with the line score: each team's points by quarter and the total, with one OT column after overtime. On a 64x64 panel it's by half, the teams side by side under their abbreviations.

A **Game leaders** card joins the turns: each team's logo and abbreviation with its passing, rushing and receiving leaders, like "Prescott 212 yds 3 TD". It shows once at halftime and takes its turn at the final. On a 64x64 panel it's the passing leader only, under each abbreviation, shortened to fit, like "Pres 212 yds". Turn it off under **Between games** on the [device page](/using/device-page/).

With **Last play** on, the ticker at halftime and the final also runs the scoring plays the panel saw, like "Q1 7:12 Lamb 12 yd pass, DAL 7-0", and at the final ESPN's headline for the game when it has one. A score the panel didn't see as it happened, because it started or switched to the game later, isn't in the list; the line score still counts it.
