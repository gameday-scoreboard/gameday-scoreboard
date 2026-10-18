---
title: "FAQ"
description: "Common questions about Game Day Scoreboard: the offseason, using it outside, Home Assistant, how often it checks scores, running costs, favorites, the remote, updates and how fast support answers."
sidebar:
  order: 4
---

## My team's season is over. What does the panel show?

A clock in your team's colors. Under the time is your team's next game once it's on the schedule, like **Next: at PHI Mar 26 7:05 PM**, or the date until then. Nothing is wrong with the panel, and it picks the next season up by itself.

Outside football, the clock comes up once the next game is more than 10 days away. In football it comes up only when ESPN lists no next game, so a bye week never shows it.

To keep the board busy, switch to **Favorite teams** with teams from leagues that are in season, or to **Live games** for whatever is on right now. See [Modes](/using/modes/).

## Does it work during the offseason?

Yes. Between seasons it shows the clock above, with your team's next game when ESPN has one. You can also show the clock page any time: set **Showing** in the Panel card on the device page. See [Brightness and the clock](/using/brightness-and-clock/).

## Can I use it outside?

The panel is made for indoor use. Keep it, its controller and its power supply out of rain and damp. On a covered patio, put it somewhere out of the wind. It's light, and a gust can knock it over.

## I don't have an iPhone. Can I still set it up?

Yes. The Game Day app is for Android too, and you don't need the app at all. Set the panel up from any phone or laptop through its own hotspot. While the panel shows **SETUP**, join the Wi-Fi network it names, like `Game Day 2f6a70`, and a setup page opens. Pick your Wi-Fi network, enter the password and save.

Once it's on your Wi-Fi, open the panel's address in a browser to pick your team and change everything the app would. See [Set up your panel](/get-started/setup/) and [The Game Day app](/get-started/the-app/).

## Does it need Home Assistant?

No. The panel runs on its own and gets its scores from ESPN by itself. If you use Home Assistant, it finds the panel on your network and you can automate the team, mode, power, brightness and more. See [Home Assistant](/using/home-assistant/).

## How often does it check scores?

| When | How often |
| --- | --- |
| During a game | Every 10 seconds, about as often as ESPN updates |
| In the hour before the start | Every minute |
| Further from the start | Every 15 minutes |
| After the final | Every minute for half an hour, ten minutes for baseball, then it looks up the next game |

In Live games with nothing on, the panel waits for the next start time instead of checking every few minutes. When a read fails, it waits longer before the next try.

## What does it cost to run?

Nothing beyond the panel and its electricity. There's no subscription and no account. The scores come from ESPN's public feed, and the panel reads them itself over your Wi-Fi.

If your internet plan has a data cap, **ESPN data** in the Device card on the device page shows how much the panel has downloaded since it started.

## Can I follow more than one team?

Yes. **Favorite teams** holds up to 16 teams from any league, in priority order. Turn on **Only today's games** to cycle through only the favorites playing today. When two favorites play at once, the panel sticks with the higher one, or alternates between them if you choose **Alternate**. See [Favorite teams](/using/favorites/).

## Can I use a remote?

Yes, a WiZ WizMote, the small battery remote sold for WiZ smart bulbs. It talks to the panel directly, with no hub. To pair it, open the Remote card on the device page or in the app, turn on **Discovery**, then press any button on the remote.

- **ON** and **OFF** turn the panel on and off.
- The brightness buttons make it brighter or dimmer, and the night button dims it right down.
- Buttons **1** to **4** jump to your first four favorite teams.

See [Remote](/using/remote/).

## Will it update itself?

It checks for a new release every six hours, and the device page shows an **Install** button when one is out. If you turn on **Install updates automatically** in the Device card, the panel installs a new release on its own between 3 and 5 AM, never during a live game it's showing. That switch is off unless you turn it on.

Your team and settings stay through an update. See [Updates](/using/updates/).

:::note[Coming in the next release]
If the update server doesn't answer, the panel checks a second copy of the same files at `updates.gameday-scoreboard.com`.
:::

## How fast does support answer?

Email [support@gameday-scoreboard.com](mailto:support@gameday-scoreboard.com) and we reply within one business day. See [Get support](/help/support/) for what to include.
