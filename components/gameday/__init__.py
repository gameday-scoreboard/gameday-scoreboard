"""gameday: polls ESPN for one team's football game and hands the page
the same fields the Home Assistant blueprint used to push."""

import re
from pathlib import Path

from esphome import automation
import esphome.codegen as cg
from esphome.components import http_request, number, runtime_image, time, web_server_base
from esphome.components.http_request.update import HttpRequestUpdate
from esphome.components.panel_layout import PanelLayout
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_TIME_ID, CONF_TRIGGER_ID, __version__ as ESPHOME_VERSION


DEPENDENCIES = ["network", "http_request", "time"]
AUTO_LOAD = ["json", "runtime_image", "select", "web_server_base"]
CODEOWNERS = ["@bharvey88"]

CONF_HTTP_REQUEST_ID = "http_request_id"
CONF_PANEL_LAYOUT_ID = "panel_layout_id"
# The Brightness and Scroll Speed numbers, for the settings backup (settings_doc.h)
CONF_BRIGHTNESS_ID = "brightness_id"
CONF_SCROLL_SPEED_ID = "scroll_speed_id"
CONF_ON_UPDATE = "on_update"
CONF_ON_ACTION = "on_action"
# The game events of the game Home Assistant follows (ha_events.h). The
# component sends them to Home Assistant and the webhook itself; these
# triggers are for a build's own YAML: scores, finals, or every event.
CONF_ON_SCORE = "on_score"
CONF_ON_FINAL = "on_final"
CONF_ON_GAME_EVENT = "on_game_event"
# The demo build (firmware/gameday-demo.yaml): a looping scripted game the
# remote and the device page start and stop. Off in release firmware.
CONF_DEMO = "demo"
# The bar build (firmware/gameday-bar.yaml): real firmware that installs from
# the beta channel on its own. Off in release firmware.
CONF_BAR = "bar"
CONF_REPORT_EMAIL = "report_email"
# "Follow beta" on the device page (release builds): points the update entity
# at the beta manifest instead of the release one. The bar build is always on
# the bar channel and the demo build never updates, so neither has it.
CONF_BETA_CHANNEL = "beta_channel"
CONF_UPDATE_ID = "update_id"
CONF_STABLE = "stable"
CONF_BETA = "beta"

gameday_ns = cg.esphome_ns.namespace("gameday")
GamedayComponent = gameday_ns.class_("GamedayComponent", cg.Component)
UpdateFields = gameday_ns.struct("UpdateFields")
UpdateFieldsConstRef = UpdateFields.operator("const").operator("ref")
UpdateTrigger = gameday_ns.class_(
    "UpdateTrigger", automation.Trigger.template(UpdateFieldsConstRef)
)
ActionTrigger = gameday_ns.class_(
    "ActionTrigger", automation.Trigger.template(cg.std_string)
)
HaEventConstRef = (
    cg.global_ns.namespace("espn").struct("HaEvent").operator("const").operator("ref")
)
ScoreTrigger = gameday_ns.class_(
    "ScoreTrigger", automation.Trigger.template(HaEventConstRef)
)
FinalTrigger = gameday_ns.class_(
    "FinalTrigger", automation.Trigger.template(HaEventConstRef)
)
GameEventTrigger = gameday_ns.class_(
    "GameEventTrigger", automation.Trigger.template(HaEventConstRef)
)

COMPONENT_DIR = Path(__file__).resolve().parent

_TZ_RE = re.compile(r'\{"([^"]+)",\s*"([^"]+)",\s*"([^"]+)"\}')


def timezone_options():
    text = (COMPONENT_DIR / "timezones.h").read_text(encoding="utf-8")
    text = "\n".join(ln for ln in text.splitlines() if not ln.lstrip().startswith("//"))
    opts = [name for name, _posix, _iana in _TZ_RE.findall(text)]
    if not opts:
        raise cv.Invalid("timezones.h has no entries")
    return opts


def _parsed_timezone_count():
    text = (COMPONENT_DIR / "timezones_parsed.h").read_text(encoding="utf-8")
    return len(re.findall(r"^\s*\{-?\d+, -?\d+, \{", text, re.M))


def _esphome_at_least(year, month):
    m = re.match(r"(\d+)\.(\d+)", ESPHOME_VERSION)
    return bool(m) and (int(m.group(1)), int(m.group(2))) >= (year, month)


def _not_demo_and_bar(config):
    if config[CONF_DEMO] and CONF_BAR in config:
        raise cv.Invalid("the demo build never updates itself; it can't be a bar build too")
    if CONF_BETA_CHANNEL in config and (config[CONF_DEMO] or CONF_BAR in config):
        raise cv.Invalid("beta_channel is for release builds; remove it in the demo and bar builds")
    return config


CONFIG_SCHEMA = cv.All(cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(GamedayComponent),
        cv.GenerateID(CONF_HTTP_REQUEST_ID): cv.use_id(
            http_request.HttpRequestComponent
        ),
        cv.GenerateID(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
        cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(
            web_server_base.WebServerBase
        ),
        cv.Optional(CONF_PANEL_LAYOUT_ID): cv.use_id(PanelLayout),
        cv.Optional(CONF_BRIGHTNESS_ID): cv.use_id(number.Number),
        cv.Optional(CONF_SCROLL_SPEED_ID): cv.use_id(number.Number),
        cv.Optional(CONF_DEMO, default=False): cv.boolean,
        cv.Optional(CONF_BAR): cv.Schema(
            {cv.Required(CONF_REPORT_EMAIL): cv.string_strict}
        ),
        cv.Optional(CONF_BETA_CHANNEL): cv.Schema(
            {
                cv.Required(CONF_UPDATE_ID): cv.use_id(HttpRequestUpdate),
                cv.Required(CONF_STABLE): cv.url,
                cv.Required(CONF_BETA): cv.url,
            }
        ),
        cv.Optional(CONF_ON_UPDATE): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(UpdateTrigger)}
        ),
        cv.Optional(CONF_ON_ACTION): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ActionTrigger)}
        ),
        cv.Optional(CONF_ON_SCORE): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ScoreTrigger)}
        ),
        cv.Optional(CONF_ON_GAME_EVENT): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(GameEventTrigger)}
        ),
        cv.Optional(CONF_ON_FINAL): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(FinalTrigger)}
        ),
    }
).extend(cv.COMPONENT_SCHEMA), _not_demo_and_bar)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    if _parsed_timezone_count() != len(timezone_options()):
        raise cv.Invalid(
            "timezones_parsed.h is out of date; run scripts/build_timezones.py"
        )
    if _esphome_at_least(2026, 9):
        # 2026.9 removed RealTimeClock::set_timezone and the on-device POSIX
        # parser; the clock takes a pre-parsed struct instead.
        cg.add_define("GAMEDAY_TZ_PARSED")
    if config[CONF_DEMO]:
        cg.add_define("GAMEDAY_DEMO")
    if CONF_BAR in config:
        cg.add_define("GAMEDAY_BAR")
        cg.add(var.set_report_email(config[CONF_BAR][CONF_REPORT_EMAIL]))
    if CONF_BETA_CHANNEL in config:
        conf = config[CONF_BETA_CHANNEL]
        cg.add_define("GAMEDAY_BETA_CHANNEL")
        cg.add(
            var.set_beta_channel(
                await cg.get_variable(conf[CONF_UPDATE_ID]),
                conf[CONF_STABLE],
                conf[CONF_BETA],
            )
        )
    runtime_image.enable_format("PNG")  # logo_cache.cpp decodes ESPN's PNG logos
    cg.add(var.set_http(await cg.get_variable(config[CONF_HTTP_REQUEST_ID])))
    cg.add(var.set_time(await cg.get_variable(config[CONF_TIME_ID])))
    cg.add(
        var.set_web_server_base(
            await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
        )
    )
    if CONF_PANEL_LAYOUT_ID in config:
        cg.add(
            var.set_panel_layout(await cg.get_variable(config[CONF_PANEL_LAYOUT_ID]))
        )
    if CONF_BRIGHTNESS_ID in config:
        cg.add(var.set_brightness_number(await cg.get_variable(config[CONF_BRIGHTNESS_ID])))
    if CONF_SCROLL_SPEED_ID in config:
        cg.add(var.set_scroll_number(await cg.get_variable(config[CONF_SCROLL_SPEED_ID])))
    for conf in config.get(CONF_ON_UPDATE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger, [(UpdateFieldsConstRef, "x")], conf
        )
    for conf in config.get(CONF_ON_ACTION, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "x")], conf)
    for key in (CONF_ON_SCORE, CONF_ON_FINAL, CONF_ON_GAME_EVENT):
        for conf in config.get(key, []):
            trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
            await automation.build_automation(trigger, [(HaEventConstRef, "x")], conf)
