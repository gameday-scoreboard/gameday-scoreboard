---
title: "Smart board"
description: "Up to four rows of your own live text on the panel, usually from Home Assistant: any entity's state or attribute, in the colors you pick."
sidebar:
  order: 13.5
---

The **Smart board** puts up to four rows of your own text on the panel in place of the scoreboard: the weather, whether the front door is locked, the air quality, the alarm. The rows usually come from [Home Assistant](/using/home-assistant/), so a row can show any entity's state or attribute, worked out by a template, and change the moment the thing it shows does. It is the fourth page under **Showing**, after the Scoreboard, the Clock and Effects.

## Turning it on

There are two ways, depending on whether you want it all the time or between games.

- **All the time:** set **Showing** to **Smart board** on the [device page](/using/device-page/)'s Home tab, or **Select Page** in Home Assistant. It stays up, through restarts and power cuts too, until somebody picks another page there. Your games don't take it over, and while it's up the panel stops reading scores and sends Home Assistant nothing about games. Pick the Scoreboard again and it catches up in a second or two.
- **Between games:** set **After the game** to **Show the Smart board** (under **Screen** on the Display tab). Once your game's final has been up a while, the Smart board goes up, and the scoreboard comes back at the **Turns on** time before your next game. See [Game-day wake and after the game](/using/brightness-and-clock/#game-day-wake-and-after-the-game). This only takes over from the Scoreboard: if **Showing** is on the Clock or Effects, that stays.

Until any row has text, the page says **Smart board** and how to fill it, so the panel never sits black.

## The rows

In Home Assistant the panel has four text entities, **Smart board row 1** to **Smart board row 4**. Set one with the **Text: Set value** action, and the panel shows it right away. While the Smart board is up, the [device page](/using/device-page/)'s Home tab shows the four rows under **Showing**, in their colors. **Edit** or **Add** on a row takes the words and a color picked from the swatches, so there are no tags to type. Home Assistant writes over a row it fills the next time it updates it.

The rows you fill share the panel's height, top to bottom, so fewer rows get bigger text: one row is big, four rows are the panel's usual size. An empty row takes no room. A row too wide for the panel first tries the next size down, and if it still doesn't fit, it scrolls.

A row can be up to 255 characters, color tags included. The panel draws letters, numbers and accented letters; emoji are left out.

## The blueprint

The quickest way to fill the rows is the Smart board blueprint: [import it into Home Assistant](https://my.home-assistant.io/redirect/blueprint_import/?blueprint_url=https%3A%2F%2Fdocs.gameday-scoreboard.com%2Fblueprints%2Fsmart-board.yaml), then create an automation from it. For each row, pick the panel's row entity, write the text as a template, and pick a color. Under **When to send**, list the entities your rows show, so a row changes the moment they do. The blueprint also sends the rows every minute (you can turn that off), when Home Assistant starts, and when the panel comes back online, and it only sends a row that changed.

## Colors

Start a row with a color in square brackets to color it:

- `[red]Front Door Unlocked`
- `[#FF6400]63°F Partly Cloudy`

The names are **red**, **orange**, **yellow**, **green**, **cyan**, **blue**, **purple**, **pink**, **white** and **gray**, in any case. `[#RRGGBB]` takes any color in hex. A row with no tag is white.

A tag later in a row colors the words after it: `CO2 766ppm [yellow]AQI 62` shows the air quality in yellow and the rest white. Brackets that aren't a color, like `[1]`, show as written.

One limit: a row that changes color partway can't show a `#` of its own (`Room #2` shows as `Room 2`). A row in one color shows `#` as usual.

## A row that changes with its state

Because the color is part of the text, one template can set both. This row is green while the door is locked and red when it isn't:

```yaml
action: text.set_value
target:
  entity_id: text.living_room_panel_smart_board_row_2
data:
  value: >-
    {{ '[green]' if is_state('lock.front_door', 'locked') else '[red]' }}Front Door
    {{ states('lock.front_door') | title }}
```

Use the entity ids your panel shows in Home Assistant. Templates can read attributes too, like `state_attr('weather.home', 'temperature')`.

Set the rows when the things they show change, and also when the panel comes back online: the panel doesn't keep its rows through a restart, so it waits for Home Assistant to send them again. A trigger on a row entity changing from **unavailable** covers that.

## When Home Assistant goes quiet

The rows are only as fresh as what Home Assistant last sent. If Home Assistant has been disconnected from the panel for two minutes (it's restarting, or down), the rows turn gray and a small orange dot lights in the top right corner, so an old "Front Door Locked" doesn't look current. They turn back to color the moment Home Assistant is back.

A panel that hasn't talked to Home Assistant since it started, with rows typed on the device page, never turns gray.

## What still shows over it

The Smart board is a page like the Scoreboard, so the screens that tell you something show over it and then go away again, leaving the Smart board up: Wi-Fi lost and **No internet** (see [Panel messages](/help/panel-messages/)), the heat or power mark, a [banner](/using/messages/) you send, a firmware install, and the [night schedule](/using/brightness-and-clock/#night-schedule)'s dimming and screen-off. **Showing** stays on **Smart board** through all of them.
