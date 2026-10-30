---
title: "Show: Favorite teams, Live games"
description: "The two things the panel can follow, how it picks a game, when it moves on, and what it shows when nothing is on."
sidebar:
  order: 3
---

The two buttons under the board on the device page, **Favorite teams** and **Live games**, choose what the panel follows. The Game Day app has the same choice. The panel changes over right away and keeps the choice through restarts.

Favorite teams replaces My team. To follow one team, make it your only favorite. A panel that was in My team starts in Favorite teams after the update. The team it followed isn't copied over, so add it as a favorite. Live games with nothing on shows your first favorite instead of the My team pick, and the **Team** entity is gone from Home Assistant.

## Favorite teams

The panel cycles through up to 16 favorite teams, and a favorite's game takes the panel shortly before kickoff. With one favorite, the panel follows that one team. See [Favorite teams](/using/favorites/) for how it works, its settings and playlists.

### How a team's game is shown

- **Before the game** the board shows both logos and the matchup, and the ticker gives the kickoff day and time in the panel's timezone, plus the line, over/under, TV channel and venue when ESPN has them. Each sport has its own details; see [Football](/using/football/), [Baseball](/using/baseball/), [Basketball](/using/basketball/), [Hockey](/using/hockey/) and [Soccer](/using/soccer/).
- **During the game** the panel reads ESPN every 10 seconds, about as often as ESPN updates.
- **After the final** the score stays up for a while, then the panel moves on to the team's next game.

Far from kickoff the panel checks the game every 15 minutes, and every minute in the last hour before it starts. If a read from ESPN fails, it waits longer before each new try.

Your team is always on the left of the board.

Before the game, the board takes turns with a big countdown to kickoff in your team's colors, like "2d 14h". See [Countdowns](/using/countdowns/).

### When your teams have no game coming

Between seasons the board shows a clock instead of an empty scoreboard. See [Favorite teams](/using/favorites/) and [Brightness and the clock](/using/brightness-and-clock/).

### A panel with no favorites

A new panel has no favorites. Until you add one, it shows live games as Live games would, and when nothing is on, the latest final scores and the games still to come (see [When nothing is live](#when-nothing-is-live)). The ticker asks you to pick a team until you choose a mode or a favorite, and the QR code for the Game Day app comes back over the board for 13 seconds every 3 minutes until you add a team. In Favorite teams with no favorites, the ticker starts with "Favorites: add teams in the app or on the page". See [Pick your team](/get-started/pick-your-team/).

A panel with no favorites follows live games in every league it supports, not only football: NFL, college football, MLB, MLS, Premier League, NBA, WNBA, men's college basketball and NHL. On a weeknight in October that means the hockey, basketball and baseball games that are on. College basketball comes from the Top 25 games until you add a favorite, and once you add a college basketball team, or choose men's college basketball in Live games, the panel follows every Division I game. When nothing is live, the Later today games and the final scores cover every league too.

The app's QR code still comes back every 3 minutes, now with **Pick your team in the app** beside it on a 128x64 panel. On a single 64x64 panel those words and the QR code take turns.

## Live games

The panel follows a game in progress, picked at random from the leagues you choose, and moves to another one when that game ends or after **Switch every** minutes (1 to 30). When it moves on, it picks a different game from the one it showed last, if another one is on. Splashes name the team that scored, like "DAL TOUCHDOWN", since there is no "your team" here. With **Opponent scores too** off, only the team on the left gets them.

Live games follows the best game on instead of a random one, with the **Pick the best game** switch on, as it is on a new or updated panel:

- **The best game** is the closest one with the least time left. A game in overtime or extra innings comes first, and in college a ranked team, more so a top-ranked one, moves a game up.
- **It never leaves mid-play.** The panel looks for a better game at the end of a quarter, period or half inning, at halftime, or when the game ends, and no sooner than **Switch every** minutes after the last look. It stays on the game it shows unless another one is clearly better.
- **Upset alert.** When the better-ranked college team trails (a ranked team behind an unranked one, or behind a lower-ranked one) in the fourth quarter, the last ten minutes of a basketball game, or overtime, the panel flashes **UPSET ALERT** in red over that game, once a game. Those games also jump to the front of the line.

Turn **Pick the best game** off for the random game and the move every **Switch every** minutes, as before.

A late football game, like a 10:30 PM Eastern college kickoff on the West Coast, stays in Live games until it ends, as late games in the other sports already did. Before, a football game still on at midnight Eastern dropped out of Live games.

A soccer match has no stops in play, so it counts as at a break from the 45th minute to halftime and from the 90th minute on. Before, only halftime counted, and a match could hold the panel for a whole half. The panel also looks once per break: a long break, like a hockey intermission, gets one look instead of one every **Switch every** minutes.

### Favorites take over

Turn on **Favorites take over** under **Switch every** and a game one of your [favorites](/using/favorites/) is playing gets the panel while it's on, whatever else is live. Live games carries on as usual the rest of the time. Your favorites can be from any sport, like the Cowboys, Mavericks, Stars and Rangers on one list.

- The panel moves to a favorite's game when it starts, even in the middle of another game, and stays there until it ends.
- Your favorite is the panel's own team, on the left, and its scores always get a splash, the same as in Favorite teams. The other side's follow **Opponent scores too**.
- **Two favorites at once** shows up under the switch once it's on, and it's the same setting as **Two games at once** in Favorite teams. **Stick with the higher team** stays on the favorite higher in your list, and moves to a higher one when its game starts. **Alternate** switches between your favorites' games every **Switch every** minutes.
- Only favorites in the leagues Live games follows count. On automatic leagues, that's all of them.

The bar under the board says **Locked on** and the team's name while a favorite has the panel. It's off until you turn it on.

With **Stick with the higher team**, the panel also checks every **Switch every** minutes (at least every 5) for a higher favorite whose game started without a set time, like the second game of a doubleheader, so it doesn't wait for the lower favorite's game to end. With **Alternate**, the switch between your favorites' games comes only every **Switch every** minutes, not also when another favorite's game is about to start.

### Choosing the leagues

The league buttons under the mode buttons pick the leagues. Until you pick any, Live games follows the leagues of your favorites (NFL when you have none), and the page says so. Tap a league to choose by hand; **Back to automatic** goes back to following your favorites. At least one league stays on.

### When nothing is live

What the panel shows depends on whether you have favorites:

- **With favorites**, it shows your first favorite's card, as in [How a team's game is shown](#how-a-teams-game-is-shown), with a line like "No live NFL games, showing DAL" at the front of the ticker. It keeps looking, and switches to a live game when one starts.
- **With no favorites**, it rotates through the games still to come today under **Later today**, each with both logos, the start time and the TV channel, then the latest final scores for every league it follows, under headers like "Yesterday's NHL scores". When no game is left today, a "Next" card names the next matchup and its time, like "Next: BOS at NYR Thu 6:00 PM". A game drops off the Later today list once it starts.

The panel waits for the next kickoff before it checks again, instead of checking every few minutes, and never waits more than 3 hours.

A check that fails, on a slow or dropped connection, is tried again within a few minutes instead of waiting for that kickoff.

When there has been no game in the last week and none is coming, the board shows a gray clock and the ticker says "No live games right now".

A change to the leagues, or to the favorites they follow, takes effect right away, instead of leaving the old leagues' final scores up until the next check.

## Remote buttons and modes

Buttons 1 to 4 on a WizMote remote pick your first four favorites. In Favorite teams, a button jumps to that favorite. In Live games, a button switches the panel to Favorite teams on that favorite. See [WizMote remote](/using/remote/).
