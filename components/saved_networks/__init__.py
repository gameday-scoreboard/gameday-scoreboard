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
