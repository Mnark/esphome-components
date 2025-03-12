import esphome.config_validation as cv
import esphome.codegen as cg
from esphome.const import CONF_ID, CONF_PORT, CONF_AUTH, CONF_PASSWORD
from esphome.components import esp32
from ..esp32_camera import ESP32Camera
from ..sdmmc import CONF_SDMMC, SDMMC, CONF_SDMMC_ID
import base64

CODEOWNERS = ["@mnark"]
DEPENDENCIES = ["sdmmc", "esp32", "esp32_camera"]
MULTI_CONF = True
CONF_HOME_PAGE = "home_page"
CONF_SHARE_NAME = "share_name"
CONF_CAMERA = "camera"
CONF_USER = "user"
CONF_WEB_ENABLED = "enable_web"
CONF_WEB_DIR = "web_directory"

webdav_ns = cg.esphome_ns.namespace("webdav")
WebDav = webdav_ns.class_("WebDav", cg.Component)
WebDavAuthentication = webdav_ns.enum("WebDavAuthentication")

ENUM_AUTHENTICATION = {
    "NONE": WebDavAuthentication.NONE,
    "BASIC": WebDavAuthentication.BASIC,
}

# Schema definition with all possible fields
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(WebDav),
        cv.Required(CONF_PORT): cv.port,
        cv.Required(CONF_SDMMC): cv.use_id(SDMMC),
        cv.Optional(CONF_CAMERA): cv.use_id(ESP32Camera),
        cv.Optional(CONF_AUTH, default="NONE"): cv.enum(
            ENUM_AUTHENTICATION, upper=True
        ),
        cv.Optional(CONF_WEB_ENABLED, default=True): cv.boolean,
        cv.Optional(CONF_WEB_DIR, default="www"): cv.string,        
        cv.Optional(CONF_HOME_PAGE, default="default.htm"): cv.string,
        cv.Optional(CONF_SHARE_NAME, default="share"): cv.string,
        # Define user/pass as optional initially
        cv.Optional(CONF_USER): cv.string,
        cv.Optional(CONF_PASSWORD): cv.string,
    }
).extend(cv.COMPONENT_SCHEMA)

# Validation function to enforce user/pass when auth is BASIC
def validate_auth(value):
    if value[CONF_AUTH] == "BASIC":
        if CONF_USER not in value or CONF_PASSWORD not in value:
            raise cv.Invalid("When AUTH is 'BASIC', 'user' and 'password' must be provided.")
    else:
        # If AUTH is "NONE", user/pass should NOT be included
        if CONF_USER in value or CONF_PASSWORD in value:
            raise cv.Invalid("'user' and 'password' should not be set when AUTH is 'NONE'.")
    return value

# Apply validation
CONFIG_SCHEMA = cv.All(CONFIG_SCHEMA, validate_auth)

async def to_code(config):
    print (config)
    cg.add_library(
    name="tinyxml2",
    repository="https://github.com/leethomason/tinyxml2",
    version=None,
    )
    cg.add_library(
    name="tiny-json",
    repository="https://github.com/rafagafe/tiny-json",
    version=None,
    )
    server = cg.new_Pvariable(config[CONF_ID])
    cg.add(server.set_port(config[CONF_PORT]))
    sd = await cg.get_variable(config.get(CONF_SDMMC))
    cg.add(server.set_sdmmc(sd))
    cg.add(server.set_auth(config[CONF_AUTH]))
    cg.add(server.set_share_name(config[CONF_SHARE_NAME]))
    if config[CONF_AUTH] == "BASIC":
        auth_string = config[CONF_USER] + ":" + config[CONF_PASSWORD]
        auth_string_bytes = auth_string.encode("ascii")
        base64_bytes = base64.b64encode(auth_string_bytes)
        base64_string = base64_bytes.decode("ascii")
        cg.add(server.set_auth_credentials(base64_string))

    if CONF_CAMERA in config:
        print("Camera configured")
        cg.add_define("WEBDAV_ENABLE_CAMERA")
        camera = await cg.get_variable(config.get(CONF_CAMERA))
        cg.add(server.set_camera(camera))


    cg.add(server.set_web_enabled(config[CONF_WEB_ENABLED]))
    if config[CONF_WEB_ENABLED] == True: 
        cg.add(server.set_home_page(config[CONF_HOME_PAGE]))
        cg.add(server.set_web_directory(config[CONF_WEB_DIR]))
    await cg.register_component(server, config)