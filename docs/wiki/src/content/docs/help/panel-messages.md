---
title: "What the panel is telling you"
description: "Every message and icon the Game Day Scoreboard panel shows when it needs you or something is wrong, what each means and what to do."
sidebar:
  order: 3
---

The panel says what's going on in words on the screen, or in the ticker along the bottom. Find the words you see below. Your panel's own six characters, like `2f6a70`, and its own IP address stand in for the examples here.

## While it starts and sets up

These show on the start-up screen, under **GAME DAY**. On the 128x64 panel the QR code sits on the left half with the words beside it. On a single 64x64 panel the words and the QR code take turns.

| The panel says | What it means | What to do |
| --- | --- | --- |
| **Starting** | The panel has power and is starting up. | Wait a few seconds. |
| **SETUP / Game Day / app**, with a QR code | The panel has no Wi-Fi network yet. | Set it up in the Game Day app, or scan the QR code. See [Set up your panel](/get-started/setup/). |
| **Join WiFi / Game Day / 2f6a70** | The panel's own hotspot is up, named `Game Day 2f6a70`. | Join that network from any phone or laptop to set it up without the app. |
| **Connecting to** and a network name | The panel is joining a network it already knows. | Wait. |
| **JOINING** and a network name | The app sent the panel a network, and it's joining it. | Wait. If the name is wrong, pick again in the app. |
| A QR code with **Scan for Game Day app** | The panel joined your Wi-Fi. | Scan it to open the app, or wait for the address. |
| **Open gameday-2f6a70.local** | The panel's device page address. | Open it in a browser on the same Wi-Fi. |
| **Looking up scores** | The panel is loading its first scoreboard, at start-up or after you change the team. It shows for up to 10 seconds. | Wait. |

:::note[Coming in the next release]
| The panel says | What it means | What to do |
| --- | --- | --- |
| **Unplug it to rejoin saved Wi-Fi** | The panel is in setup after a Wi-Fi reset and still knows its old networks. | Set it up on a new network, or unplug it and plug it back in to go back to the old one. It goes back by itself after 15 minutes. |
:::

## When the Wi-Fi is the problem

| The panel says | What it means | What to do |
| --- | --- | --- |
| **Wi-Fi lost / open app**, with a QR code | The panel has been off its Wi-Fi for more than about 10 seconds. | If the router or internet is down, wait: the panel rejoins by itself. If you changed the router or the password, give the panel the new one in the app. See [New router or Wi-Fi password](/get-started/setup/#new-router-or-wi-fi-password). |

:::note[Coming in the next release]
| The panel says | What it means | What to do |
| --- | --- | --- |
| **Waiting for** and a network name, with **Just had a power cut? / Routers take a few min** | The panel just started and hasn't found its Wi-Fi yet. After a power cut the panel is up in seconds, and the router takes a few minutes. | Wait. The panel joins by itself once the router is back. If it still can't after 3 minutes, it says **Wi-Fi lost / open app**. |
| **Wi-Fi needs sign-in / use home or a hotspot**, with a QR code | The network wants a sign-in on a web page, like many bar, hotel and dorm networks. The panel can't sign in. | Set it up on a home network or a phone hotspot. The message goes by itself once scores come through. |
:::

## On the scoreboard

These show on the board or in the ticker.

| The panel says | What it means | What to do |
| --- | --- | --- |
| **Pick a team in the Game Day app or at** and an IP address | Nobody has picked a team or a mode yet. | Pick one in the app, or open that address in a browser. See [Pick your team](/get-started/pick-your-team/). |
| **Getting ready** | The panel is on Wi-Fi but hasn't set its clock yet. It can't read scores before it has. | Wait a few seconds. If it lasts, see [Troubleshooting](/help/troubleshooting/#the-panel-says-getting-ready-for-a-long-time). |
| **Can't reach ESPN** | The panel is on Wi-Fi but gets no answer from ESPN, where the scores come from. | Check the internet connection. The panel keeps trying. |
| **no update yet** at the end of the ticker | Three reads from ESPN in a row failed, and none has worked since the panel started. | Check the internet connection. |
| **no update for 12 min** at the end of the ticker | Three reads in a row failed. The score on the board is from that many minutes ago. | Check the internet connection. The board catches up once a read works. |
| **No upcoming game** | ESPN lists no next game for your team. | Nothing. Between seasons the panel shows a clock instead, below. |
| **Looking for a live game** | In Live games, the panel is checking what's on. With one league picked, it names it, like **Looking for a live NHL game**. | Wait. |
| **No live games right now** | In Live games, nothing is being played in the leagues you picked. | Nothing. The panel shows today's later games and recent final scores, and picks up the next game when it starts. |
| **No live NFL games, showing CHI** | In Live games with nothing on, the panel shows your own team until a game starts. | Nothing. |
| **Favorites: add teams in the app or on the page** | The panel is in Favorite teams with no favorites. | Add favorites, or switch to My team. See [Favorite teams](/using/favorites/). |
| **Later today** | A card before the games still to come today, each with both logos, the start time and the TV channel. | Nothing. |
| **Yesterday's NHL scores**, or another day and league | A card before that day's final scores. | Nothing. |
| **Next: BOS at NYR Thu 6:00 PM** | No game is left today. This is the next one. | Nothing. |
| A clock, with **Next:** and a date, or only the date | Your team is between seasons. The clock is in your team's colors, or gray with no team picked. Under it is the next game once it's on the schedule. | Nothing. See [Brightness and the clock](/using/brightness-and-clock/). |
| **Postponed**, **Canceled** or **Suspended** | The game ended without being played out. | Nothing. |
| **Sat TBD** | ESPN hasn't set the game's start time yet. | Nothing. The time shows once ESPN has it. |

A panel with no team picked also brings the app's QR code back over the board for 13 seconds every 3 minutes, until you pick one.

## When you hold the button

The button on the controller shows the panel's address and resets it. See [Buttons and resets](/using/buttons-and-resets/).

| The panel says | What it means |
| --- | --- |
| **On your phone, open** the address **or** the IP address **then pick your team** | You held the button for about 1.5 seconds. The address shows for 20 seconds. On a 64x64 panel it shows the address over two lines, then the IP address and **pick a team**. |
| **Keep holding to reset Wi-Fi**, with a bar filling | You've held it for 5 seconds. Let go now and nothing resets. |
| **Wi-Fi reset / restarting** | You held it for 10 seconds. The panel forgets its Wi-Fi and restarts into setup. |

:::note[Coming in the next release]
Nothing resets until you let go, and the screen says what letting go will do.

| The panel says | Let go and |
| --- | --- |
| **Keep holding / Wi-Fi reset / in 5s**, counting down | Nothing resets. |
| **Release to reset Wi-Fi**, then **Hold 10s to erase all**, counting down | The Wi-Fi resets. The panel says **Wi-Fi reset / restarting**. |
| **Release to erase all**, then **Hold 10s to cancel**, counting down | Factory reset. The panel says **Erasing all / restarting**. |
| **Release to cancel** | Nothing resets. |
:::

## Icons in the corner

:::note[Coming in the next release]
A small icon pulsing slowly in the top right corner means the panel is holding its brightness down. Every two minutes the bottom line says why, for 15 seconds.

| You see | What it means | What to do |
| --- | --- | --- |
| An orange thermometer and **Too warm, dimming** | The panel is running hot and has dimmed itself. | Keep it out of direct sun and give it air behind it. It goes back to your brightness as it cools. |
| A red plug and **Check power supply** | The panel restarted twice in a row because its power dipped, so it keeps its brightness low. | Use the power supply that came with it, not a phone charger or a USB port. Then press **Clear** on the device page. |

More in [Troubleshooting](/help/troubleshooting/#the-panel-is-dim-or-shows-a-thermometer-or-plug-in-the-corner).
:::

## Red, green, blue and white screens

If the whole panel lights red, green, blue and white in turn, then a white line runs down it and a screen of details ends with **Press button**, the panel is running its factory check. Press the button on the controller once. The panel says **Button OK** and goes back to normal.

## During an update

The panel shows no message of its own while it updates. It restarts when the update is done and comes back on its start-up screen. The device page shows how the update is going. See [Updates](/using/updates/).

| The device page says | What it means |
| --- | --- |
| **Update available** and a version | A newer release is out. Press **Install** to install it. |
| **Installing** and a version | The update is running. Keep the panel powered. The page reconnects when the panel is back. |
| **You're up to date** | The panel has the latest release. |
| **Could not reach the update server. Try again in a minute.** | After you tap **Check for updates**, the check didn't get an answer. |
| **Not checked yet** | The panel hasn't checked for updates since it started. It checks a few seconds after its clock is set, or tap **Check for updates**. |

:::note[Coming in the next release]
| The device page says | What it means |
| --- | --- |
| **The update to** a version **didn't take and the panel went back to** another | The new version crashed as it started, so the panel went back to the one it had. It won't install that version on its own again. **Install** tries it once more. |
:::
