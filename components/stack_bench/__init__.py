"""stack_bench: bench-only stack watermarks. Not for a release build.

Keeps every FreeRTOS task's lowest free stack (sampled once a second) and
scoped meters around the suspects in the gameday component, in RTC memory so
the numbers survive a restart. GET /bench/stack prints them.
"""

import esphome.codegen as cg
from esphome.components import logger, web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["network", "logger", "lvgl"]
AUTO_LOAD = ["web_server_base"]

stack_bench_ns = cg.esphome_ns.namespace("stack_bench")
StackBench = stack_bench_ns.class_("StackBench", cg.Component)
StackBenchEarly = stack_bench_ns.class_("StackBenchEarly", cg.Component)

CONF_EARLY_ID = "early_id"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(StackBench),
        cv.GenerateID(CONF_EARLY_ID): cv.declare_id(StackBenchEarly),
        cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    # Two log callbacks (one before the web server's and API's, one after);
    # the logger sizes its listener list from these requests.
    logger.request_log_listener()
    logger.request_log_listener()
    early = cg.new_Pvariable(config[CONF_EARLY_ID])
    await cg.register_component(early, {})
    base = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    var = cg.new_Pvariable(config[CONF_ID], base)
    await cg.register_component(var, config)
    cg.add_define("USE_STACK_BENCH")
