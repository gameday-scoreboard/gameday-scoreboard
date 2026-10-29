---
title: "Effects"
description: "Fill the panel with a moving pattern in place of the scoreboard, from the device page or Home Assistant, and what the panel still does while one runs."
sidebar:
  order: 13
---

:::note[Coming in the next release]
Effects arrive in the next release. A panel on v2026.10.2 doesn't have the **Effects** card yet; everything on this page describes the release after it.
:::

**Effects** fills the whole panel with a moving pattern, one of 52 from the [WLED](https://kno.wled.ge) project, in place of the scoreboard: a slow plasma for the shelf, falling snow in December, fireworks at a party. The panel keeps following your games underneath, so the moment you turn Effects off the board is up to date, and the device page, the app, Home Assistant and the remote all keep working while a pattern runs.

Effects is off every time the panel starts. It never comes on by itself.

## Turning it on

On the [device page](/using/device-page/), the **Effects** card sits after **Message**:

1. Turn on **Effects**. A panel that was switched off lights up for it.
2. Pick an **Effect** and a **Palette**. **Default** is the palette the effect was designed with.
3. Set **Speed** and **Intensity**, 0 to 255. What intensity does depends on the effect: how many drops in **Rain**, how big the blobs in **Metaballs**, how many fireworks go up.

Turn **Effects** off to get the scoreboard back. **Brightness**, **Panel on** and the [night schedule](/using/brightness-and-clock/#night-schedule) work on an effect the same as on the board.

In [Home Assistant](/using/home-assistant/), the panel has an **Effects** switch, **Effect** and **Effect Palette** selects, and **Effect Speed** and **Effect Intensity** numbers, so an automation can turn a pattern on for a party and off at bedtime. The Game Day app gets the same controls in a later release.

## What the panel does while an effect runs

The effect has the whole screen, so the things the panel draws on the board wait until Effects is off: a score and its celebration, the countdown, the line score, another favorite's score banner, your [message card](/using/messages/) and the clock page. Scores still reach Home Assistant and the device page, which show the game as usual.

When the panel has something to tell you, or you ask it for the screen, it turns Effects off and the board comes back. The device page and Home Assistant show the switch off, and the panel's log says why. That happens for:

- **Wi-Fi lost**, a Wi-Fi that wants a sign-in, and **No internet** (see [Panel messages](/help/panel-messages/)).
- The heat or power mark in the corner: **Running warm** or **Power dipped**.
- A **banner** you send from the device page, the app or Home Assistant. It shows over the board, not over the effect.
- Holding the button on the controller for the panel's address, and **Show on panel** under **FCC information**.
- A firmware install, so the panel sits on the board, not a pattern, until it restarts.
- The night schedule's screen-off, and turning **Panel on** off. While the screen is off for the night, Effects stays off if you try to turn it on.

## The effects

The 52 patterns in the list. Some, like **Scrolling Text** (it says GAME DAY) and **Halloween Eyes**, suit a particular night; most look best with a palette that fits the room.

- Black Hole
- Blobs
- Colored Bursts
- Crazy Bees
- Distortion Waves
- DNA
- DNA Spiral
- Drift
- Drift Rose
- Firenoise
- Fireworks
- Fireworks 1D
- Frizzles
- Game Of Life
- Ghost Rider
- Halloween Eyes
- Hiphotic
- Julia
- Lissajous
- Matrix
- Metaballs
- Noise2D
- Octopus
- Palette
- Plasma Ball
- Polar Lights
- PS Attractor
- PS Ballpit
- PS Box
- PS Fire
- PS Fireworks
- PS Fuzzy Noise
- PS Galaxy
- PS Ghost Rider
- PS Impact
- PS Volcano
- PS Vortex
- PS Waterfall
- Pulser
- Rain
- Ripple
- Ripple Rainbow
- Rotozoomer
- Scrolling Text
- Sindots
- Snow Fall
- Soap
- Spaceships
- Squared Swirl
- Sun Radiation
- Tartan
- Waving Cell

The patterns that react to sound are not in the list: the panel has no microphone.

Every pattern runs at the panel's full frame rate on a 64x64 panel. On a 128x64 display the busiest ones, **Distortion Waves** and **Soap** among them, run at a lower frame rate and look slower, which suits some rooms.
