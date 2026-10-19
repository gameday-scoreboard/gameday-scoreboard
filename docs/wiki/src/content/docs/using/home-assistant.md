---
title: "Home Assistant (optional)"
description: "The panel is an ESPHome device: add it to Home Assistant to see the game status and automate power, brightness, the team and updates."
sidebar:
  order: 16
---

Nothing about the panel needs Home Assistant. It reads ESPN, runs its own device page and updates itself over Wi-Fi. If you already use Home Assistant, you can add the panel to it, because the firmware is an ordinary ESPHome device.

## Adding it

With Home Assistant on the same network, it discovers the panel as a new ESPHome device and offers to add it. The prebuilt firmware has no API encryption key, so there is no key to enter.

Picking a team or a mode from Home Assistant counts as setting the panel up, the same as the app or the device page, so the ticker stops asking you to pick a team.

## What appears

| Entity | What it is |
| --- | --- |
| **Team** | The team the panel follows in My team. Every league except men's college basketball, whose list would be too long for one entity; pick those teams on the device page. |
| **Mode** | My team, Favorite teams or Live games, plus the older Live NFL, Live college and Live anything. |
| **Power** | The panel's lights on or off. |
| **Brightness** | 1 to 255. |
| **Scroll Speed** | The ticker speed, 1 to 10. |
| **Select Page** | The Scoreboard or the Clock page, with **Page Next** and **Page Previous** buttons. |
| **Game Status** | The game in one line: the matchup, whether it's upcoming, live or final, and the score. |
| **Last Play** | ESPN's latest play. |
| **Firmware** | The firmware update, so Home Assistant shows when a new release is out. |
| **Install Updates Automatically** | The same switch as on the device page. See [Firmware updates](/using/updates/). |
| **Check for Updates**, **Refresh Now**, **Reboot** | The device page's buttons. |
| **Reset Wi-Fi**, **Factory Reset** | The resets. See [The boot button and resets](/using/buttons-and-resets/). |
| **WizMote Auto-Discovery**, **WizMote MAC Address**, **WizMote Status**, **Clear WizMote Pairing** | Pairing a WizMote. See [WizMote remote](/using/remote/). |
| **IP**, **ESPN Data** | The panel's IP address, and how much it has downloaded from ESPN since it started. |

A few diagnostic sensors, the Wi-Fi signal and the free memory readings, are there but turned off until you turn them on in Home Assistant.

Favorites, the timezone, the ticker switches, the celebrations, the Live games leagues and the panel layout have no entities. Set them on the device page or in the app.

:::note[Coming in the next release]
A **Chip Temperature** sensor reports the controller's temperature, the reading the panel uses to dim itself when it runs hot.

**Game Live** is on while the game in Game Status is being played. **Team Score** and **Opponent Score** are that game's two scores: Team is the side on the left of the board, your team in My team and Favorite teams. They change only when a score does, and are unknown when there is no game or it hasn't started.
:::

## Score events

:::note[Coming in the next release]
The panel sends Home Assistant an event every time either side scores in the game Game Status follows, and another when that game ends. They come whether or not the panel celebrates the score on screen, in every sport, basketball included. An event goes out once per score, so nothing repeats in your logbook. When the board takes turns between games, in Live games or with two favorites playing at once, a score in the game that's off the board arrives when the board comes back to that game.

`esphome.gameday_score` carries:

| Field | What it is |
| --- | --- |
| `team` | The team that scored, like `DAL`. |
| `opponent` | The other team. |
| `play` | `touchdown`, `field_goal`, `extra_point` or `two_point` (a two-point try or a safety) in football, `run` or `home_run` in baseball, `goal` in hockey and soccer, `score` in basketball and for any other football score. |
| `points` | The points since the panel's last check. In basketball that can be more than one basket. |
| `team_score`, `opponent_score` | The score now, the scoring team's first. |
| `sport` | `football`, `baseball`, `basketball`, `hockey` or `soccer`. |
| `my_team` | `true` when your team scored. Always `false` in Live games, where neither side is yours. |

`esphome.gameday_final` carries `team` and `opponent` (the left and right sides of the board), both scores, `result` (`win`, `loss` or `draw` for `team`), `winner` (empty on a draw), `sport` and `my_team` (`true` when `team` is your team).

Every value arrives as text, so match `"true"` and `"7"` in quotes. To see them, open **Developer tools**, then **Events**, and listen to `esphome.gameday_score` during a game. This automation flashes a light when your team scores a touchdown:

```yaml
- alias: "Flash the lights on our touchdown"
  triggers:
    - trigger: event
      event_type: esphome.gameday_score
      event_data:
        play: touchdown
        my_team: "true"
  actions:
    - action: light.turn_on
      target:
        entity_id: light.living_room
      data:
        flash: long
```

Use `play: goal` for hockey or soccer, `play: home_run` for baseball, or leave `play` out to catch every score. Home Assistant accepts these events from the panel without the "Allow the device to perform Home Assistant actions" option.
:::

## Messages

:::note[Coming in the next release]
The **Message** text entity puts a banner on the panel: set it, from a dashboard or an automation's `text.set_value`, and the text goes over the board for 10 seconds. The **show_message** action, listed as `esphome.<your panel's name>_show_message`, does the same with `message` and `seconds` (0 for 10):

```yaml
- alias: "Tell the room the pizza is here"
  triggers:
    - trigger: state
      entity_id: binary_sensor.front_door
      to: "on"
  actions:
    - action: esphome.gameday_2f6a70_show_message
      data:
        message: "Pizza's here!"
        seconds: 15
```

Use your panel's own name in the action. Like the other device actions, it needs no "Allow the device to perform Home Assistant actions" option. The message card that takes turns between games is set on the device page. See [Your own messages](/using/messages/).
:::

## What you can automate

Game Status changes when a game starts, when someone scores and when it ends, not on every play, so it makes a good trigger and keeps your history small. Some ideas:

- Flash the room lights or play a sound when Game Status changes during your team's game.
- Turn the panel on before kickoff and off at bedtime with **Power**, or turn **Brightness** down at night.
- Switch **Team** or **Mode** for the evening's game.
- Install updates on your own schedule with the **Firmware** entity.

If you want room lighting to celebrate with the score, the separate [gameday-matrix](https://github.com/bharvey88/gameday-matrix) project does the same game tracking through a Home Assistant blueprint, using the same splash rules.

## On your network

The panel treats your home network as trusted: Home Assistant, the app and the device page all connect without a password. Anyone on your Wi-Fi can change its settings. Don't expose it to the internet.
