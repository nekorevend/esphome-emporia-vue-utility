import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, sensor
from esphome.const import (
    CONF_ID,
    CONF_POWER,
    CONF_ENERGY,
    UNIT_WATT,
    UNIT_WATT_HOURS,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_ENERGY,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING
)

DEPENDENCIES = ['uart']

SENSOR_OPTIONS = ["", "_export", "_import"]

emporia_vue_utility_ns = cg.esphome_ns.namespace('emporia_vue_utility')
EmporiaVueUtility = emporia_vue_utility_ns.class_('EmporiaVueUtility', cg.PollingComponent, uart.UARTDevice)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(EmporiaVueUtility),
            **{
                cv.Optional(CONF_POWER + suffix): sensor.sensor_schema(
                    unit_of_measurement=UNIT_WATT,
                    device_class=DEVICE_CLASS_POWER,
                    state_class=STATE_CLASS_MEASUREMENT,
                    accuracy_decimals=2,
                )
                for suffix in SENSOR_OPTIONS
            },
            **{
                cv.Optional(CONF_ENERGY + suffix): sensor.sensor_schema(
                    unit_of_measurement=UNIT_WATT_HOURS,
                    device_class=DEVICE_CLASS_ENERGY,
                    state_class=STATE_CLASS_TOTAL_INCREASING,
                    accuracy_decimals=0,
                )
                for suffix in SENSOR_OPTIONS
            }
        }
    )
    .extend(cv.polling_component_schema('30s'))
    .extend(uart.UART_DEVICE_SCHEMA)
)

def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)
    yield uart.register_uart_device(var, config)