---
title: "Weather"
description: "Your town's forecast between games, and the forecast for kickoff on the next game's board, in °F or °C, from ESPN and Open-Meteo."
sidebar:
  order: 12.6
---

:::note[Coming in the next release]
The weather card is new in the next release.
:::

Between games the panel can show the weather where you live: the temperature now with an icon for the sky, today's high and low, and the next two days small underneath. Before an outdoor game the next game's board also shows the forecast for kickoff at the stadium.

## Set your town

Under **Between games** on the [device page](/using/device-page/), type your town in **Town or ZIP code** and press **Look up**. Any of these work:

- A US ZIP code, like `75019`
- A town and state, like `Springfield, IL` or `Portland, Maine`, which picks the right one of several towns with the same name
- A town anywhere else, like `London, Ontario` or `São Paulo`

The panel looks the town up once and shows what it found under the box, like **Found: Coppell, Texas**. If it says **No place found**, try the town and state, or a ZIP code.

Setting a town turns the **Weather** switch in the **Between games** list on. Turn the switch off to keep the town and hide the card; clear the box and press **Look up** to forget the town.

## What it shows

The weather card takes 10 seconds of every minute, from 40 to 49 seconds past the minute, so every panel in the house shows it at the same moment, right before [your own countdown](/using/countdowns/#your-own-countdown)'s turn. It comes up wherever the board is between games: the next game's board, the [off-season clock](/using/brightness-and-clock/#the-off-season-clock), Favorite teams' cards, and the finals Live games shows. It never covers a live game, a favorite that has taken the panel, or the panel's own trouble messages. The ticker keeps running along the bottom.

On a wide board the temperature is big, with **Hi** and **Lo** beside it, the sky in words under it, and the next two days side by side at the bottom. A single 64x64 panel stacks the same things in smaller letters.

At night a clear sky shows a moon instead of a sun.

## Fahrenheit or Celsius

:::note[Coming in the next release]
The **Degrees** setting is new in the next release. Before it, the weather card is in °F only.
:::

**Degrees** under **Weather** on the device page picks **Fahrenheit**, **Celsius** or **Auto**, the setting a new panel starts with. Auto uses °F when your town is in the United States (or one of the few other places that use it) and °C anywhere else. With no town set yet, it goes by the panel's time zone: °F in the US time zones, °C in the others. The device page shows which one Auto picked, like **Auto (°F now)**.

The setting covers the weather card, the kickoff forecast on the board, and the forecast in the ticker ("12C Cloudy at Ryan Field"). Switching it needs no new read: the board changes at once.

## The forecast for kickoff

:::note[Coming in the next release]
The kickoff forecast on the board is new in the next release.
:::

Before a football, baseball or soccer game at an outdoor stadium, the next game's board shows a small icon for the sky and the temperature expected at kickoff. On a wide board it sits between the two logos, where the game clock goes once the game starts; on a single 64x64 panel it sits between the two scores. It goes away when the game starts, and it never shows for a dome or an arena (a venue ESPN lists as indoors).

The forecast is ESPN's when ESPN has one for the game, which it usually does for NFL, college football and baseball games a few days out. That's the same forecast the ticker already shows ("54F Clear at Ryan Field"). When ESPN has none, which is most soccer games and games further out, the panel asks Open-Meteo for the hour of kickoff at the stadium's city, as soon as the game is 15 days away or closer. Then the ticker shows that forecast too.

This works without a town set and whether the weather card is on or off. It reads at most once every three hours for a game, only while the next ESPN read is at least 25 seconds away, so it never holds up the scores.

## How often it reads

The panel reads the forecast every 30 minutes, and once right after you set a town or the panel starts. It doesn't read while a game it follows is live, so the forecast never takes time from the scores; it reads again once the game ends.

If a read fails, the panel tries again a little later, waiting longer each time it fails. The device page says when the last good read was. A forecast more than 3 hours old leaves the rotation until a read works again.

## Where the forecast comes from

The panel asks [Open-Meteo](https://open-meteo.com/), a free weather service, for your town's place and forecast. It needs no account or key. Your town goes to Open-Meteo for the forecast; nothing else about you or the panel does. For the kickoff forecast the panel sends the stadium's city, like "Green Bay, WI", and the hour of kickoff.

The town, the switch and **Degrees** are kept in a [settings backup](/using/backup/).
