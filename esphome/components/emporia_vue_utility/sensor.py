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

DEPENDENCIES = ["uart"]

POWER_SENSOR_TYPES = {
    CONF_POWER: "set_power_sensor",
    CONF_POWER + "_export": "set_power_export_sensor",
    CONF_POWER + "_import": "set_power_import_sensor",
}

ENERGY_SENSOR_TYPES = {
    CONF_ENERGY: "set_energy_sensor",
    CONF_ENERGY + "_export": "set_energy_export_sensor",
    CONF_ENERGY + "_import": "set_energy_import_sensor",
}

ALL_SENSOR_TYPES = {**POWER_SENSOR_TYPES, **ENERGY_SENSOR_TYPES}

emporia_vue_utility_ns = cg.esphome_ns.namespace('emporia_vue_utility')
EmporiaVueUtility = emporia_vue_utility_ns.class_('EmporiaVueUtility', cg.PollingComponent, uart.UARTDevice)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(EmporiaVueUtility),
            **{
                cv.Optional(name): sensor.sensor_schema(
                    unit_of_measurement=UNIT_WATT,
                    device_class=DEVICE_CLASS_POWER,
                    state_class=STATE_CLASS_MEASUREMENT,
                    accuracy_decimals=0,
                )
                for name in POWER_SENSOR_TYPES
            },
            **{
                cv.Optional(name): sensor.sensor_schema(
                    unit_of_measurement=UNIT_WATT_HOURS,
                    device_class=DEVICE_CLASS_ENERGY,
                    state_class=STATE_CLASS_TOTAL_INCREASING,
                    accuracy_decimals=0,
                )
                for name in ENERGY_SENSOR_TYPES
            }
        }
    )
    .extend(cv.polling_component_schema('30s'))
    .extend(uart.UART_DEVICE_SCHEMA)
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    for key, funcName in ALL_SENSOR_TYPES.items():
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(getattr(var, funcName)(sens))