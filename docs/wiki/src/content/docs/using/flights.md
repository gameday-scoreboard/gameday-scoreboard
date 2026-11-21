---
title: "Flights"
description: "The planes flying near your home on the panel: callsign, airline, route, aircraft type, altitude, speed, distance and direction, from free community flight data or your own ADS-B receiver."
sidebar:
  order: 13.6
---

**Flights** shows the planes flying near your home in place of the scoreboard: the nearest one's callsign, airline, route and aircraft type, how high and fast it's going, and how far away it is and in which direction. It's the fifth page under **Showing**, after the Scoreboard, the Clock, Effects and the Smart board. It needs no account and no setup beyond the town you already gave [Weather](/using/weather/).

## Turning it on

- **All the time:** set **Showing** to **Flights** on the [device page](/using/device-page/)'s Home tab, or **Select Page** in Home Assistant. It stays up, through restarts and power cuts, until somebody picks another page. While it's up the panel stops reading scores.
- **Between games:** set **After the game** to **Show flights** (under **Screen** on the Display tab). Once your game's final has been up a while, Flights goes up, and the scoreboard comes back at the **Turns on** time before your next game. See [Game-day wake and after the game](/using/brightness-and-clock/#game-day-wake-and-after-the-game).

## Setting it up

Flights uses the town you set for [Weather](/using/weather/) as home. If no town is set, the panel says **Set your town on the device page**.

The **Flights** card on the device page's Customize tab has these settings:

- **Within**: how far from home a plane can be: 5, 10, 15, 25 or 50 miles, or 10, 15, 25, 40 or 80 km when Weather uses Celsius. A new panel starts at 10 miles (15 km).
- **Facing**: the way your window faces. The card then shows the nearest plane in that direction, the one you can see, and falls back to the nearest plane anywhere when nothing is that way. **Any direction** shows the nearest plane.
- **ADS-B receiver** and **adsb.lol key**: both optional, see [Faster updates](#faster-updates).

The card's status line says what the panel is doing, like **8 planes nearby, from adsb.lol, read 20 s ago**.

## What the panel shows

The card shows one plane:

- its callsign, like **AAL2192**, or its registration when it has no callsign. During an emergency (squawk 7500, 7600 or 7700, or an emergency the plane declares) the callsign is red.
- the airline, like **American**, on 128-wide panels
- the route in gold, like **DFW > ORD**, when one is known and it fits where the plane is and the way it's heading. Otherwise the registration or the aircraft type.
- the aircraft type, like **737-800**, or just its code, like **B738**, on a 64-wide panel
- the altitude and ground speed
- how far away it is and in which direction, like **2.4 mi SW**, with an arrow when it's climbing or descending

When a plane's heading and speed will bring it within about a mile and a quarter of home in the next three minutes, the distance line says **Overhead in 40 s** instead, counting down.

When there's more than one plane nearby, a list of the nearest four (callsign, altitude and distance) takes turns with the card, about 8 seconds each. The card stays on the same plane until another one is clearly nearer, so it doesn't flip between two planes at about the same distance.

The panel reads the planes once a minute, and in between it moves each one along its heading at its speed, so the distance and direction change every second.

Instead of a plane, the panel shows a message when it has none to show:

- **Looking for planes**: the first read hasn't come back yet.
- **No planes within 10 mi**: nothing is flying within your **Within** distance right now. Planes on the ground show only when nothing is in the air.
- **Can't reach flight data**: the panel couldn't read the planes. It keeps trying.
- **Flight data paused**: the free daily limit is used up (see below). The line under it says when it comes back.

## Where the data comes from

Plane positions come from [adsb.lol](https://www.adsb.lol), a free, community-run network of ADS-B receivers whose data is open under the [ODbL 1.0](https://opendatacommons.org/licenses/odbl/1.0/) license. Routes come from [adsbdb](https://www.adsbdb.com), a free flight route database. Neither needs an account.

If Flights is useful to you, please support them: [sponsor adsb.lol](https://www.adsb.lol/sponsor), or [share your own ADS-B receiver](https://www.adsb.lol/docs/get-started/introduction) with its network.

The panel reads only while Flights is on screen and the panel is lit. Without a key, adsb.lol allows 500 reads a day for each home internet connection, which at one read a minute is a little over 8 hours of Flights on screen. Other devices in your home that use adsb.lol count toward the same 500. When they run out, the panel shows **Flight data paused** until adsb.lol's day starts over at midnight UTC.

A route is looked up once for the plane on the card and kept for 3 hours.

## Faster updates

- **adsb.lol key:** a free key raises the limit to 1,000 reads a day, and the panel then reads every 30 seconds, still about 8 hours of Flights on screen a day. [Request a key](https://www.adsb.lol/request) from adsb.lol, then paste it into **adsb.lol key** on the Flights card. The device page never shows the key again once it's saved, only **Key saved**, and a [settings backup](/using/backup/) leaves it out.
- **ADS-B receiver:** a receiver on your home network (tar1090, readsb or dump1090) is read every 10 seconds with no limit. Type its address in **ADS-B receiver**, like **192.168.1.50**, and the panel reads `/tar1090/data/aircraft.json` from it, or give the whole address of its `aircraft.json`. If the receiver stops answering twice in a row, the panel reads adsb.lol until it answers again.

## Your location

Each read to adsb.lol sends your home rounded to two decimal places, about a kilometer, and the **Within** distance. A route lookup sends only the plane's callsign. Your receiver is read on your own network, so nothing about it leaves your home.
