---
title: "Panel sizes and layouts"
description: "The three panel arrangements the firmware drives, what each layout shows, and how to change it on the device page."
sidebar:
  order: 11
---

One firmware drives three arrangements of HUB75 LED panels. The panel size is a setting on the device page, not a different install.

| Arrangement | Layout | Pick on the device page |
| --- | --- | --- |
| One 128x64 panel | Wide | **One 128x64 panel** |
| Two 64x64 panels side by side | Wide | **One 128x64 panel** (the picture says "or two 64x64 side by side") |
| One 64x64 panel | Single | **One 64x64 panel** |

The Game Day Scoreboard ships with one 128x64 panel, and its firmware starts on the wide layout. Built your own? See [Panels](/diy/panels/).

## What each layout shows

### Wide: 128x64

The logos sit at the far edges with the abbreviations and big scores between them. Under the scores is the game clock, and under that the situation row: down and distance in football, red in the red zone; the count, a diamond of the runners and the outs in baseball; the latest goal in soccer; the playoff series in basketball. The records sit under the logos, with football's timeout pips or soccer's red card marks under them. The ticker runs along the bottom.

During setup, the QR code for the Game Day app sits on the left half with the words beside it.

### Single: 64x64

The two logos sit side by side at the top, with the abbreviations and scores stacked under them. During play the game clock takes the abbreviations' place, and the gold possession line in football goes under the score. Baseball's diamond sits between the two scores with the outs as three dots under it.

A single panel has no room for the records, the timeout pips, the red card marks or the situation row, so down and distance, the count and the soccer goals are in the ticker only, when their switches are on. See [The ticker](/using/ticker/).

The off-season clock shows the date under the time, and the next game goes in the ticker. During setup, the QR code and the words take turns on the screen.

## Changing the layout

1. Open the device page and find the **Setup** card.
2. Under **Matrix**, click the picture that matches your panel.
3. The page says **Restarting as** and the size. The panel restarts with the new layout a second later, and the page reconnects when it's back.

Your team and settings stay as they were. A [factory reset](/using/buttons-and-resets/) puts the layout back to the one the firmware starts on: 128x64 for the Game Day Scoreboard and the two-panel DIY build, 64x64 for the one-panel DIY build.

## Wrong size?

If the layout doesn't match the panel, the picture looks wrong:

- **The picture shows twice, side by side** (two QR codes during setup, for example): the panel is set to one 64x64 panel but is a 128x64 display. Pick **One 128x64 panel**.
- **Half the board is blank, or only part of it shows**: the panel is set to 128x64 but is a single 64x64 panel. Pick **One 64x64 panel**.

More fixes are in [Troubleshooting](/help/troubleshooting/).
