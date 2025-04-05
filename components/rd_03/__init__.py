import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.automation as auto
from esphome import pins
from esphome.const import (
    CONF_ID,
    CONF_TX_PIN,
    CONF_RX_PIN,
    CONF_NAME,
    CONF_DEVICE_CLASS,
    DEVICE_CLASS_MOTION,
    
)

from esphome.core import CORE
from esphome.components import sensor, binary_sensor
from esphome.cpp_helpers import setup_entity
from esphome.components import esp32

AUTO_LOAD = ["sensor", "binary_sensor"]

CODEOWNERS = ["@mnark"]

rd03_ns = cg.esphome_ns.namespace("rd03")
RD03 = rd03_ns.class_("RD03", sensor.Sensor, binary_sensor.BinarySensor, cg.Component)

CONF_TX_PIN = "tx_pin"
CONF_RX_PIN = "rx_pin"
CONF_RD03 = "rd03"
CONF_RD03_ID = "rd03_id"
ICON_RADAR = "mdi:radar"
ICON_MOTION = "mdi:motion-sensor"
UNIT_CM = "cm"
CONF_MOTION_SENSOR = "motion_sensor"
CONF_RANGE_SENSOR = "range_sensor"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(RD03),
        cv.Required(CONF_TX_PIN): pins.internal_gpio_input_pin_number,
        cv.Required(CONF_RX_PIN): pins.internal_gpio_input_pin_number,
        cv.Optional(
            CONF_MOTION_SENSOR,
            default={ CONF_NAME: "RD03 Motion Sensor",
            CONF_DEVICE_CLASS: DEVICE_CLASS_MOTION,
            }
        ): binary_sensor.binary_sensor_schema(
            icon=ICON_MOTION,
        ),
        cv.Optional(
            CONF_RANGE_SENSOR,
            default={ CONF_NAME: "RD03 Range Sensor",}
        ): sensor.sensor_schema(
            unit_of_measurement=UNIT_CM,
            icon=ICON_RADAR,
            accuracy_decimals=0,
            #state_class=STATE_CLASS_MEASUREMENT,
        ),       
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_tx_pin(config[CONF_TX_PIN]))
    cg.add(var.set_rx_pin(config[CONF_RX_PIN]))

    motion_sensor = await binary_sensor.new_binary_sensor(config.get(CONF_MOTION_SENSOR))
    cg.add(var.set_motion_sensor(motion_sensor))

    range_sensor = await sensor.new_sensor(config.get(CONF_RANGE_SENSOR))
    cg.add(var.set_range_sensor(range_sensor))