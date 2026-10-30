---
title: "Several games at once"
description: "Games on the board: two or four games on the panel as tiles, or Auto, which shows as many live games as there are worth showing."
sidebar:
  order: 4
---

:::note[Coming in the next release]
Everything on this page is new. Today's panel shows one game at a time.
:::

The panel normally shows one game, big. **Games on the board**, in the Panel section of the device page, lets it show several at once as tiles:

- **One game**: the board you know. The default.
- **Two**: two games side by side, each with both logos, the score and a status line.
- **Four**: four games at once. On one panel they are small tiles two by two; on a two-panel board they go four across at the two-game size.
- **Auto**: as many live games as there are worth showing, up to four. With two games on, two tiles; with three or more, four; with one game on, the big board.

The Game Day app has the same setting. A 64-wide panel never tiles; there is no room.

## What a tile shows

A two-game tile is the single-panel board in miniature: both logos, a bar under each in the team's color, the scores in big digits and a status line under them. A four-game tile on one panel has no logos (a logo that small is a smudge): each side is its abbreviation on a badge in the team's color, with the score beside it, and the status line under both.

The status line says where the game stands:

- Live: the period and clock, like **Q3 4:12**, **P2 1:05** (hockey), **H2 8:30** (college basketball), **TOP 5** or **BOT 5** (baseball) and **65'** (soccer), or **HALF**, **END Q3**, **OT 2:10**, **DELAY**.
- Over: **F**, or **F/OT** and **F/SO**.
- Not started: the kickoff, like **7:30 PM**, or **Sun 7:30 PM** for another day.

A gold dot by a score marks the side with the ball in football, or the side batting in baseball. When a side scores, its tile flashes in that team's color for a moment.

## Which games get a tile

In **Live games**, the tiles come from the same scoreboards the panel reads to follow a game. Live games fill the tiles first, your favorites ahead of the rest and then the closest games; finals and the next games to start fill what is left. In **Favorite teams**, the tiles are your favorites' games, in your list's order, whether live, over or still to come. Two favorites playing each other share a tile.

Tiles keep their places. A game stays in its tile while it is live, and for half an hour after it ends; a new live game takes an empty tile, or one a final or a game to come was holding, never another live game's. So the scores don't jump around between reads.

The one-game board, the ticker, the score splashes and Home Assistant keep following the game the panel would follow anyway, so the celebrations and sensors work as before. The splash for that game shows over the tiles.

## Reads from ESPN

With tiles up in Live games, the panel reads each league's scoreboard every 45 seconds, on top of the game it follows. That is one read per league, however many games are on. With fewer than two live games it checks every three minutes for a kickoff. In Favorite teams, every live favorite is read every 30 seconds while tiles are up.
