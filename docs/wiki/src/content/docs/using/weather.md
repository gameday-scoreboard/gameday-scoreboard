---
title: "Weather"
description: "Your town's forecast between games: the temperature now, today's high and low, and the next two days, from Open-Meteo."
sidebar:
  order: 12.6
---

:::note[Coming in the next release]
The weather card is new in the next release.
:::

Between games the panel can show the weather where you live: the temperature now with an icon for the sky, today's high and low, and the next two days small underneath. Temperatures are in °F.

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

## How often it reads

The panel reads the forecast every 30 minutes, and once right after you set a town or the panel starts. It doesn't read while a game it follows is live, so the forecast never takes time from the scores; it reads again once the game ends.

If a read fails, the panel tries again a little later, waiting longer each time it fails. The device page says when the last good read was. A forecast more than 3 hours old leaves the rotation until a read works again.

## Where the forecast comes from

The panel asks [Open-Meteo](https://open-meteo.com/), a free weather service, for your town's place and forecast. It needs no account or key. Your town goes to Open-Meteo for the forecast; nothing else about you or the panel does.

The town and the switch are kept in a [settings backup](/using/backup/).
