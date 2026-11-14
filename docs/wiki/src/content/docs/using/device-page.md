---
title: "The device page"
description: "The web page your panel serves: a live copy of the board, the team chooser, every setting and firmware updates, in five tabs."
sidebar:
  order: 2
---

Every panel serves its own web page. Open it in a browser on any phone or computer on the same Wi-Fi, at the `gameday-xxxxxx.local` name the panel shows when it starts (your panel's six characters, like `gameday-2f6a70.local`), or at its IP address if your phone won't open `.local` names. Hold the button on the controller for about 1.5 seconds and the panel shows both again. See [Wi-Fi](/using/wifi/) for more on the address.

The page is plain HTML built into the firmware, so there is nothing to install and no account. It works with no internet connection except for two things your browser reads from ESPN itself: the team logos and the **On now** list. The Game Day app shows the same settings, so a change in one shows up in the other.

A change is saved on the panel the moment you make it, from the page, the app, Home Assistant or the remote, so you can unplug the panel right after and it comes back with the change.

The page is split into five tabs: **Home**, **Teams**, **Display**, **Customize** and **Device**, described below. On a phone they sit in a bar at the bottom of the screen; on a computer they're under the panel's name at the top. The open tab is kept in the page's address, so a reload stays on it. Each setting has one short line on the page, and the full rules are here in the docs. The page also fits a phone's screen now, where iPhone Safari used to draw it as a zoomed-out desktop page.

## The header

The top of the page shows the panel's name, big, or its nickname if you gave it one. It shrinks into a slim bar as you scroll down. A red **Not answering** pill shows next to it only while the page can't reach the panel.

## Home

Home is the page as it opens: the board, the controls you reach for most, and what the panel is doing.

### The live board

The board mirrors the panel: both logos, the score, the game clock, down and distance or the count and outs, timeout pips, the ticker line and the last play. It glows in each team's color, the way the panel does, and stays black like the real panel in the light look too. A label above the score says **UPCOMING**, **LIVE**, **FINAL** or **NO GAME**, and bowl and playoff games show their name, like "Rose Bowl", under the score. When the panel splashes a score, the page shows the same words for a few seconds.

When nothing is on, the board says why, like **No upcoming game for this team**, **No live games right now** or **None of your favorites has a game coming up**.

Two warnings can appear on the board:

- **data stale**: the panel has missed three reads from ESPN in a row, so the score may be old.
- **Panel not answering since** and a time: the page can't reach the panel. The board dims until it answers again.

When the board is empty, the page shows the panel's own reason, the same words the panel shows, like **Can't reach ESPN** or **Getting ready**. On a Wi-Fi that wants you to sign in on a web page, the board says **Wi-Fi needs a sign-in** instead of **data stale**. See [Panel messages](/help/panel-messages/).

### Under the board

A strip along the bottom edge of the board has:

- the round power button, **Panel on**, which turns the panel's lights on or off
- **Brightness**, shown as a percent
- **Refresh scores**, which reads your teams from ESPN again right away. Use it when the board looks wrong, for example after a game moved.

Under it, **Showing** picks the page the panel shows: **Scoreboard**, **Clock** or **Effects**. The Game Day app and Home Assistant have the same controls. See [Brightness and the clock](/using/brightness-and-clock/).

**Effects** is new: one of 52 moving patterns fills the panel in place of the scoreboard, and **Effect**, **Palette**, **Speed** and **Intensity** controls appear under **Showing** while it is the page showing. The board comes back when you pick the Scoreboard again, or when the panel has something to tell you. See [Effects](/using/effects/).

A line under the board says when the panel is holding its brightness down: **Too warm, dimming**, or **Check the power supply** with a **Clear** button after two restarts in a row from a power dip. See [Heat and power protection](/using/brightness-and-clock/#heat-and-power-protection).

That line reads **Running warm** or **Power dipped**, the same words as the panel's corner, with one sentence on what to do and no button. The panel no longer dims itself for either, so there is nothing to clear.

During the [night schedule](/using/brightness-and-clock/#night-schedule)'s hours, a line says **Night mode until** and the time it ends, with what the panel is doing now: dimmed, off, or at normal brightness for a live game.

### Up next

In Favorite teams, the **Up next** list has one row per favorite: the live score, the final, or the next kickoff and channel. The row the panel is showing now is highlighted. **No game scheduled** means ESPN lists no game for that team, and **Not checked yet** means the panel hasn't read that team from ESPN yet.

### Favorite teams or Live games

Two cards pick what the panel follows, each with one line on what it does: **Favorite teams** (only the teams you pick) and **Live games** (games on right now, in the leagues you pick). See [Show: Favorite teams, Live games](/using/modes/).

The line under them says why the panel shows what it shows, like **None of your teams are playing. Showing their next games.** or **Dallas Cowboys are on**. With no favorites it offers **Add a team**. In Favorite teams with [playlists](/using/favorites/#playlists), **Playing** there picks the playlist the panel plays.

In Live games, a row of league chips picks the leagues Live games chooses from. Until you pick, it follows the leagues of your favorites; **Back to automatic** goes back to that.

## Teams

### Favorites

Your favorites, up to 16 teams in priority order. Move a team up or down, or remove it. Tap **Add a favorite** to open the team chooser (see [Choosing teams](#choosing-teams)). While a WizMote is paired, the first four teams say **remote button 1** to **remote button 4**. See [Favorite teams](/using/favorites/).

Three switches sit above the list:

- **Only today's games**: between games, Favorite teams skips favorites that aren't playing today.
- **Jump to the action** (off to start): moves the panel to another favorite's game at its big moment, then back. See [When a game is on](/using/favorites/#when-a-game-is-on).
- **Score banner** (on to start): shows another favorite's score along the bottom for five seconds.

### Playlists

Groups your favorites into named playlists you can switch between. **New playlist** makes one, and **Playing** on Home picks the one the panel plays. See [Playlists](/using/favorites/#playlists).

**Playing** also has a row for each sport in your favorites, and the **One per sport** button is gone.

### When favorites play

| Setting | What it does |
| --- | --- |
| **Take over** | How many minutes before kickoff a favorite's game takes the panel, 5 to 120. Starts at 15. |
| **After the final** | How long a favorite's final stays up before the panel moves on, 30 seconds to 60 minutes. Starts at 30 seconds. |
| **Two games at once** | **Stick with the higher team** in your list, or **Alternate** between them every set number of minutes. |

### Live games

| Setting | What it does |
| --- | --- |
| **Switch every** | Moves to another game in progress after this many minutes, 1 to 30. A new panel starts at 1. |
| **Pick the best game** | Off to start. Off, the panel picks a random live game and moves to another every **Switch every** minutes; on, it follows the closest game with the least time left and never leaves it mid-play, moving only at a break. See [Live games](/using/modes/#live-games). |
| **Favorites take over** | Off to start. On, a game one of your favorites is playing gets the panel until it ends, and **Two favorites at once** picks between two of them. See [Favorites take over](/using/modes/#favorites-take-over). |

### Choosing teams

**Add a favorite** opens the team chooser. It has a search box and a tab per league: **NFL**, **NCAAF** (college football), **MLB**, **MLS**, **EPL** (Premier League), **NBA**, **WNBA**, **NCAAM** (men's college basketball) and **NHL**. Search looks through every league at once. The chooser stays open so you can add several: a tap adds a team to the end of your list, a tap on a checked team takes it out, and **Done** closes it.

The first tab, **On now**, lists today's games in every league, under **Live now**, **Later today**, **Final** and **Postponed or canceled**. Tap a side to add that team. The chooser opens on **On now** when a game is live, and on your first favorite's league otherwise.

Each league tab starts with that league's games today, under the same headings, and lists all of its teams under a **Teams** heading after them. Tap a side to add that team. **Final** and **Postponed or canceled** start folded, with a count; tap the heading to open one. A league with no games today shows just its teams.

On **On now**, a search that matches none of today's games lists the teams it matches under a **Teams** heading instead, with their league, so a team that isn't playing today still turns up.

The chooser is for favorites only.

## Display

### Screen

| Setting | What it does |
| --- | --- |
| **Ticker speed** | How fast the ticker scrolls, 1 to 10. See [The ticker](/using/ticker/). |
| **Night schedule** | Off to start. Dims the panel, or turns the screen off, between **Starts at** and **Ends at**. **Screen at night** On keeps it lit at **Night brightness**, Off turns it dark. A live game of your team or a favorite keeps it at normal brightness. The remote, a team pick or **Panel on** turns it off for the rest of the night. See [Night schedule](/using/brightness-and-clock/#night-schedule). |
| **Game-day wake** | Off to start. Turns a panel you switched off back on before a game of your team or a favorite, **Turns on** 5 minutes to 2 hours before (15 to start). See [Game-day wake and after the game](/using/brightness-and-clock/#game-day-wake-and-after-the-game). |
| **After the game** | What the panel does once the final has been up a while: **Stay on the scoreboard** (to start), **Turn off**, or **Show the clock** until your next game is close. |

### Ticker

What scrolls along the bottom of the panel during a game. All of them start on. See [The ticker](/using/ticker/).

| Setting | What it does |
| --- | --- |
| **Down and distance** | Football: the down, yards to go and where the ball is. |
| **Count and at-bat** | Baseball: the count, outs, pitcher and batter. |
| **Match stats** | Soccer: goals, cards, possession and shots. |
| **Last play** | ESPN's latest play in every sport. In soccer, the latest goal or card. |
| **Odds, TV and win chance** | The line, over/under and TV channel before the start, when ESPN has them, and in football who is favored to win during the game. |
| **Pre-game season leaders** | Before the game, each team's season leader: the passing leader with yards and touchdowns in football, home runs in baseball, points per game in basketball and points in hockey. |

Only the switches for sports your panel follows are shown. Hockey and basketball have no game situation line, so they have no switch of their own.

### Live pictures

Drawn in the row under the clock during a game, on a 128-wide board (not on a single 64x64 panel). Both start on. The card is hidden when the panel only follows sports with nothing to turn off here (hockey, baseball, soccer).

| Setting | What it does |
| --- | --- |
| **Field strip** | Football: the field across the row, with the ball, the line to gain and the direction of play. See [Football](/using/football/#during-the-game). |
| **Win chance** | Football and basketball: the row under the clock takes turns with the team ESPN favors in words, like "ND 84% to win", when the game comes up, after a touchdown, field goal or turnover, and when the number moves 10 points or more (basketball: when it comes up and on a move). This switch turns that line on or off. |

### Celebrations

The panel fills the screen with a splash when someone scores, in the scoring team's color. On a white or pale team color the words are dark so they stay readable. Your team's always show. **Opponent scores too** turns the other side's on or off in football, baseball, hockey and soccer. Basketball only celebrates the win, so the switch makes no difference there.

A **Celebrations** choice sets how big the show is:

- **Full** (the default): fireworks over goalposts for a touchdown, a light sweep for a field goal or a run, the ball arcing out of the park for a home run, a rippling net for a soccer goal, the spinning red goal lamp for a hockey goal, a three dropping through the hoop in basketball, and when your team wins, a victory screen with the final score under its name and confetti in its colors for 10 seconds.
- **Subtle**: a quick flash in the team's color with white words, for every score and the win.
- **Off**: no full-screen splash at all. The scores on the board still change, and Home Assistant still gets its events.

**Big plays**, on by default, gives some plays a pixel-art scene of their own on **Full**: a basketball dunk or alley-oop (see [Basketball](/using/basketball/)), a hockey hat trick (see [Hockey](/using/hockey/#big-plays)), a baseball grand slam or walk-off (see [Baseball](/using/baseball/#big-plays)), a soccer brace, hat trick, penalty or late go-ahead goal (see [Soccer](/using/soccer/)), and your football defense's interceptions, pick sixes, fumble recoveries and sacks (see [Football](/using/football/#big-plays)). Off, every score and the win keep their show; only the big plays go.

The other side's scores stay quiet in every setting: a dim flash with white words, over in 2 seconds, and only with **Opponent scores too** on. In the Live games mode nobody is "the other side": the first team's scores always get the full show in its colors, and with **Opponent scores too** on the second team's do too.

### Between games

What the panel shows when no game is on. **Countdown to the next game**, on unless you turn it off, counts down to your next game, like 2d 14h. See [Countdowns](/using/countdowns/).

**Between games** lists the cards that are showing, in the order they take turns, and **Add a card** lists the rest. Tap a card to turn it on or off, or to open its settings. The cards, in this order:

| Card | What it is |
| --- | --- |
| **Next game** | The next game's board. Off, the panel shows the [clock](/using/brightness-and-clock/#the-off-season-clock) between games instead, with the next game under the time. |
| **Clock** | The clock between seasons, when no game is coming up for weeks. Off, the next game's board stays up instead; in Live games with nothing in the last week and no favorite to show, the board says there are no live games. |
| **Countdown to the next game** | The countdown to your next game. |
| **Date countdown** | Your own countdown's turn at the end of every minute. Set it on the Customize tab. |
| **Message card** | The [message card](/using/messages/)'s turns. Its text or list, and **Show it**, are on the Customize tab. |
| **Pictures** | Your [pictures](/using/pictures/)' turns. They stay saved while it's off. |
| **Game leaders** | Each team's top players at halftime and the final: passing, rushing and receiving in football, points and rebounds in basketball, goals and assists in hockey, the starting pitcher and top hitter in baseball. The card takes a turn with the board and the line score, 10 seconds each: once at halftime, and at the final for as long as the final is up. The panel reads the box score once at halftime and once at the final, never during play. |
| **College Top 25** | The AP poll between games while you follow a college football or basketball team. See [College Top 25](/using/top-25/). |
| **News headlines** | ESPN headlines for your teams or leagues. See [News headlines](/using/news/). |
| **Schedule** | A favorite's next three games, for teams in every league but MLB. See [Schedule and season recap](/using/schedule/). |
| **Season recap** | A favorite's record and playoff result between seasons, for teams in every league but MLB. |
| **Weather** | The forecast for your town. See [Weather](/using/weather/). |

Every card is on to start except News headlines and Weather (on once you set a town), so a panel does what it did before the update. A card with nothing to show yet says what it needs under its name: a day for the date countdown, text or a list for the message card, a picture for Pictures, a college football or basketball team for College Top 25, a team outside MLB in Favorite teams for Schedule and Season recap, a town for Weather. You can still turn it off before then. The list is part of the [settings backup](/using/backup/).

The **Weather** card holds a **Town or ZIP code** box with **Look up**, the place the panel found, and when the forecast was last read and what it said. Setting a town turns the card on. **Degrees** picks °F, °C or Auto for the card and the kickoff forecast.

## Customize

Your own words, pictures and dates on the panel.

- **Banner**: your words over whatever the panel shows, for **Seconds on screen**. **Show** puts it up and **Take down** takes it down early. See [Your own messages](/using/messages/).
- **Message card**: your own text, or a list from a web address, that takes turns with the scoreboard between games, with **Show it** for how often. See [Your own messages](/using/messages/).
- **Pictures**: up to 4 of your own pictures or GIFs for the panel to show between games. **Add a picture**, and **Delete** next to each one. See [Pictures](/using/pictures/).
- **Date countdown**: a **Name** and a **Day** to count down to, like Christmas, with **Clear the countdown** to remove it. See [Countdowns](/using/countdowns/#your-own-countdown).

The Message card, Pictures and Date countdown each have a **Show between games** switch at the top, the same switch as that card's row in [Between games](#between-games).

## Device

### Panel

- **Nickname**: give each panel a name, like "Living room" or "Garage", so you can tell them apart. The page's header and the browser tab show it, and so does the panel list in the iPhone app. Up to 32 characters (fewer with accents); emoji are left out. Clear the box to go back to the panel's own name. The nickname doesn't change the panel's network name, its address, its Wi-Fi hotspot or its name in Home Assistant.
- **Address** and **Name**: the panel's IP address and its name on your network.
- **Check for updates** asks the update server now. The **Firmware** line under it says what it found, with **Install** when a new release is out. See [Firmware updates](/using/updates/).
- **Install updates automatically**: off unless you turn it on. Installs a new release between 3 and 5 AM, never during a game the panel follows.
- **Follow beta** can show here too. Beta versions are test builds for test panels and can have bugs. Leave it off.

### This page

**Look** picks how the page looks: **Phone** follows your phone or computer's light or dark setting, or pick **Light** or **Dark**. It's saved in that browser only, so each phone can have its own. The board stays black like the real panel either way.

### The folded sections

The next sections are folded shut, each with a one-line summary. Tap one to open it.

- **Setup**: things you set once.
  - **Time** names the panel's timezone, used for kickoff times and the clock. While it follows your browser, opening the page from a browser in another zone moves the panel to that zone. Tap **choose manually** to pick one from the list instead; the page then leaves it alone until you tap **follow this browser**. The list covers the US zones (Eastern, Central, Mountain, Arizona, Pacific, Alaska and Hawaii), Canada Atlantic and Newfoundland, Mexico City, UK and Ireland, Central Europe, Australia Eastern and UTC. A new panel starts on US Central.
  - **Show how to connect when the panel starts**: after it joins Wi-Fi, the panel shows a QR code for the Game Day app, then its address, for about 20 seconds. Turn it off to go straight to the scoreboard.
  - **Backup**: **Export settings** saves this panel's settings to a file, and **Import settings** loads a saved file back, onto this panel or another one. Wi-Fi networks and the WizMote pairing aren't in the file. See [Back up and restore settings](/using/backup/).
- **Game events**: tells Home Assistant when your team scores, with a heads-up before the start and an optional web address to post them to. See [Game events](/using/game-events/).
- **Wi-Fi networks**: the networks the panel remembers, in the order it tries them, with **Forget** for each. Forgetting the network the panel is on moves it to the next saved one right away. See [Wi-Fi](/using/wifi/).
- **Diagnostics**: see [below](#diagnostics).
- **Remote**: pairs and unpairs a WizMote with **Discovery** and **Unpair remote**. It also sets what buttons 1 to 4 show. See [WizMote remote](/using/remote/).
- **Matrix**: click the picture that matches your panel: **One 64x64 panel**, or **One 128x64 panel** (or two 64x64 side by side). The panel restarts with the new layout. See [Panel sizes and layouts](/using/panel-sizes/).

### Diagnostics

**Diagnostics** says why the panel last restarted and when (power on, restart, update, crash, froze or a power dip), any crash, freeze or power dip since it was plugged in, free memory, the chip temperature, power dips, the last read from ESPN and the firmware version and build. Under **Signal**, the panel's Wi-Fi signal, it draws the last day of signal, says when the signal is weak, and counts **Wi-Fi drops** with the cause of the last one. **ESPN data** is how much the panel has downloaded from ESPN since it started, for anyone on a capped internet plan.

While a new release waits, **Diagnostics** also shows **Update ready on the ticker**: when the ticker last started saying so. See [Update ready on the ticker](/using/updates/#update-ready-on-the-ticker).

### Reboot and resets

A red box near the bottom of the Device tab has the three buttons that restart the panel:

- **Reboot** restarts the panel and keeps everything. The page says it's restarting and reconnects when the panel is back.
- **Reset Wi-Fi** forgets the network and **Factory reset** erases every setting. Both restart the panel into setup, so the page can't follow it. See [The boot button and resets](/using/buttons-and-resets/).

### FCC information

Folded at the very bottom. **Show on panel** puts the panel's model, FCC ID and FCC statement on its own screen for about a minute, then it goes back to the board. A panel that was switched off turns on for it and stays on. One that is dimmed or dark for the night lights up for it, then goes back to the night setting. See [FCC information](/fcc/).

## Under every tab

The bottom of the page shows the firmware version, a link to the source code, where the scores come from, and the author and license: "© 2026 Brandon Harvey · GPLv3". GPLv3 links to the license notice.
