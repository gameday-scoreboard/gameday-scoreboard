# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 Brandon Harvey. Game Day Scoreboard firmware; see NOTICE.md.
"""saved_networks: the panel remembers up to five Wi-Fi networks instead of
ESPHome's one, and tries the last one that worked first."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@bharvey88"]
DEPENDENCIES = ["wifi"]

saved_networks_ns = cg.esphome_ns.namespace("saved_networks")
SavedNetworks = saved_networks_ns.class_("SavedNetworks", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SavedNetworks),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    # The gameday component serves the list over /gameday/wifi when this is built in.
    cg.add_define("USE_GAMEDAY_SAVED_NETWORKS")
    # ESPHome's first scan starts before this component hands over the list,
    # while its one slot is the only network. With one network and no
    # USE_WIFI_MULTI_SSID it has the driver keep only that name's access
    # points, so the other saved networks are not found until a second scan.
    # wifi defines this itself when the YAML lists more than one network.
    cg.add_define("USE_WIFI_MULTI_SSID")
