"""crash_report: keeps the last crash's backtrace in flash, so it survives an
unplug and can be read over the network at any time from /gameday/crash."""

import esphome.codegen as cg
from esphome.components import logger
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@bharvey88"]
DEPENDENCIES = ["esp32", "logger"]
AUTO_LOAD = ["json"]

CONF_VARIANT = "variant"

crash_report_ns = cg.esphome_ns.namespace("crash_report")
CrashReport = crash_report_ns.class_("CrashReport", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(CrashReport),
        # The manifest folder this build updates from (moonhub75, scoreboard75),
        # so a script can find the matching ELF in the release.
        cv.Optional(CONF_VARIANT, default=""): cv.string,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_variant(config[CONF_VARIANT]))
    # Reads the crash handler's report lines as they are logged.
    logger.request_log_listener()
    # The gameday component serves the report over /gameday/crash when this is built in.
    cg.add_define("USE_GAMEDAY_CRASH_REPORT")
