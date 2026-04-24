import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, sensor, spi, text_sensor
from esphome.const import CONF_CS_PIN
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_POWER,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
    UNIT_SECOND,
    UNIT_WATT,
)

DEPENDENCIES = ["spi"]
AUTO_LOAD = ["sensor", "binary_sensor", "text_sensor"]
MULTI_CONF = False

CONF_MAINS_VOLTAGE = "mains_voltage"
CONF_PREFERRED_TX_ID = "preferred_tx_id"
CONF_PUBLISH_RAW_BYTES = "publish_raw_bytes"
CONF_GDO0_PIN = "gdo0_pin"
CONF_GDO2_PIN = "gdo2_pin"
CONF_CURRENT = "current"
CONF_POWER = "power"
CONF_INTERVAL = "interval"
CONF_PAIRING = "pairing"
CONF_TX_ID = "tx_id"
CONF_BATTERY_STATE = "battery_state"
CONF_RAW_BYTES = "raw_bytes"


EfergyCc1101Ns = cg.esphome_ns.namespace("efergy_cc1101")
EfergyCc1101Component = EfergyCc1101Ns.class_("EfergyCc1101Component", cg.Component, spi.SPIDevice)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EfergyCc1101Component),
        cv.Optional(CONF_MAINS_VOLTAGE, default=230.0): cv.float_range(min=1.0, max=300.0),
        cv.Optional(CONF_PREFERRED_TX_ID, default="auto"): cv.string,
        cv.Optional(CONF_PUBLISH_RAW_BYTES, default=False): cv.boolean,
        cv.Optional(CONF_GDO0_PIN, default=4): cv.int_range(min=0, max=48),
        cv.Optional(CONF_GDO2_PIN, default=27): cv.int_range(min=0, max=48),
        cv.Optional(CONF_CURRENT): sensor.sensor_schema(
            unit_of_measurement=UNIT_AMPERE,
            accuracy_decimals=3,
            device_class=DEVICE_CLASS_CURRENT,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_POWER): sensor.sensor_schema(
            unit_of_measurement=UNIT_WATT,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_INTERVAL): sensor.sensor_schema(
            unit_of_measurement=UNIT_SECOND,
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_PAIRING): binary_sensor.binary_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_TX_ID): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_BATTERY_STATE): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_RAW_BYTES): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
).extend(cv.COMPONENT_SCHEMA).extend(spi.spi_device_schema(cs_pin_required=True, default_data_rate="4MHz", default_mode="mode0"))

FINAL_VALIDATE_SCHEMA = spi.final_validate_device_schema(
    "efergy_cc1101", require_miso=True, require_mosi=True
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)

    cg.add(var.set_mains_voltage(config[CONF_MAINS_VOLTAGE]))
    cg.add(var.set_preferred_tx_id(config[CONF_PREFERRED_TX_ID]))
    cg.add(var.set_publish_raw_bytes(config[CONF_PUBLISH_RAW_BYTES]))
    cg.add(var.set_gdo0_pin(config[CONF_GDO0_PIN]))
    cg.add(var.set_gdo2_pin(config[CONF_GDO2_PIN]))

    if CONF_CURRENT in config:
        sens = await sensor.new_sensor(config[CONF_CURRENT])
        cg.add(var.set_current_sensor(sens))

    if CONF_POWER in config:
        sens = await sensor.new_sensor(config[CONF_POWER])
        cg.add(var.set_power_sensor(sens))

    if CONF_INTERVAL in config:
        sens = await sensor.new_sensor(config[CONF_INTERVAL])
        cg.add(var.set_interval_sensor(sens))

    if CONF_PAIRING in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_PAIRING])
        cg.add(var.set_pairing_sensor(sens))

    if CONF_TX_ID in config:
        sens = await text_sensor.new_text_sensor(config[CONF_TX_ID])
        cg.add(var.set_tx_id_sensor(sens))

    if CONF_BATTERY_STATE in config:
        sens = await text_sensor.new_text_sensor(config[CONF_BATTERY_STATE])
        cg.add(var.set_battery_state_sensor(sens))

    if CONF_RAW_BYTES in config:
        sens = await text_sensor.new_text_sensor(config[CONF_RAW_BYTES])
        cg.add(var.set_raw_bytes_sensor(sens))
