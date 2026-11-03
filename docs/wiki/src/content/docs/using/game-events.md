---
title: "Game events for your smart home"
description: "The events the panel sends Home Assistant, or any address on your network, when your game starts, on every score, lead change and period, and at the final, with ready-made blueprints."
sidebar:
  order: 16.5
---

:::note[Coming in the next release]
Everything on this page comes with the next release. Until then the panel sends Home Assistant only `esphome.gameday_score` and `esphome.gameday_final`, described on [Home Assistant](/using/home-assistant/#score-events), and has no webhook.
:::

The panel can be the heart of game day at home. As the game it follows goes on, it sends an event at each moment that matters, with both teams and their colors, so your lights, speakers and TV can join in:

| Event | When |
| --- | --- |
| `esphome.gameday_pregame` | Your game starts soon: 15 minutes before, or what you set. |
| `esphome.gameday_start` | Kickoff, tip-off, first pitch or puck drop. |
| `esphome.gameday_score` | Either side scores. |
| `esphome.gameday_lead_change` | A side takes the lead, the lead changes hands, or the score comes level. |
| `esphome.gameday_period` | A quarter, half, period or inning ends. |
| `esphome.gameday_final` | The game is over. |
| `esphome.gameday_test` | You pressed **Send a test event** on the device page. |

They follow the game Home Assistant's **Game Status** shows: your team's in Favorite teams, or the game on the board in Live games. Nothing about them needs setting up. Home Assistant receives them as soon as the panel is added to it, without the "Allow the device to perform Home Assistant actions" option.

## Ready-made blueprints

Two blueprints do the most-asked things. Import one in Home Assistant under **Settings**, then **Automations & scenes**, **Blueprints**, **Import blueprint**, with its address:

- **Flash the lights in team colors on a score**: `https://docs.gameday-scoreboard.com/blueprints/gameday-score-lights.yaml`. Pick your lights, your team's scores or both, which plays count and how many flashes. The lights flash in the scoring team's color and then go back the way they were.
- **Goal horn on a speaker**: `https://docs.gameday-scoreboard.com/blueprints/gameday-goal-horn.yaml`. Pick a speaker and a sound, and a second sound for a win if you like.

Then press **Send a test event** in the device page's **Game events** card. It sends a made-up score for your team, so you can watch the lights flash or set the speaker's volume without waiting for a game.

:::tip
A team whose color is very dark, like navy, looks dim on most bulbs. The blueprint turns the lights to full brightness, but a light strip shows dark colors better than a bulb does.
:::

## What each event carries

Every event has these fields. Every value is text, so match `"true"` and `"7"` in quotes in an automation.

| Field | What it is |
| --- | --- |
| `kind` | `pregame`, `start`, `score`, `lead_change`, `period`, `final` or `test`. |
| `league` | `nfl`, `ncaa`, `mlb`, `nba`, `wnba`, `mcbb`, `nhl`, `mls` or `epl`. |
| `sport` | `football`, `baseball`, `basketball`, `hockey` or `soccer`. |
| `team`, `opponent` | The two sides, like `DAL` and `PHI`. `team` is the side the event is about: the side that scored, the side that leads now, or for the rest, the side on the left of the board. |
| `team_score`, `opponent_score` | The score now, `team`'s first. |
| `my_team` | `true` when `team` is your team. Always `false` in Live games, where neither side is yours. |
| `team_color`, `opponent_color` | ESPN's color for each side, like `#002A5C`. |
| `team_rgb`, `opponent_rgb` | The same colors as `0,42,92`, ready for a light's `rgb_color`. |
| `game_id` | ESPN's id for the game. |
| `panel` | The panel's name, like `gameday-2f6a70`, for homes with more than one. |

And each kind adds its own:

| Event | Fields |
| --- | --- |
| `pregame` | `minutes`: to the start. |
| `score`, `test` | `play`: `touchdown`, `field_goal`, `extra_point` or `two_point` (a two-point try or a safety) in football, `run` or `home_run` in baseball, `goal` in hockey and soccer, `score` in basketball and for any other football score. `points`: since the panel's last look, which in basketball can be more than one basket. |
| `lead_change` | `change`: `took_lead` from a level score, `lead_change` when it changes hands, `tied` when the score comes level. `leader` and `previous_leader`: the sides, empty when level. |
| `period` | `period`: the number of the one that ended. `period_name`: `1st quarter`, `2nd half`, `3rd period`, `7th inning`, `overtime`. |
| `final` | `result`: `win`, `loss` or `draw` for `team`. `winner`: empty on a draw. |

A touchdown that puts your team ahead looks like this in **Developer tools**, then **Events**, listening to `esphome.gameday_score`:

```yaml
event_type: esphome.gameday_score
data:
  kind: score
  league: nfl
  sport: football
  team: DAL
  opponent: PHI
  team_score: "7"
  opponent_score: "0"
  my_team: "true"
  team_color: "#002A5C"
  team_rgb: 0,42,92
  opponent_color: "#06424D"
  opponent_rgb: 6,66,77
  game_id: "401772938"
  play: touchdown
  points: "7"
  panel: gameday-2f6a70
```

followed by its lead change:

```yaml
event_type: esphome.gameday_lead_change
data:
  kind: lead_change
  team: DAL
  opponent: PHI
  team_score: "7"
  opponent_score: "0"
  my_team: "true"
  change: took_lead
  leader: DAL
  previous_leader: ""
  # and the fields every event has
```

the end of the second quarter:

```yaml
event_type: esphome.gameday_period
data:
  kind: period
  team: DAL
  opponent: PHI
  team_score: "7"
  opponent_score: "10"
  period: "2"
  period_name: 2nd quarter
  # and the fields every event has
```

and the final:

```yaml
event_type: esphome.gameday_final
data:
  kind: final
  team: DAL
  opponent: PHI
  team_score: "24"
  opponent_score: "10"
  my_team: "true"
  result: win
  winner: DAL
  # and the fields every event has
```

## When they come

- The events of one moment come together and in order: a touchdown that takes the lead is a `score`, then a `lead_change`. A score on the last play arrives with the final.
- Each one comes once. When the board takes turns between games, in Live games or with two favorites playing at once, a score in the game that's off the board arrives when the board comes back to that game.
- After the panel restarts, or starts following a game already under way, the first event is the next thing that happens, not the ones before it. A panel turned on inside the heads-up window sends no heads-up for that game.
- ESPN sometimes serves an older copy of a game for a moment. The panel ignores a score that goes down and comes back, so nothing repeats. A score ESPN takes back for good, like a touchdown overturned on review, counts after a minute and a half, without an event.
- A period ends at the break after it, or when the next one is under way if the panel missed the break. The final ends the last period itself.
- With two favorites playing at once, the heads-up goes out for the game Game Status is following.

## The heads-up before the start

**Heads-up before the start** in the device page's **Game events** card sets how long before the start `esphome.gameday_pregame` goes out: off, or 5 minutes to 2 hours. It's 15 minutes until you change it. Use it to turn the panel, the TV or the room lights on:

```yaml
- alias: "Game day lights before kickoff"
  triggers:
    - trigger: event
      event_type: esphome.gameday_pregame
      event_data:
        my_team: "true"
  actions:
    - action: scene.turn_on
      target:
        entity_id: scene.game_day
```

## A webhook, without Home Assistant

No Home Assistant? The panel can post the same events to an address on your network, for Node-RED, a script or anything else that takes a web request. Put the address in **Also post them to a web address** on the device page and press **Save**.

Each event is a `POST` with a small JSON body, the same fields as above with `event` and `panel` first:

```json
{"event":"gameday_score","panel":"gameday-2f6a70","kind":"score","league":"nfl","sport":"football","team":"DAL","opponent":"PHI","team_score":"7","opponent_score":"0","my_team":"true","team_color":"#002A5C","team_rgb":"0,42,92","opponent_color":"#06424D","opponent_rgb":"6,66,77","game_id":"401772938","play":"touchdown","points":"7"}
```

- The address starts with `http://`. The panel doesn't post over `https://`: it is meant for something on your home network, and a second secure connection beside ESPN's would cost the panel memory it needs for the board.
- Each event gets one try, with a few seconds to answer. A listener that is down misses the event; the panel never holds up the board waiting for it, and never retries. Events wait while the panel's Wi-Fi is down, up to the last six.
- The card says how the last post went: delivered, an error code from your listener, or no answer.
- Home Assistant has webhooks of its own: an automation with a **Webhook** trigger gives you an address like `http://homeassistant.local:8123/api/webhook/<id>` that works here too. A name ending in `.local` is looked up on your network the way your phone does it; if the card says the last post got no answer, use the IP address instead.

## Entities for the game

These follow the same game, and change only when what they show does:

| Entity | What it is |
| --- | --- |
| **Game Live** | On while the game is being played. |
| **Team Score**, **Opponent Score** | The two scores; unknown before the start. |
| **Team**, **Opponent** | The two sides, like `DAL` and `PHI`. **Team** is your team in Favorite teams. |
| **Team Color**, **Opponent Color** | Each side's color as `0,42,92`, so a light can follow it: `rgb_color: "{{ states('sensor.gameday_team_color').split(',') \| map('int') \| list }}"`. |
| **Period** | Where the game is: `3rd quarter`, `Halftime`, `End of 1st`, `Top of the 5th`, `Final`. Empty before the start. |
| **Game Clock** | The clock while it runs, like `4:32`, or `67'` in soccer. Empty between periods and in baseball. |

Use your panel's own entity names; Home Assistant names them after the panel.

## In your own firmware

Building your own YAML on the gameday component? The events are also triggers: `on_score`, `on_final`, and `on_game_event` for every kind, with the event as `x` (`x.team`, `x.kind`, `x.team_rgb` and so on, from `components/gameday/ha_events.h`).
