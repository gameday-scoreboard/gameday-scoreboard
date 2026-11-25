---
title: "Volleyball: women's college"
description: "What the panel shows for a women's college volleyball match: the set's points, the sets won, the finished sets and the set and match splashes."
sidebar:
  order: 9.5
---

Women's college volleyball matches get their own board. This page describes a 128x64 display and notes where a single 64x64 panel differs. See [Panel sizes and layouts](/using/panel-sizes/).

Every Division I team is in the **NCAAVB** tab of the team picker. The season runs from late August to the NCAA championship in December.

## Before the match

The next-match card shows both logos, your team on the left, with the records under them. A team in the Top 25 has its rank on the outer top corner of its logo, small white digits on a black patch, before and during the match. In the NCAA tournament the number is the team's seed. The ticker carries the day and time in the panel's timezone, the TV channel when ESPN has one and **Odds, TV and win chance** is on, and the arena. A tournament match starts with its name.

Volleyball teams have the Up next list on the device page, like football and soccer teams.

## During the match

- **The big numbers** are the points in the set being played, your team's first.
- **The set and the sets won** are under them, like "Set 3  1-1": the third set, one set each, your team's sets first.
- **Between sets** the numbers stay on the set that just ended, and the line under them says which, like "End 2  1-1", until the next set starts.

On a 128-wide board the row under that lists the **finished sets**, your team's points first, like "25-19 17-25". The ticker shows both records.

On a single 64x64 panel the set and the sets won take the abbreviations' place during play, and the finished sets are in the ticker.

ESPN doesn't send who is serving or the last play in volleyball, so the board and ticker show neither, and there are no leader cards.

## Set and match splashes

A set your team wins splashes with "SET!" in your team's color. The other side's says their abbreviation, like "WIS SET", unless you turn off **Opponent scores too** under **Celebrations** on the device page's Display tab. Points don't splash.

When your team wins the match, a victory splash says your abbreviation and "WINS", with the sets under it.

With **Celebrations** on **Full** a set gets the light sweep and the win the full victory screen. **Subtle** keeps both to a quick flash; **Off** shows no splash at all.

In Home Assistant, the scores and the score events are the sets won. See [Game events](/using/game-events/).

## The final

The final shows the sets, like 3-1, and the ticker says "Final" with the records. In Favorite teams the final stays up for the **After the final** time (see [Favorite teams](/using/favorites/#when-a-game-is-on)). When Live games shows your first favorite with nothing live, the final stays up for half an hour, then the panel looks up the team's next match.

At the final the board takes turns, 10 seconds each, with the line score: each team's points in every set, and the sets won under **S**. On a 64x64 panel the teams sit side by side under their abbreviations, with the sets down the left; a five-set match leaves out the **S** row, since the board it takes turns with says 3-2.

With **Last play** on, the final's ticker also runs ESPN's headline for the match when it has one.

A postponed or canceled match shows ESPN's word for it and gets no victory splash.
