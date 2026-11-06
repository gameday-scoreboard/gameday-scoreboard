"""names_wall: a list of names the panel pages through (backlog #152 spike).

Throwaway. The list arrives as a POST to /names (one name a line, a CSV's
first column, or a JSON array) or is fetched from a URL on a timer, and the
page in firmware/pages/names-wall.yaml draws it."""

import esphome.codegen as cg
from esphome.components import http_request, web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["network", "http_request"]
AUTO_LOAD = ["web_server_base"]
CODEOWNERS = ["@bharvey88"]

CONF_HTTP_REQUEST_ID = "http_request_id"

names_wall_ns = cg.esphome_ns.namespace("names_wall")
NamesWall = names_wall_ns.class_("NamesWall", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(NamesWall),
        cv.GenerateID(CONF_HTTP_REQUEST_ID): cv.use_id(
            http_request.HttpRequestComponent
        ),
        cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(
            web_server_base.WebServerBase
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_http(await cg.get_variable(config[CONF_HTTP_REQUEST_ID])))
    cg.add(
        var.set_web_server_base(
            await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
        )
    )
