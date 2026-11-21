---
title: "Effects"
description: "Fill the panel with a moving pattern in place of the scoreboard, from the device page or Home Assistant, and what the panel still does while one runs."
sidebar:
  order: 13
---

**Effects** fills the whole panel with a moving pattern, one of 52 from the [WLED](https://kno.wled.ge) project, in place of the scoreboard: a slow plasma for the shelf, falling snow in December, fireworks at a party. It is the third page under **Showing**, next to the Scoreboard and the Clock. The panel keeps following your games underneath, so the moment you go back to the Scoreboard the board is up to date, and the device page, the app, Home Assistant and the remote all keep working while a pattern runs.

Nothing plays an effect unless you pick it. Every time the panel starts it shows the Scoreboard, and the panel never switches to Effects on its own unless you set **After the game** to **Show effects** (see [Between games](#between-games)).

## Turning it on

On the [device page](/using/device-page/)'s Home tab, under the board:

1. Set **Showing** to **Effects**. A panel that was switched off lights up for it.
2. Pick an **Effect** and a **Palette** from the controls that appear under it. **Default** is the palette the effect was designed with.
3. Set **Speed** and **Intensity**, 0 to 255. What intensity does depends on the effect: how many drops in **Rain**, how big the blobs in **Metaballs**, how many fireworks go up.

Set **Showing** back to **Scoreboard** (or **Clock**) to get the board back. **Brightness**, **Panel on** and the [night schedule](/using/brightness-and-clock/#night-schedule) work on an effect the same as on the board.

In [Home Assistant](/using/home-assistant/), **Select Page** has the same **Effects** option, **Page Next** and **Page Previous** step through it, and **Effect**, **Effect Palette**, **Effect Speed** and **Effect Intensity** set the pattern, so an automation can put a pattern up for a party and the Scoreboard back at bedtime. In the [Game Day app](/get-started/the-app/), the same controls show under **Showing** when it is on **Effects**.

## Between games

The panel can play an effect by itself whenever no game of yours is on, and go back to the scoreboard for the next one. It's off on a new panel. To turn it on:

1. Set **Showing** to **Effects** and pick the effect, palette, speed and intensity you want (see [Turning it on](#turning-it-on)), then set **Showing** back to **Scoreboard**.
2. On the device page's Display tab, under **Screen**, set **After the game** to **Show effects**.

From then on, once your game's final has been up a while (30 minutes, 10 for baseball), the panel switches **Showing** to **Effects** and plays that effect. It does the same whenever no game of your team or your favorites is on and the next one isn't close, so a panel with no game coming up for months plays it instead of the off-season clock and "No upcoming game". It goes back to the scoreboard by itself:

- at the **Turns on** time before your next game starts (it shows above **After the game**, 15 minutes to start), or when a game of yours goes live, and stays there through the game and its final.
- when you pick a team, a mode or your favorites, press a [remote](/using/remote/) button that follows something or picks a page, or pick another page under **Showing**. The panel stays on what you picked, and the effect comes back after your next game's final.

While it's between games, the effect steps aside for a few seconds, then comes back, for the cards you turned on under **Between games** that keep their turns: your own [countdown](/using/countdowns/), the [weather](/using/weather/) and the [message card](/using/messages/). The countdown to your next game waits until the scoreboard is back.

Anything on the [list below](#what-the-panel-does-while-an-effect-runs) still takes the screen. After a banner, the night schedule's screen-off, **Panel on** off, the heat or power mark, or a Wi-Fi or internet problem, the effect comes back once it's over. Starting the effect by itself never turns night mode off or lights a panel that's off.

It only takes over while **Showing** is on **Scoreboard**: with the Clock, the Smart board or Effects picked under **Showing**, the panel stays on that page. In Live games, a game on the board that isn't your team's keeps the board.

## Effect playlists

An effect playlist is a list of effects the panel steps through on its own, each for as long as you choose, like a playlist in WLED: a few calm patterns for an evening, or fireworks, bursts and rain for a party. You can keep up to 4 effect playlists of up to 8 effects each. They are separate from the [team playlists](/using/favorites/) on the Teams tab, which is why everything about them says **Effect playlist** in full.

To make one, open the device page's **Customize** tab and find **Effect playlists**:

1. Tap **New effect playlist** and give it a name of up to 16 characters. Any name works except one another effect playlist has and **None**, which is the choice that stops a playlist.
2. Tap **Add an effect**. The new effect starts as whatever the panel is showing now, so you can try an effect, palette, speed and intensity on the Home tab first and then add it. Change any of them in the row after: the effect and palette from their lists, the speed and intensity with their sliders.
3. Pick how long it stays up from **For**, from 1 second to 1 hour. A new effect stays up for 15 seconds.
4. Use the arrows to put the effects in order, and the **✕** to take one out.
5. Turn on **Shuffle** to play them in a random order instead. Shuffle shows every effect once before any of them comes round again, and never the same one twice in a row.

Tap **Play** to start it. The panel switches **Showing** to **Effects** if it isn't there already, and the card says **Playing now**. On the Home tab, the **Effect playlist** picker under the effect controls shows which one is playing and starts or stops one too (**None** stops it).

The playlist stops, and the panel stays on the effect it was showing, when:

- you pick an effect, palette, speed or intensity yourself, on the device page, in the app or in Home Assistant. The panel keeps what you picked. Pick the playlist again to start it over.
- **Showing** leaves **Effects**, whether you pick another page or the panel takes the screen back for one of the reasons below.
- you delete the playlist or take out all its effects.

Editing the one that's playing doesn't start it over. It carries on from the effect that's up: change that effect and the change shows at once, take it out and the next one comes up, and changing, adding or moving the others leaves it playing. Renaming the playlist or moving it in the list doesn't interrupt it either.

Every time the panel starts it shows the Scoreboard, so a playlist never comes back on its own after a restart. The effect playlists themselves are kept, and a [settings backup](/using/backup/) saves them.

A [WizMote remote](/using/remote/#a-button-that-plays-an-effect-playlist) button can start one too: set it to **Effect playlist:** and the playlist's name under **Showing**.

In [Home Assistant](/using/home-assistant/), the **Effect playlist** select lists **None** and your effect playlists by name. Picking one plays it, so an automation can start the party playlist at 7 PM. Home Assistant reads the names when it connects, so one you add or rename shows up there after the next reconnect.

## What the panel does while an effect runs

The effect has the whole screen, so the things the panel draws on the board wait until you leave the Effects page: a score and its celebration, the countdown, the line score, another favorite's score banner and your [message card](/using/messages/). Scores still reach Home Assistant and the device page, which show the game as usual.

When the panel has something to tell you, or you ask it for the screen, it switches **Showing** back to **Scoreboard** itself. The device page and Home Assistant show the change, and the panel's log says why. That happens for:

- **Wi-Fi lost**, a Wi-Fi that wants a sign-in, and **No internet** (see [Panel messages](/help/panel-messages/)).
- The heat or power mark in the corner: **Running warm** or **Power dipped**.
- A **banner** you send from the device page, the app or Home Assistant. It shows over the board, not over the effect.
- Holding the button on the controller for the panel's address, and **Show on panel** under **FCC information**.
- A firmware install, so the panel sits on the board, not a pattern, until it restarts.
- The night schedule's screen-off, and turning **Panel on** off. Picking Effects while the panel is dim or dark for the night turns night mode off for the rest of the night, the same as **Panel on** or a team pick.

Picking Effects while the heat or power mark is up, or while the panel says its Wi-Fi wants a sign-in, leaves **Showing** on **Scoreboard**, and the panel's log says why.

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
