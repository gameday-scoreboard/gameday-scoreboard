---
title: "Effects"
description: "Fill the panel with a moving pattern in place of the scoreboard, from the device page or Home Assistant, and what the panel still does while one runs."
sidebar:
  order: 13
---

:::note[Coming in the next release]
Effects arrive in the next release. A panel on v2026.10.2 has only the Scoreboard and the Clock under **Showing**; everything on this page describes the release after it.
:::

**Effects** fills the whole panel with a moving pattern, one of 52 from the [WLED](https://kno.wled.ge) project, in place of the scoreboard: a slow plasma for the shelf, falling snow in December, fireworks at a party. It is the third page under **Showing**, next to the Scoreboard and the Clock. The panel keeps following your games underneath, so the moment you go back to the Scoreboard the board is up to date, and the device page, the app, Home Assistant and the remote all keep working while a pattern runs.

Nothing plays an effect unless you pick it. Every time the panel starts it shows the Scoreboard, and the panel never switches to Effects on its own.

## Turning it on

On the [device page](/using/device-page/), in the Panel card:

1. Set **Showing** to **Effects**. A panel that was switched off lights up for it.
2. Pick an **Effect** and a **Palette** from the controls that appear under it. **Default** is the palette the effect was designed with.
3. Set **Speed** and **Intensity**, 0 to 255. What intensity does depends on the effect: how many drops in **Rain**, how big the blobs in **Metaballs**, how many fireworks go up.

Set **Showing** back to **Scoreboard** (or **Clock**) to get the board back. **Brightness**, **Panel on** and the [night schedule](/using/brightness-and-clock/#night-schedule) work on an effect the same as on the board.

In [Home Assistant](/using/home-assistant/), **Select Page** has the same **Effects** option, **Page Next** and **Page Previous** step through it, and **Effect**, **Effect Palette**, **Effect Speed** and **Effect Intensity** set the pattern, so an automation can put a pattern up for a party and the Scoreboard back at bedtime. The Game Day app gets the same controls in a later release.

## What the panel does while an effect runs

The effect has the whole screen, so the things the panel draws on the board wait until you leave the Effects page: a score and its celebration, the countdown, the line score, another favorite's score banner and your [message card](/using/messages/). Scores still reach Home Assistant and the device page, which show the game as usual.

When the panel has something to tell you, or you ask it for the screen, it switches **Showing** back to **Scoreboard** itself. The device page and Home Assistant show the change, and the panel's log says why. That happens for:

- **Wi-Fi lost**, a Wi-Fi that wants a sign-in, and **No internet** (see [Panel messages](/help/panel-messages/)).
- The heat or power mark in the corner: **Running warm** or **Power dipped**.
- A **banner** you send from the device page, the app or Home Assistant. It shows over the board, not over the effect.
- Holding the button on the controller for the panel's address, and **Show on panel** under **FCC information**.
- A firmware install, so the panel sits on the board, not a pattern, until it restarts.
- The night schedule's screen-off, and turning **Panel on** off. Picking Effects while the panel is dim or dark for the night turns night mode off for the rest of the night, the same as **Panel on** or a team pick.

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

The patterns that react to sound are left out.

Most patterns run at the panel's full frame rate, about 43 frames a second, on a 64x64 panel. The busiest ones, **Distortion Waves**, **Soap**, **Plasma Ball** and **Tartan**, run slower, and on a 128x64 display more of them do. A slower pattern just moves more slowly, which suits some rooms.
