import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.automation as auto
from esphome import pins
from esphome.const import (
    CONF_ID,
    CONF_CLOCK_PIN,
    CONF_DATA_PIN,
    CONF_LENGTH,
    CONF_DATA,
    CONF_NAME,
)
from esphome.core import CORE
from esphome.components import sensor, text_sensor
from esphome.cpp_helpers import setup_entity
from esphome.components import esp32

CODEOWNERS = ["@mnark"]
AUTO_LOAD = ["text_sensor", "sensor"]

sdmmc_ns = cg.esphome_ns.namespace("sdmmc")
SDMMC = sdmmc_ns.class_("SDMMC", cg.PollingComponent)
SDMMCWriteAction = sdmmc_ns.class_("SDMMCWriteAction", auto.Action)
SDMMCAppendAction = sdmmc_ns.class_("SDMMCAppendAction", auto.Action)

CONF_COMMAND_PIN = "command_pin"
CONF_SDMMC = "sdmmc"
#CONF_SDMMC_ID = "sdmmc_id"
CONF_PATH = "path"
CONF_FILENAME = "filename"
ICON_MICRO_SD = "mdi:micro-sd"
CONF_DIAGNOSTIC = "diagnostics"
CONF_MOUNT_POINT = "mount_point"
CONF_CARD_SENSOR = "card_sensor"

def validate_raw_data(value):
    if isinstance(value, str):
        return value.encode("utf-8")
    if isinstance(value, str):
        return value
    if isinstance(value, list):
        return cv.Schema([cv.hex_uint8_t])(value)
    raise cv.Invalid(
        "data must either be a string wrapped in quotes or a list of bytes"
    )

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(SDMMC),
        #cv.GenerateID(CONF_SDMMC_ID): cv.declare_id(text_sensor.TextSensor),
        cv.Required(CONF_COMMAND_PIN): pins.internal_gpio_input_pin_number,
        cv.Required(CONF_CLOCK_PIN): pins.internal_gpio_input_pin_number,
        cv.Required(CONF_DATA_PIN): cv.All(
            cv.ensure_list(pins.internal_gpio_input_pin_number),  # Allow a list
            cv.Length(min=1, max=4),  # Must be 1 or 4 pins
        ),
        cv.Optional(CONF_MOUNT_POINT, default="sdcard"): cv.string,
        cv.Optional(CONF_NAME, default="SD Card"): cv.string,
        
        cv.Optional(
            CONF_CARD_SENSOR,
            default={
                CONF_NAME: "SD Card",
            }): text_sensor.text_sensor_schema(
            icon=ICON_MICRO_SD,
        ),
    }
).extend(cv.polling_component_schema("60s"))

async def to_code(config):
    esp32.add_idf_sdkconfig_option("CONFIG_FATFS_LONG_FILENAMES","y")
    esp32.add_idf_sdkconfig_option("CONFIG_FATFS_MAX_LFN","255")
    esp32.add_idf_sdkconfig_option("CONFIG_FATFS_LFN_HEAP","y")
    
    #var = await sensor.new_sensor(config)
    #await cg.register_component(var, config) 
    var = cg.new_Pvariable(config[CONF_ID])
    #await setup_entity(var, config)
    await cg.register_component(var, config)

    cg.add(var.set_command_pin(config[CONF_COMMAND_PIN]))
    cg.add(var.set_clock_pin(config[CONF_CLOCK_PIN]))

    # Handle single or multiple data pins
    data_pins = config[CONF_DATA_PIN]
    if len(data_pins) == 1:
        cg.add(var.set_data_pin(data_pins[0]))
    elif len(data_pins) == 4:
        cg.add(var.set_data_pins(data_pins[0], data_pins[1], data_pins[2], data_pins[3]))

    cg.add(var.set_mount_point(config[CONF_MOUNT_POINT]))

    card_sensor = await text_sensor.new_text_sensor(config.get(CONF_CARD_SENSOR))
    cg.add(var.set_card_sensor(card_sensor))
    
    cg.add_define("USE_SDMMC")

@auto.register_action(
    "sdmmc.save",
    SDMMCWriteAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(SDMMC),
            cv.Required(CONF_FILENAME): cv.templatable(cv.string),
            cv.Required(CONF_LENGTH): cv.templatable(cv.positive_int),
            cv.Required(CONF_DATA): cv.templatable(validate_raw_data), 
        }
    ),
)
async def sdmmc_save_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    template_ = await cg.templatable(config[CONF_FILENAME], args, cg.std_string )
    cg.add(var.set_filename(template_))

    template_ = await cg.templatable(config[CONF_LENGTH], args, cg.uint32 )
    cg.add(var.set_length(template_))
    
    data = config[CONF_DATA]
    if cg.is_template(data):
        print("Data is template")
        templ = await cg.templatable(data, args, None ) 
        cg.add(var.set_data_template_int(templ))
    else:
        print("Data is not template")
        if isinstance(data, bytes):
            print("Data is in bytes")
            data = [int(x) for x in data]
        cg.add(var.set_data_static(data))

    return var

@auto.register_action(
    "sdmmc.append",
    SDMMCAppendAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(SDMMC),
            cv.Required(CONF_FILENAME): cv.templatable(cv.string),
            cv.Required(CONF_LENGTH): cv.templatable(cv.positive_int),
            cv.Required(CONF_DATA): cv.templatable(validate_raw_data), 
        }
    ),
)
async def sdmmc_append_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    template_ = await cg.templatable(config[CONF_FILENAME], args, cg.std_string )
    cg.add(var.set_filename(template_))

    template_ = await cg.templatable(config[CONF_LENGTH], args, cg.uint32 )
    cg.add(var.set_length(template_))
    
    data = config[CONF_DATA]
    if cg.is_template(data):
        print("Data is template")
        templ = await cg.templatable(data, args, None ) 
        cg.add(var.set_data_template_int(templ))
    else:
        print("Data is not template")
        if isinstance(data, bytes):
            print("Data is in bytes")
            data = [int(x) for x in data]
        cg.add(var.set_data_static(data))

    return var
