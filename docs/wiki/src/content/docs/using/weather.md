---
title: "Weather"
description: "The weather where you are between games, with a moving icon and the next few days, and the forecast for the start of an outdoor game."
sidebar:
  order: 12
---

:::note[Coming in the next release]
Between games the panel shows the weather where you live, and before an outdoor game it shows the forecast at the stadium for the start.

## The weather card

Between games the weather takes a turn on the panel, every 5 minutes unless you pick another interval. It shows two pages, 8 seconds each:

- **Now**: a moving icon (the sun's rays turn, clouds drift, rain and snow fall, a storm flashes), the temperature in big digits, the conditions in words, like "Partly cloudy", and today's high and low. The place's name is in the corner, so you can see which city the forecast is for.
- **The next days**: today and the next four days on a 128x64 board, three days on a single 64x64 panel, each with its icon, its high and its low.

The card never covers a game in progress, a score's splash or a problem the panel needs to tell you about, and it takes turns with your [message card](/using/messages/) rather than showing at the same time.

The card is on for every panel. Turn it off with **Weather card** under **Weather** on the [device page](/using/device-page/), and pick how often it shows with **Show it**. **Show it now** puts it up right away, between games.

## Where the weather is for

Type a **City or ZIP** under **Weather** on the device page and press **Save**, like "Coppell, TX", "London, England" or "75019". The panel looks the place up once and remembers it. The line under the box says what it found, or that it found nothing, so you can try another spelling.

Leave the box empty and the panel uses your time zone's city: Chicago for US Central, New York for US Eastern, London for UK and Ireland, and so on. A panel set to UTC has no city, so it shows no weather until you type one.

## Units

**Units** on **Automatic** shows Fahrenheit for a place in the US and Celsius elsewhere. Pick **Fahrenheit** or **Celsius** to choose. The setting also changes the forecast in the ticker before a game.

## The forecast for the start of a game

Before an outdoor football, baseball or soccer game, the board shows the forecast at the stadium for the start, as a small icon and a temperature between the two teams on a 128x64 board. The ticker says it in words, like "54°F Clear at Ryan Field".

Most NFL, college football and MLB games come with a forecast from ESPN. When a game has none, the panel looks up the stadium's city and asks for the forecast at the start time itself, up to about two weeks ahead. Games under a roof, and basketball and hockey, get no forecast.

## Where the forecast comes from

The weather comes from [Open-Meteo](https://open-meteo.com/), a free weather service that needs no account. The panel asks for the forecast about every 30 minutes, with a few hundred bytes in each answer, and never while it's reading a score. If the weather service can't be reached, the card stops showing after three hours without a forecast and comes back on its own once the panel gets through again.
:::
