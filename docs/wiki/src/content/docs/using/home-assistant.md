---
title: "Home Assistant (optional)"
description: "The panel is an ESPHome device: add it to Home Assistant to see the game status and automate power, brightness, the mode and updates."
sidebar:
  order: 16
---

Nothing about the panel needs Home Assistant. It reads ESPN, runs its own device page and updates itself over Wi-Fi. If you already use Home Assistant, you can add the panel to it, because the firmware is an ordinary ESPHome device.

## Adding it

With Home Assistant on the same network, it discovers the panel as a new ESPHome device and offers to add it. The prebuilt firmware has no API encryption key, so there is no key to enter.

On its device page, Home Assistant lists the panel under its own name, like **Game Day Scoreboard 2f6a70**, as **Scoreboard by Game Day**.

With the panel on an IoT network or IoT VLAN and Home Assistant on your main network, Home Assistant needs TCP port 6053 to the panel. With mDNS on between the networks it discovers the panel the same way and follows it to a new IP address by itself. Without mDNS, add it by IP address under **Settings > Devices & services > Add integration > ESPHome**, and give the panel a fixed IP address in your router. See [IoT network, IoT VLAN or IoT subnet](/using/wifi/#iot-network-iot-vlan-or-iot-subnet).

Picking a mode from Home Assistant counts as setting the panel up, the same as the app or the device page, so the ticker stops asking you to pick a team.

## What appears

| Entity | What it is |
| --- | --- |
| **Mode** | Favorite teams or Live games. A panel on one of the older football-only live modes shows Live games. |
| The panel's light | The panel on or off, and its brightness. It has the panel's own name, like **Game Day Scoreboard 2f6a70**. See [The panel's light](#the-panels-light). |
| **Scroll Speed** | The ticker speed, 1 to 10. |
| **Select Page** | The Scoreboard or the Clock page, with **Page Next** and **Page Previous** buttons. |
| **Game Status** | The game in one line: the matchup, whether it's upcoming, live or final, and the score. |
| **Last Play** | ESPN's latest play. A very long one is cut short, since Home Assistant keeps 255 characters. |
| **Firmware** | The firmware update, so Home Assistant shows when a new release is out. |
| **Install Updates Automatically** | The same switch as on the device page. See [Firmware updates](/using/updates/). |
| **Check for Updates**, **Refresh Now**, **Reboot** | The device page's buttons. |
| **Reset Wi-Fi**, **Factory Reset** | The resets. See [The boot button and resets](/using/buttons-and-resets/). |
| **WizMote Auto-Discovery**, **WizMote MAC Address**, **WizMote Status**, **Clear WizMote Pairing** | Pairing a WizMote. See [WizMote remote](/using/remote/). |
| **IP**, **ESPN Data** | The panel's IP address, and how much it has downloaded from ESPN since it started. |

The **Team** entity is gone, along with My team.

A few diagnostic sensors, the Wi-Fi signal and the free memory readings, are there but turned off until you turn them on in Home Assistant.

Favorites, the timezone, the ticker switches, the celebrations, the Live games leagues and the panel layout have no entities. Set them on the device page or in the app.

A **Playlist** select picks the playlist Favorite teams plays: **All favorites** or one of your playlists by name. Choosing one switches the panel to Favorite teams. Home Assistant reads the names when it connects to the panel, so a playlist you add or rename shows in the list after the panel restarts or Home Assistant reconnects. See [Playlists](/using/favorites/#playlists).

With favorites in more than one sport, the select also lists **All football**, **All basketball** and so on, one for each sport, after **All favorites**. See [A row for each sport](/using/favorites/#a-row-for-each-sport).

The controller's chip temperature, the reading the panel uses to dim itself when it runs hot, is on the device page under Diagnostics rather than in Home Assistant, where it wrote a row to the database every 30 seconds. **Chip Temperature**, **Effect FPS**, **Effect Render Time** and **Effect Output Time** entities left over from an older firmware show as unavailable; delete them in Home Assistant.

**Select Page** gets a third option, **Effects**, which fills the panel with a moving pattern in place of the scoreboard, with **Effect** and **Effect Palette** selects and **Effect Speed** and **Effect Intensity** numbers (0 to 255) to set it. The panel starts on the Scoreboard after every restart and puts the Scoreboard back itself when it has something to show you, so an automation that wants the pattern back after that selects Effects again. See [Effects](/using/effects/).

An **Effect playlist** select plays one of your [effect playlists](/using/effects/#effect-playlists) by name, putting the panel on Effects, and **None** stops the one playing. It shows **None** again when the playlist stops because an effect setting was changed by hand or the panel left Effects. It is separate from the **Playlist** select, which picks a playlist of teams.

**Select Page** also has **Smart board**, and four text entities, **Smart board row 1** to **Smart board row 4**, fill it: set one with **Text: Set value**, from a template that reads any entity's state or attribute, with color tags like `[red]`. Picked under **Select Page**, the Smart board stays up through restarts until something picks another page. See [Smart board](/using/smart-board/).

The game entities below, Game Status and Last Play come from the Scoreboard. While **Select Page** is on the Clock, Effects or the Smart board, the panel sends them nothing, and each keeps its last state until the Scoreboard is back.

**Game Live** is on while the game in Game Status is being played. **Team Score** and **Opponent Score** are that game's two scores: Team is the side on the left of the board, your team in Favorite teams. They change only when a score does, and are unknown when there is no game or it hasn't started.

**Team** and **Opponent** name the two sides, **Team Color** and **Opponent Color** give each side's color as `0,42,92` for a light to follow, **Period** says where the game is (`3rd quarter`, `Halftime`) and **Game Clock** shows the clock. See [Game events](/using/game-events/#entities-for-the-game).

## Score events

The panel also sends an event before the start, at the start, when the lead changes and at the end of each period, every event carries the league and both teams' colors, and there are ready-made blueprints for team-colored lights and a goal horn. The score and final events keep every field below. See [Game events](/using/game-events/).

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

After the panel restarts, or starts following a game already under way, the first event is for the next score, not the ones before it.

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

## Messages

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

## The panel's light

The panel shows up in Home Assistant as a light with the panel's own name, like **Game Day Scoreboard 2f6a70**. It's **Panel on** and **Brightness** in one light, so it works like the other lights in your home:

- Turn it on or off, or set its brightness, from a light card, a scene, a voice assistant or an automation. Its brightness is the same setting as **Brightness** on the device page, shown in Home Assistant as a percent.
- Put it in a light group, or give it an area, and it turns on and off with that room's lights. A dimmer remote on the group dims it too.
- Ask for a fade (a transition) and it fades in or out over that time. Without one it turns on and off at once.

Because it's a light, anything that turns off every light, or every light in its area, turns the panel off too. If you don't want that, leave it out of those automations, or give it no area.

## What you can automate

Game Status changes when a game starts, when someone scores and when it ends, not on every play, so it makes a good trigger and keeps your history small. Some ideas:

- Flash the room lights or play a sound when Game Status changes during your team's game.
- Turn the panel on before kickoff and off at bedtime with the panel's light, or turn its brightness down at night.
- Switch **Team** or **Mode** for the evening's game.
- Install updates on your own schedule with the **Firmware** entity.

If you want room lighting to celebrate with the score, the separate [gameday-matrix](https://github.com/bharvey88/gameday-matrix) project does the same game tracking through a Home Assistant blueprint, using the same splash rules.

## On your network

The panel treats your home network as trusted: Home Assistant, the app and the device page all connect without a password. Anyone on your Wi-Fi can change its settings. Don't expose it to the internet.
