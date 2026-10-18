---
title: "Show: My team, Favorite teams, Live games"
description: "The three things the panel can follow, how it picks a game, when it moves on, and what it shows when nothing is on."
sidebar:
  order: 3
---

The three buttons under the board on the device page, **My team**, **Live games** and **Favorite teams**, choose what the panel follows. The Game Day app has the same choice. The panel changes over right away and keeps the choice through restarts.

## My team

The panel follows one team: its next game before kickoff, the game while it's on, and the final after.

- **Before the game** the board shows both logos and the matchup, and the ticker gives the kickoff day and time in the panel's timezone, plus the line, over/under, TV channel and venue when ESPN has them. Each sport has its own details; see [Football](/using/football/), [Baseball](/using/baseball/), [Basketball](/using/basketball/), [Hockey](/using/hockey/) and [Soccer](/using/soccer/).
- **During the game** the panel reads ESPN every 10 seconds, about as often as ESPN updates.
- **After the final** the score stays up for half an hour, or ten minutes for baseball, since game two of a doubleheader can start soon after game one. Then the panel looks up the team's next game.

Far from kickoff the panel checks the game every 15 minutes, and every minute in the last hour before it starts. If a read from ESPN fails, it waits longer before each new try.

Your team is always on the left of the board.

### When your team has no game coming

Between seasons the board shows a clock in your team's colors instead of an empty scoreboard. Under the time is the next game once it's on the schedule, like "Next: at PHI Mar 26 7:05 PM", or the date until then. On a single 64x64 panel the next game goes in the ticker, since there is no room for it under the time.

Football teams get the clock only when ESPN lists no game at all, so a bye week never shows it. In the other leagues the clock comes up once the next game is more than 10 days away.

### A panel with no team picked

A new panel has no team. Until you pick one, it shows live NFL and college football games as Live games would, and when nothing is on, the latest final scores and the games still to come (see [When nothing is live](#when-nothing-is-live)). The ticker asks you to pick one until you choose a team, a mode or a favorite, and the QR code for the Game Day app comes back over the board for 13 seconds every 3 minutes until you pick a team. See [Pick your team](/get-started/pick-your-team/).

## Live games

The panel follows a game in progress, picked at random from the leagues you choose, and moves to another one when that game ends or after **Switch every** minutes (1 to 30). When it moves on, it picks a different game from the one it showed last, if another one is on. Splashes name the team that scored, like "DAL TOUCHDOWN", since there is no "your team" here. With **Opponent scores too** off, only the team on the left gets them.

### Choosing the leagues

The league buttons under the mode buttons pick the leagues. Until you pick any, Live games follows the leagues of your team and your favorites, and the page says so. Tap a league to choose by hand; **Back to automatic** goes back to following your team and favorites. At least one league stays on.

### When nothing is live

What the panel shows depends on whether you have a team:

- **With a team picked**, it shows your team's card, the same as My team, with a line like "No live NFL games, showing DAL" at the front of the ticker. It keeps looking, and switches to a live game when one starts.
- **With no team picked**, it rotates through the games still to come today under **Later today**, each with both logos, the start time and the TV channel, then the latest final scores for every league it follows, under headers like "Yesterday's NHL scores". When no game is left today, a "Next" card names the next matchup and its time, like "Next: BOS at NYR Thu 6:00 PM". A game drops off the Later today list once it starts.

The panel waits for the next kickoff before it checks again, instead of checking every few minutes, and never waits more than 3 hours. When there has been no game in the last week and none is coming, the board shows a gray clock and the ticker says "No live games right now".

:::note[Coming in the next release]
A change to the leagues, or to the favorites they follow, takes effect right away, instead of leaving the old leagues' final scores up until the next check.
:::

## Favorite teams

The panel cycles through up to 16 favorite teams, and a favorite's game takes the panel shortly before kickoff. See [Favorite teams](/using/favorites/) for how the playlist works and its settings.

Favorite teams with no favorites set works like My team, and the ticker asks you to add teams.

## Remote buttons and modes

Buttons 1 to 4 on a WizMote remote pick your first four favorites. In Favorite teams, a button jumps to that favorite. In My team or Live games, a button switches the panel to My team following that favorite. See [WizMote remote](/using/remote/).
