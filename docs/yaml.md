# YAML configuration

## Introduction

ESPHome comes with a [large library of components](https://esphome.io/components/) and configuration options, which translates to features and functionality in the firmware.  Some people want some features, others won't.

The example YAMLs should work out of the box, provided you fill in the necessary secrets.  This section is to help get you started on customizing.  You may want to do this to add features, or take some away to make it leaner.

### Examples

- The [Web Server](https://esphome.io/components/web_server/) component adds a browser-accessible web UI with sensors and logs. Neat, but it duplicates functionality already in Home Assistant, and consumes a lot of memory on the device. If you won’t use it, disable it.
- The [WiFi Signal Strength](https://esphome.io/components/sensor/wifi_signal/) component creates a sensor in HA showing the devices Wi-Fi strength.  You can also see this in the device logs.  Handy for troubleshooting but once the connection is stable, you may not need an extra entity cluttering HA.
- The [Uptime Sensor](https://esphome.io/components/sensor/uptime/?utm_source=chatgpt.com) reports how long the device has been running since the last reboot. Handy for debugging stability (e.g. “is my device randomly restarting?”). But if you don’t actively monitor it, it’s more HA clutter.
- [MQTT](https://esphome.io/components/mqtt/) is a powerful but more complex alternative to HA’s native ESPHome API.  It is essential for some setups, but if you don’t already use MQTT, you can remove it.


## Vue configuration

### Sensors

This Vue sensor component provides **6** output values and takes **3** configuration options.

A full configuration can look like this:

```yaml
# Boilerplate UART configuration.
uart:
  id: emporia_uart
  rx_pin: GPIO21
  tx_pin: GPIO22
  baud_rate: 115200

# The actual sensor configuration.
sensor:
  - platform: emporia_vue_utility
    uart_id: emporia_uart
    debug: true
    update_interval: 15s
    power:
      name: '${name} Watts'
    power_export:
      name: '${name} Watts Returned'
    power_import:
      name: '${name} Watts Consumed'
    energy:
      name: '${name} Wh Net'
    energy_export:
      name: '${name} Wh Returned'
    energy_import:
      name: '${name} Wh Consumed'
```

You likely do not need all of these fields, and you can remove (or comment out) the sensors you do not need.


#### UART

We need to talk to the MGM chip that talks to the utility meter. This chip is connected to the ESP via `GPIO21` and `GPIO22` and the configuration is:

```yaml
uart:
  id: emporia_uart
  rx_pin: GPIO21
  tx_pin: GPIO22
  baud_rate: 115200
```

Then, within the `sensor:` configuration, you can add `uart_id: emporia_uart`. I believe it is optional if there is only one `uart:` defined.

Example with `uart_id` added:

```yaml
sensor:
  - platform: emporia_vue_utility
    uart_id: emporia_uart
    ...
```

#### Poll rate

The `update_interval` lets you define the rate at which we ask the utility meter for a new reading. This defaults to `30` seconds, which is also the default in the Emporia stock firmware.

Note that the utility meter has its own update rate, and if you poll more frequently it will just give you the same value as before. Some users report being able to receive new values every 5 seconds while I only see a new value every 15 seconds. You are welcome to trial-and-error and see how frequently you can get updated values and settle on a value that makes sense to you.

Example with `update_interval` added:

```yaml
sensor:
  - platform: emporia_vue_utility
    update_interval: 15s
    ...
```

#### Debug logs

Add `debug: true` if you need to see additional details about issues you're experiencing.

When enabled, each reading will cause the log to output something like this:

```
[00:05:17][D][emporia_vue_utility:360]: Meter Cost Unit: 1000
[00:05:17][D][emporia_vue_utility:361]: Meter Divisor: 1
[00:05:17][D][emporia_vue_utility:362]: Meter Energy Import Flags: 00596680
[00:05:17][D][emporia_vue_utility:363]: Meter Energy Export Flags: 0078f478
[00:05:17][D][emporia_vue_utility:364]: Meter Power Flags: 00038d2a
[00:05:17][D][emporia_vue_utility:365]: Meter Import Energy: 5858.944kWh
[00:05:17][D][emporia_vue_utility:366]: Meter Export Energy: 7926.904kWh
[00:05:17][D][emporia_vue_utility:367]: Meter Net Energy: -2067.960kWh
[00:05:17][D][emporia_vue_utility:368]: Meter Power:  909W
[00:05:17][D][emporia_vue_utility:377]: Meter Response Bytes   0 to   3: 18 91 01 00
[00:05:17][D][emporia_vue_utility:377]: Meter Response Bytes   4 to   7: 00 00 25 80
[00:05:17][D][emporia_vue_utility:377]: Meter Response Bytes   8 to  11: 66 59 00 00
[00:05:17][D][emporia_vue_utility:377]: Meter Response Bytes  12 to  15: 00 01 00 00
[00:05:17][D][emporia_vue_utility:377]: Meter Response Bytes  16 to  19: 25 78 f4 78
[00:05:17][D][emporia_vue_utility:377]: Meter Response Bytes  20 to  23: 00 00 00 01
[00:05:17][D][emporia_vue_utility:377]: Meter Response Bytes  24 to  27: 03 00 22 01
[00:05:17][D][emporia_vue_utility:377]: Meter Response Bytes  28 to  31: 00 00 02 03
[00:05:17][D][emporia_vue_utility:377]: Meter Response Bytes  32 to  35: 00 22 e8 03
[00:05:17][D][emporia_vue_utility:377]: Meter Response Bytes  36 to  39: 00 00 04 00
[00:05:17][D][emporia_vue_utility:377]: Meter Response Bytes  40 to  43: 2a 8d 03 00
```

Example with `debug` added:

```yaml
sensor:
  - platform: emporia_vue_utility
    debug: true
    ...
```