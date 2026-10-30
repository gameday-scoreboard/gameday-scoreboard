---
title: "The device page"
description: "The web page your panel serves: a live copy of the board, the team chooser, every setting and firmware updates."
sidebar:
  order: 2
---

Every panel serves its own web page. Open it in a browser on any phone or computer on the same Wi-Fi, at the `gameday-xxxxxx.local` name the panel shows when it starts (your panel's six characters, like `gameday-2f6a70.local`), or at its IP address if your phone won't open `.local` names. Hold the button on the controller for about 1.5 seconds and the panel shows both again. See [Wi-Fi](/using/wifi/) for more on the address.

The page is plain HTML built into the firmware, so there is nothing to install and no account. It works with no internet connection except for two things your browser reads from ESPN itself: the team logos and the **On now** list. The Game Day app shows the same settings, so a change in one shows up in the other.

A change is saved on the panel the moment you make it, from the page, the app, Home Assistant or the remote, so you can unplug the panel right after and it comes back with the change.

## The live board

The top of the page mirrors the panel: both logos, the score, the game clock, down and distance or the count and outs, timeout pips, the ticker line and the last play. A label above it says **UPCOMING**, **LIVE**, **FINAL** or **NO GAME**, and bowl and playoff games show their name, like "Rose Bowl", under the score. When the panel splashes a score, the page shows the same words for a few seconds.

When nothing is on, the board says why, like **No upcoming game for this team**, **No live games right now** or **None of your favorites has a game coming up**.

Two warnings can appear on the board:

- **data stale**: the panel has missed three reads from ESPN in a row, so the score may be old.
- **Panel not answering since** and a time: the page can't reach the panel. The board dims until it answers again.

The dot at the top right says **live** while the page is connected to the panel and **offline** when it isn't.

When the board is empty, the page shows the panel's own reason, the same words the panel shows, like **Can't reach ESPN** or **Getting ready**. On a Wi-Fi that wants you to sign in on a web page, the board says **Wi-Fi needs a sign-in** instead of **data stale**. See [Panel messages](/help/panel-messages/).

## Up next

In Favorite teams, a list under the board has one row per favorite: the live score, the final, or the next kickoff and channel. The row the panel is showing now is highlighted. **No game scheduled** means ESPN lists no game for that team.

A favorite the panel hasn't read from ESPN yet says **Not checked yet**, so it no longer looks like a team with no game.

## Choosing teams

Tap **Add a favorite** in the Favorites card to open the team chooser. It has a search box and a tab per league: **NFL**, **NCAAF** (college football), **MLB**, **MLS**, **EPL** (Premier League), **NBA**, **WNBA**, **NCAAM** (men's college basketball) and **NHL**. Search looks through every league at once. The chooser stays open so you can add several: a tap adds a team to the end of your list, a tap on a checked team takes it out, and **Done** closes it. See [Favorite teams](/using/favorites/).

The first tab, **On now**, lists today's games in every league, under **Live now**, **Later today**, **Final** and **Postponed or canceled**. Tap a side to add that team. The chooser opens on **On now** when a game is live, and on your first favorite's league otherwise.

:::note[Coming in the next release]
On **On now**, a search that matches none of today's games lists the teams it matches under a **Teams** heading instead, with their league, so a team that isn't playing today still turns up.
:::

The chooser is for favorites only.

## Settings

Settings are grouped in cards. In the order the page shows them:

| Setting | What it does |
| --- | --- |
| **Favorite teams**, **Live games** | What the panel follows. See [Show: Favorite teams, Live games](/using/modes/). |
| Leagues (Live games) | The leagues Live games picks from. Until you pick, it follows the leagues of your favorites. **Back to automatic** goes back to that. |
| **Switch every** (Live games) | Moves to another game in progress after this many minutes, 1 to 30. A new panel starts at 1. |
| **Take over** (Favorite teams) | How many minutes before kickoff a favorite's game takes the panel, 5 to 120. Starts at 15. |
| **After the final** (Favorite teams) | How long a favorite's final stays up before the panel moves on, 30 seconds to 60 minutes. Starts at 30 seconds. |
| **Two games at once** (Favorite teams) | **Stick with the higher team** in your list, or **Alternate** between them every set number of minutes. |
| Ticker: **Down and distance** | Football: the down, yards to go and where the ball is. |
| Ticker: **Count and at-bat** | Baseball: the count, outs, pitcher and batter. |
| Ticker: **Match stats** | Soccer: goals, cards, possession and shots. |
| Ticker: **Last play** | ESPN's latest play in every sport. In soccer, the latest goal or card. |
| Ticker: **Odds, TV and win chance** | The line, over/under and TV channel before the start, when ESPN has them, and in football who is favored to win during the game. |
| **Opponent scores too** | Splashes for the other side's scores. Your team's always show. See [Celebrations](#celebrations). |
| **Panel on** | Turns the panel's lights on or off. |
| **Brightness** | Panel brightness, 1 to 255. |
| **Ticker speed** | How fast the ticker scrolls, 1 to 10. |
| **Showing** | The Scoreboard page or the Clock page. |
| **Night schedule** | Off to start. Dims the panel, or turns the screen off, between **Starts at** and **Ends at**. **Screen at night** On keeps it lit at **Night brightness**, Off turns it dark. A live game of your team or a favorite keeps it at normal brightness. Coming in the next release: the remote, a team pick or **Panel on** turns it off for the rest of the night. See [Night schedule](/using/brightness-and-clock/#night-schedule). |
| **Game-day wake** | Coming in the next release. Off to start. Turns a panel you switched off back on before a game of your team or a favorite, **Turns on** 5 minutes to 2 hours before (15 to start). See [Game-day wake and after the game](/using/brightness-and-clock/#game-day-wake-and-after-the-game). |
| **After the game** | Coming in the next release. What the panel does once the final has been up a while: **Stay on the scoreboard** (to start), **Turn off**, or **Show the clock** until your next game is close. |
| **Only today's games** | Between games, Favorite teams skips favorites that aren't playing today. |
| Favorites list | Up to 16 teams in priority order. Move a team up or down, or remove it. The first four are buttons 1 to 4 on a WizMote. |
| **Discovery**, **Unpair remote** | Pairs and unpairs a WizMote remote. See [WizMote remote](/using/remote/). |
| Matrix | Click the picture that matches your panel: **One 64x64 panel**, or **One 128x64 panel** (or two 64x64 side by side). The panel restarts with the new layout. See [Panel sizes and layouts](/using/panel-sizes/). |
| Timezone | Used for kickoff times and the clock. The page sets it from your browser unless you choose one by hand. |
| **Show how to connect when the panel starts** | After it joins Wi-Fi, the panel shows a QR code for the Game Day app, then its address, for about 20 seconds. Turn it off to go straight to the scoreboard. |
| **Install updates automatically** | Off unless you turn it on. Installs a new release between 3 and 5 AM, never during a game the panel follows. See [Firmware updates](/using/updates/). |
| Wi-Fi networks | The networks the panel remembers, in the order it tries them, with **Forget** for each. See [Wi-Fi](/using/wifi/). |

The Ticker card has a **Pre-game season leaders** switch, on to start: before the game, each team's season leader, the passing leader with yards and touchdowns in football, home runs in baseball, points per game in basketball and points in hockey. It shows on a panel that follows any of those sports. See [The ticker](/using/ticker/).

The Favorites card has two more switches: **Jump to the action** (off to start) moves the panel to another favorite's game at its big moment, then back, and **Score banner** (on to start) shows another favorite's score along the bottom for five seconds. See [Favorite teams](/using/favorites/#when-a-game-is-on).

Live games has a **Pick the best game** switch under **Switch every**, on unless you turn it off. On, the panel follows the closest game with the least time left and moves only at a break in play; off, it picks a random game every **Switch every** minutes. See [Live games](/using/modes/#live-games).

A **Playlists** card, after Favorites, groups your favorites into named playlists, and **Playing** under **Favorite teams** picks the one the panel plays. Coming in the next release: **Playing** also has a row for each sport in your favorites, and the **One per sport** button is gone. See [Playlists](/using/favorites/#playlists).

Live games also has a **Favorites take over** switch, off to start. On, a game one of your favorites is playing gets the panel until it ends, and **Two favorites at once** picks between two of them. See [Favorites take over](/using/modes/#favorites-take-over).

The Panel card starts with a **Nickname** box. Give each panel a name, like "Living room" or "Garage", so you can tell them apart. The page's header and the browser tab show it, and so does the panel list in the iPhone app. Up to 32 characters (fewer with accents); emoji are left out. Clear the box to go back to the panel's own name. The nickname doesn't change the panel's network name, its address, its Wi-Fi hotspot or its name in Home Assistant.

A **Message** card, after Celebrations, puts your own words on the panel: a **Banner** over the board for a few seconds, and a **Message card** of text or a list from a web address that takes turns with the scoreboard between games, with **Show it** for how often. See [Your own messages](/using/messages/).

:::note[Coming in the next release]
**Showing** gets a third page, **Effects**: one of 52 moving patterns fills the panel in place of the scoreboard, and **Effect**, **Palette**, **Speed** and **Intensity** controls appear under it while it is the page showing. The board comes back when you pick the Scoreboard again, or when the panel has something to tell you. See [Effects](/using/effects/).
:::

Only the ticker switches for sports your panel follows are shown. Hockey and basketball have no game situation line, so they have no switch of their own.

### Between games

The **Between games** card has **Countdown to the next game**, on unless you turn it off, and **Your own countdown**: a **Name** and a **Day** to count down to, with **Clear the countdown** to remove it. See [Countdowns](/using/countdowns/).

:::note[Coming in the next release]
**Game leaders**, on unless you turn it off, shows each team's top players at halftime and the final: passing, rushing and receiving in football, points and rebounds in basketball, goals and assists in hockey, the starting pitcher and top hitter in baseball. The card takes a turn with the board and the line score, 10 seconds each: once at halftime, and at the final for as long as the final is up. The panel reads the box score once at halftime and once at the final, never during play. See the sport's page for what it shows.

**College Top 25**, on unless you turn it off, shows the AP poll between games while you follow a college football or basketball team. See [College Top 25](/using/top-25/).

**News headlines**, off to start, puts ESPN headlines for your teams or leagues on the panel between games. See [News headlines](/using/news/).

**Weather** has a **Town or ZIP code** box with **Look up**, the place the panel found, and when the forecast was last read and what it said. Its switch is the **Weather** row in the list below, which setting a town turns on. See [Weather](/using/weather/).

A **Pictures** card, after Between games, keeps up to 4 of your own pictures or GIFs for the panel to show between games: **Add a picture**, and **Delete** next to each one. See [Pictures](/using/pictures/).

The Between games card becomes one list with a switch for each card the panel can show between games, in this order:

| Card | What it is |
| --- | --- |
| **Next game** | The next game's board. Off, the panel shows the [clock](/using/brightness-and-clock/#the-off-season-clock) between games instead, with the next game under the time. |
| **Clock** | The clock between seasons, when no game is coming up for weeks. Off, the next game's board stays up instead; in Live games with nothing in the last week and no favorite to show, the board says there are no live games. |
| **Countdown to the next game** | The same switch as before. |
| **Your own countdown** | Your countdown's turn at the end of every minute. Set its **Name** and **Day** under the list. |
| **Message card** | The [message card](/using/messages/)'s turns. Its text or list, and **Show it**, stay in the Message card. |
| **Pictures** | Your [pictures](/using/pictures/)' turns. They stay saved while it's off. |
| **Game leaders**, **College Top 25**, **News headlines** | The same switches as before. |
| **Weather** | The weather card's switch, which moves here from its block. Its town stays under the list. |

Every card is on to start except News headlines and Weather (on once you set a town), so a panel does what it did before the update. A card with nothing to show yet is grey, with what it needs under its name: a day for your own countdown, text or a list for the message card, a picture for Pictures, a college football or basketball team for College Top 25, a town for Weather. You can still turn it off before then. The list is part of the [settings backup](/using/backup/).
:::

### Celebrations

The panel fills the screen with a splash when someone scores, in the scoring team's color. Your team's always show. **Opponent scores too** turns the other side's on or off in football, baseball, hockey and soccer. Basketball only celebrates the win, so the switch makes no difference there.

### Timezone

The Time line in the Setup card names the panel's timezone. While it follows your browser, opening the page from a browser in another zone moves the panel to that zone. Tap **choose manually** to pick one from the list instead; the page then leaves it alone until you tap **follow this browser**. The list covers the US zones (Eastern, Central, Mountain, Arizona, Pacific, Alaska and Hawaii), Canada Atlantic and Newfoundland, Mexico City, UK and Ireland, Central Europe, Australia Eastern and UTC. A new panel starts on US Central.

### Backup

The Setup card has a **Backup** section: **Export settings** saves this panel's settings to a file, and **Import settings** loads a saved file back, onto this panel or another one. Wi-Fi networks and the WizMote pairing aren't in the file. See [Back up and restore settings](/using/backup/).

## The Device card

The Device card has the buttons and the details about the panel itself.

- **Refresh scores** reads your team from ESPN again right away. Use it when the board looks wrong, for example after a game moved.
- **Check for updates** asks the update server now. See [Firmware updates](/using/updates/).
- **Reboot** restarts the panel. The page says it's restarting and reconnects when the panel is back.
- **Reset Wi-Fi** and **Factory reset** restart the panel into setup, so the page can't follow it. See [The boot button and resets](/using/buttons-and-resets/).

Under them: the **Firmware** box, the Wi-Fi networks list, the panel's **Address** and **Name**, its Wi-Fi **Signal**, free memory, and **ESPN data**, how much the panel has downloaded from ESPN since it started, for anyone on a capped internet plan.

Under **Name**, **FCC information** has a **Show on panel** button. The panel shows its model, FCC ID and FCC statement on its own screen for about a minute, then goes back to the board. A panel that was switched off turns on for it and stays on. One that is dimmed or dark for the night lights up for it, then goes back to the night setting.

A **Diagnostics** section in the Device card says why the panel last restarted and when (power on, restart, update, crash, froze or a power dip), any crash, freeze or power dip since it was plugged in, free memory, the chip temperature, power dips, the last read from ESPN and the firmware version and build. Under **Signal** it draws the last day of Wi-Fi signal, says when the signal is weak, and counts **Wi-Fi drops** with the cause of the last one.

A notice under the board says when the panel is holding its brightness down: **Too warm, dimming**, or **Check the power supply** with a **Clear** button after two restarts in a row from a power dip. See [Heat and power protection](/using/brightness-and-clock/#heat-and-power-protection).

:::note[Coming in the next release]
The notice under the board reads **Running warm** or **Power dipped**, the same words as the panel's corner, with one sentence on what to do and no button. The panel no longer dims itself for either, so there is nothing to clear.
:::

:::note[Coming in the next release]
While a new release waits, **Diagnostics** also shows **Update ready on the ticker**: when the ticker last started saying so. See [Update ready on the ticker](/using/updates/#update-ready-on-the-ticker).
:::

The Device card can also show a **Follow beta** switch. Beta versions are test builds for test panels and can have bugs. Leave it off.
