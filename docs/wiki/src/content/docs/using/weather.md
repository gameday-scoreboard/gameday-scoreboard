---
title: "Weather"
description: "Your town's forecast between games or as a full Weather page with the next hours, rain chance, sunrise and sunset, and the forecast for kickoff on the next game's board, in °F or °C, from ESPN and Open-Meteo."
sidebar:
  order: 12.6
---

Between games the panel can show the weather where you live: the temperature now with an icon for the sky, today's high and low, and the next two days small underneath. For more, the [Weather page](#the-weather-page) fills the whole screen with the weather now, the next few hours and the chance of rain. Before an outdoor game the next game's board also shows the forecast for kickoff at the stadium.

## Set your town

Under **Between games** on the [device page](/using/device-page/)'s Display tab, open **Weather** and type your town in **Town or ZIP code** and press **Look up**. Any of these work:

- A US ZIP code, like `75019`
- A town and state, like `Springfield, IL` or `Portland, Maine`, which picks the right one of several towns with the same name
- A town anywhere else, like `London, Ontario` or `São Paulo`

The panel looks the town up once and shows what it found under the box, like **Found: Coppell, Texas**. If it says **No place found**, try the town and state, or a ZIP code.

Setting a town turns the **Weather** switch in the **Between games** list on. Turn the switch off to keep the town and hide the card; clear the box and press **Look up** to forget the town.

## What it shows

The weather card takes 10 seconds of every minute, from 40 to 49 seconds past the minute, so every panel in the house shows it at the same moment, right before [your own countdown](/using/countdowns/#your-own-countdown)'s turn. It comes up wherever the board is between games: the next game's board, the [off-season clock](/using/brightness-and-clock/#the-off-season-clock), Favorite teams' cards, and the finals Live games shows. It never covers a live game, a favorite that has taken the panel, or the panel's own trouble messages. The ticker keeps running along the bottom.

On a wide board the temperature is big, with **Hi** and **Lo** beside it, the sky in words under it, and the next two days side by side at the bottom. A temperature of 100 or more, or -10 or below, leaves off the degree sign there so the number fits. A single 64x64 panel stacks the same things in smaller letters.

At night a clear sky shows a moon instead of a sun.

## The Weather page

**Weather** is also a page of its own, the last one under **Showing**, after Flights. It uses the town you set above, so set that first; with no town it says **Set your town on the device page**.

- **All the time:** set **Showing** to **Weather** on the [device page](/using/device-page/)'s Home tab, or **Select Page** in Home Assistant. It stays up, through restarts and power cuts, until somebody picks another page.
- **Between games:** set **After the game** to **Show the weather** (under **Screen** on the Display tab). Once your game's final has been up a while, the Weather page goes up, and the scoreboard comes back at the **Turns on** time before your next game. See [Game-day wake and after the game](/using/brightness-and-clock/#game-day-wake-and-after-the-game).

The page works whether the weather card's switch is on or off.

### What the page shows

On a wide board the left half has the weather now: the icon for the sky, the temperature, the sky in words, and today's high and low. The right half has the next 6 hours in three columns, two hours apart: the time, an icon, the temperature and the chance of rain, like **4PM**, a cloud, **69°** and **20%**. A single 64x64 panel shows the weather now and the next hours in turns, about 10 seconds each.

A line along the bottom is always there, so nothing else moves:

- While a game of your team or a favorite is live, its score and clock, like **DAL 21 PHI 17  4:12 - 3rd**. Two live games take turns every 5 seconds. A single panel shows your team and the score, like **DAL 21-17**.
- Otherwise, when rain, snow or storms have a 50% chance or more in the next 12 hours, when they start, like **Storms after 4 PM**, or **Rain now**.
- Otherwise, from 9 PM to midnight, tomorrow's high and low, like **Tomorrow 81 / 63**.
- Otherwise, the next sunrise or sunset, like **Sunset 5:22 PM**.

At night a clear sky shows a moon, and the page dims or goes dark with the [night schedule](/using/brightness-and-clock/) like every other page.

While you picked **Weather** under **Showing**, the panel still checks your games now and then for that bottom line: as often as a panel that's off does for **Game-day wake**, and every minute while one of your games is live. It doesn't send Home Assistant anything about games until the Scoreboard is back.

## Fahrenheit or Celsius

**Degrees** under **Weather** on the device page picks **Fahrenheit**, **Celsius** or **Auto**, the setting a new panel starts with. Auto uses °F when your town is in the United States (or one of the few other places that use it) and °C anywhere else. With no town set yet, it goes by the panel's time zone: °F in the US time zones, °C in the others. The device page shows which one Auto picked, like **Auto (°F now)**.

The setting covers the weather card, the Weather page, the kickoff forecast on the board, and the forecast in the ticker ("12C Cloudy at Ryan Field"). Switching it needs no new read: the board changes at once.

## The forecast for kickoff

Before a football, baseball or soccer game at an outdoor stadium, the next game's board shows a small icon for the sky and the temperature expected at kickoff. On a wide board it sits between the two logos, where the game clock goes once the game starts; on a single 64x64 panel it sits between the two scores, as the icon and the number. It goes away when the game starts, and it never shows for a dome or an arena (a venue ESPN lists as indoors).

For a kickoff after dark at a US stadium, a clear sky shows a moon instead of a sun. Dark goes by the stadium's own clock and the time of year: from 5 PM in November, December and January, 6 PM in February and October, 7 PM in March and September, 8 PM in April, May and August, and 9 PM in June and July.

The forecast is ESPN's when ESPN has one for the game, which it usually does for NFL, college football and baseball games a few days out. That's the same forecast the ticker already shows ("54F Clear at Ryan Field"). When ESPN has none, which is most soccer games and games further out, the panel asks Open-Meteo for the hour of kickoff at the stadium's city, as soon as the game is 15 days away or closer. Then the ticker shows that forecast too.

This works without a town set and whether the weather card is on or off. The panel keeps the forecasts of the last four games it read, so with Favorite teams turning its cards, each game's forecast is read at most once every three hours (a game moved to another time or stadium is read again). Like the weather card's, these reads only start while the next ESPN read is at least 25 seconds away, so they never hold up the scores.

## How often it reads

The panel reads the forecast every 30 minutes, and once right after you set a town, change the time zone or the panel starts. Just after midnight it reads again, and until that read comes in the card already shows the new day's high and low. It doesn't read while a game it follows is live, so the forecast never takes time from the scores; it reads again once the game ends. The Weather page is the exception: while it's on screen, the forecast keeps its 30 minutes, live game or not.

If a read fails, the panel tries again a little later, waiting longer each time it fails. The device page says when the last good read was. A forecast more than 3 hours old leaves the rotation until a read works again.

## Where the forecast comes from

The panel asks [Open-Meteo](https://open-meteo.com/), a free weather service, for your town's place and forecast. It needs no account or key. Your town goes to Open-Meteo for the forecast; nothing else about you or the panel does. For the kickoff forecast the panel sends the stadium's city, like "Green Bay, WI", and the hour of kickoff.

The town, the switch and **Degrees** are kept in a [settings backup](/using/backup/).
