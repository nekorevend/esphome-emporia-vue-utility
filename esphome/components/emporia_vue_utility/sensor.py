import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, sensor
from esphome.const import CONF_ID, CONF_POWER

DEPENDENCIES = ['uart']

emporia_vue_utility_ns = cg.esphome_ns.namespace('emporia_vue_utility')
EmporiaVueUtility = emporia_vue_utility_ns.class_('EmporiaVueUtility', cg.PollingComponent, uart.UARTDevice)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        cv.GenerateID(): cv.declare_id(EmporiaVueUtility),
        cv.Optional(CONF_POWER): sensor.sensor_schema(
            unit_of_measurement=UNIT_WATT,
            device_class=DEVICE_CLASS_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
            accuracy_decimals=2,
    ),
    )
)

# sensor.sensor_schema(UNIT_EMPTY, ICON_EMPTY, 1).extend({
#     cv.GenerateID(): cv.declare_id(EmporiaVueUtility),
# }).extend(cv.polling_component_schema('60s')).extend(uart.UART_DEVICE_SCHEMA)

# def to_code(config):
#     var = cg.new_Pvariable(config[CONF_ID])
#     yield cg.register_component(var, config)
#     yield sensor.register_sensor(var, config)
#     yield uart.register_uart_device(var, config)
