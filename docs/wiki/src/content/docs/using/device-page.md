---
title: "The device page"
description: "The web page your panel serves: a live copy of the board, the team chooser, every setting and firmware updates."
sidebar:
  order: 2
---

Every panel serves its own web page. Open it in a browser on any phone or computer on the same Wi-Fi, at the `gameday-xxxxxx.local` name the panel shows when it starts (your panel's six characters, like `gameday-2f6a70.local`), or at its IP address if your phone won't open `.local` names. Hold the button on the controller for about 1.5 seconds and the panel shows both again. See [Wi-Fi](/using/wifi/) for more on the address.

The page is plain HTML built into the firmware, so there is nothing to install and no account. It works with no internet connection except for two things your browser reads from ESPN itself: the team logos and the **On now** list. The Game Day app shows the same settings, so a change in one shows up in the other.

## The live board

The top of the page mirrors the panel: both logos, the score, the game clock, down and distance or the count and outs, timeout pips, the ticker line and the last play. A label above it says **UPCOMING**, **LIVE**, **FINAL** or **NO GAME**, and bowl and playoff games show their name, like "Rose Bowl", under the score. When the panel splashes a score, the page shows the same words for a few seconds.

When nothing is on, the board says why, like **No upcoming game for this team**, **No live games right now** or **None of your favorites has a game coming up**.

Two warnings can appear on the board:

- **data stale**: the panel has missed three reads from ESPN in a row, so the score may be old.
- **Panel not answering since** and a time: the page can't reach the panel. The board dims until it answers again.

The dot at the top right says **live** while the page is connected to the panel and **offline** when it isn't.

:::note[Coming in the next release]
When the board is empty, the page shows the panel's own reason, the same words the panel shows, like **Can't reach ESPN** or **Getting ready**. On a Wi-Fi that wants you to sign in on a web page, the board says **Wi-Fi needs a sign-in** instead of **data stale**. See [Panel messages](/help/panel-messages/).
:::

## Up next

In My team, a list under the board shows the next three games after the one on the board, with the opponent, the day and time (in your browser's time) and the TV channel. It appears for football and soccer teams. MLB, NBA, WNBA, men's college basketball and NHL season schedules are too large for the panel to read, so those teams have no Up next list.

In Favorite teams, the list has one row per favorite: the live score, the final, or the next kickoff and channel. The row the panel is showing now is highlighted. **No game scheduled** means ESPN lists no game for that team.

:::note[Coming in the next release]
A favorite the panel hasn't read from ESPN yet says **Not checked yet**, so it no longer looks like a team with no game.
:::

## Choosing a team

Tap **Change team** to open the team chooser. It has a search box and a tab per league: **NFL**, **NCAAF** (college football), **MLB**, **MLS**, **EPL** (Premier League), **NBA**, **WNBA**, **NCAAM** (men's college basketball) and **NHL**. Tap a logo to follow that team. Search looks through every league at once.

The first tab, **On now**, lists today's games in every league, under **Live now**, **Later today**, **Final** and **Postponed or canceled**. Tap the side you want to follow. The chooser opens on **On now** when a game is live, and on your team's league otherwise.

The same chooser adds favorites: **Add a favorite** in the Favorites card opens it, and a tap adds the team to the end of your list. See [Favorite teams](/using/favorites/).

## Settings

Settings are grouped in cards. In the order the page shows them:

| Setting | What it does |
| --- | --- |
| **My team**, **Live games**, **Favorite teams** | What the panel follows. See [Show: My team, Favorite teams, Live games](/using/modes/). |
| **Change team** | Opens the team chooser. Shown in My team. |
| Leagues (Live games) | The leagues Live games picks from. Until you pick, it follows the leagues of your team and favorites. **Back to automatic** goes back to that. |
| **Switch every** (Live games) | Moves to another game in progress after this many minutes, 1 to 30. A new panel starts at 1. |
| **Take over** (Favorite teams) | How many minutes before kickoff a favorite's game takes the panel, 5 to 120. Starts at 15. |
| **After the final** (Favorite teams) | How long a favorite's final stays up before the playlist moves on, 30 seconds to 60 minutes. Starts at 30 seconds. |
| **Two games at once** (Favorite teams) | **Stick with the higher team** in your list, or **Alternate** between them every set number of minutes. |
| Ticker: **Down and distance** | Football: the down, yards to go and where the ball is. |
| Ticker: **Count and at-bat** | Baseball: the count, outs, pitcher and batter. |
| Ticker: **Match stats** | Soccer: goals, cards, possession and shots. |
| Ticker: **Last play** | ESPN's latest play in every sport. In soccer, the latest goal or card. |
| Ticker: **Pre-game odds and TV** | The line, over/under and TV channel before the start, when ESPN has them. |
| **Opponent scores too** | Splashes for the other side's scores. Your team's always show. See [Celebrations](#celebrations). |
| **Panel on** | Turns the panel's lights on or off. |
| **Brightness** | Panel brightness, 1 to 255. |
| **Ticker speed** | How fast the ticker scrolls, 1 to 10. |
| **Showing** | The Scoreboard page or the Clock page. |
| **Only today's games** | The Favorite teams playlist skips favorites that aren't playing today. |
| Favorites list | Up to 16 teams in priority order. Move a team up or down, or remove it. The first four are buttons 1 to 4 on a WizMote. |
| **Discovery**, **Unpair remote** | Pairs and unpairs a WizMote remote. See [WizMote remote](/using/remote/). |
| Matrix | Click the picture that matches your panel: **One 64x64 panel**, or **One 128x64 panel** (or two 64x64 side by side). The panel restarts with the new layout. See [Panel sizes and layouts](/using/panel-sizes/). |
| Timezone | Used for kickoff times and the clock. The page sets it from your browser unless you choose one by hand. |
| **Show how to connect when the panel starts** | After it joins Wi-Fi, the panel shows a QR code for the Game Day app, then its address, for about 20 seconds. Turn it off to go straight to the scoreboard. |
| **Install updates automatically** | Off unless you turn it on. Installs a new release between 3 and 5 AM, never during a game the panel follows. See [Firmware updates](/using/updates/). |
| Wi-Fi networks | The networks the panel remembers, in the order it tries them, with **Forget** for each. See [Wi-Fi](/using/wifi/). |

:::note[Coming in the next release]
A **Message** card, after Celebrations, puts your own words on the panel: a **Banner** over the board for a few seconds, and a **Message card** of text or a list from a web address that takes turns with the scoreboard between games, with **Show it** for how often. See [Your own messages](/using/messages/).
:::

Only the ticker switches for sports your panel follows are shown. Hockey and basketball have no game situation line, so they have no switch of their own.

### Celebrations

The panel fills the screen with a splash when someone scores, in the scoring team's color. Your team's always show. **Opponent scores too** turns the other side's on or off in football, baseball, hockey and soccer. Basketball only celebrates the win, so the switch makes no difference there.

### Timezone

The Time line in the Setup card names the panel's timezone. While it follows your browser, opening the page from a browser in another zone moves the panel to that zone. Tap **choose manually** to pick one from the list instead; the page then leaves it alone until you tap **follow this browser**. The list covers the US zones (Eastern, Central, Mountain, Arizona, Pacific, Alaska and Hawaii), Canada Atlantic and Newfoundland, Mexico City, UK and Ireland, Central Europe, Australia Eastern and UTC. A new panel starts on US Central.

## The Device card

The Device card has the buttons and the details about the panel itself.

- **Refresh scores** reads your team from ESPN again right away. Use it when the board looks wrong, for example after a game moved.
- **Check for updates** asks the update server now. See [Firmware updates](/using/updates/).
- **Reboot** restarts the panel. The page says it's restarting and reconnects when the panel is back.
- **Reset Wi-Fi** and **Factory reset** restart the panel into setup, so the page can't follow it. See [The boot button and resets](/using/buttons-and-resets/).

Under them: the **Firmware** box, the Wi-Fi networks list, the panel's **Address** and **Name**, its Wi-Fi **Signal**, free memory, and **ESPN data**, how much the panel has downloaded from ESPN since it started, for anyone on a capped internet plan.

:::note[Coming in the next release]
A **Diagnostics** section in the Device card says why the panel last restarted and when (power on, restart, update, crash, froze or a power dip), any crash, freeze or power dip since it was plugged in, free memory, the chip temperature, power dips, the last read from ESPN and the firmware version and build. Under **Signal** it draws the last day of Wi-Fi signal, says when the signal is weak, and counts **Wi-Fi drops** with the cause of the last one.

The Device card can also show a **Follow beta** switch. Beta versions are test builds for test panels and can have bugs. Leave it off.
:::
