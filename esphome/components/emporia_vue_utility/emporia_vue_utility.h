#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"

// Extra meter reading response debugging
#define DEBUG_VUE_RESPONSE true

// If the instant watts being consumed meter reading is outside of these ranges,
// the sample will be ignored which helps prevent garbage data from polluting
// home assistant graphs.  Note this is the instant watts value, not the
// watt-hours value, which has smarter filtering.  The defaults of 131kW
// should be fine for most people.  (131072 = 0x20000)
#define WATTS_MIN -131072 
#define WATTS_MAX  131072

// How much the watt-hours consumed value can change between samples.
// Values that change by more than this over the avg value across the
// previous 5 samples will be discarded.
#define MAX_WH_CHANGE 2000

// How many samples to average the watt-hours value over. 
#define MAX_WH_CHANGE_ARY 5

// How often to request a reading from the meter in seconds.
// Meters typically update the reported value only once every
// 10 to 30 seconds, so "5" is usually fine.
// You might try setting this to "1" to see if your meter has
// new values more often
#define METER_READING_INTERVAL 30

// How often to attempt to re-join the meter when it hasn't
// been returning readings
#define METER_REJOIN_INTERVAL 30

// On first startup, how long before trying to start to talk to meter
#define INITIAL_STARTUP_DELAY 10

// Should this code manage the "wifi" and "link" LEDs?
// set to false if you want manually manage them elsewhere
#define USE_LED_PINS true

#define LED_PIN_LINK 32
#define LED_PIN_WIFI 33

namespace esphome {
namespace emporia_vue_utility {

class EmporiaVueUtility : public sensor::Sensor, public PollingComponent, public uart::UARTDevice {
 public:
  void setup() override;
  void update() override;
  void loop() override;
  void dump_config() override;
};

}  // namespace emporia_vue_utility
}  // namespace esphome