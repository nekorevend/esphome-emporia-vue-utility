#pragma once

#include <chrono>
#include <cinttypes>

#include "driver/gpio.h"

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"

#include "zcl_meter_reading.h"

// If the instant watts being consumed meter reading is outside of these ranges,
// the sample will be ignored which helps prevent garbage data from polluting
// home assistant graphs.  Note this is the instant watts value, not the
// watt-hours value, which has smarter filtering.  The defaults of 131kW
// should be fine for most people.  (131072 = 0x20000)
#define WATTS_MIN -131072
#define WATTS_MAX 131072

// How much the watt-hours consumed value can change between samples.
// Values that change by more than this over the avg value across the
// previous 5 samples will be discarded.
#define MAX_WH_CHANGE 2000

// How many samples to average the watt-hours value over.
#define MAX_WH_CHANGE_ARY 5

// After this many consecutive rejected readings, assume the meter's cumulative
// baseline genuinely changed (outage, reboot, or counter rollover) and re-sync
// the moving-average filter to the current reading instead of ignoring it.
#define MAX_WH_REJECTS MAX_WH_CHANGE_ARY

// After this many consecutive unscaled (zero Multiplier/Divisor) readings with
// no usable reading in between, stop silently ignoring them and ask for a bug
// report.
#define MAX_UNSCALED_READINGS 3

// How often to attempt to re-join the meter when it hasn't
// been returning readings
#define METER_REJOIN_INTERVAL std::chrono::seconds(30)

// How often to attempt to re-request the MGM firmware version.
#define MGM_FIRMWARE_REQUEST_INTERVAL std::chrono::seconds(3)

// On first startup, how long before trying to start to talk to meter
#define INITIAL_STARTUP_DELAY std::chrono::seconds(10)

// Should this code manage the "wifi" and "link" LEDs?
// set to false if you want manually manage them elsewhere
#define USE_LED_PINS true

#define LED_PIN_LINK GPIO_NUM_32
#define LED_PIN_WIFI GPIO_NUM_33

static const char *TAG = "emporia_vue_utility";

namespace esphome {
namespace emporia_vue_utility {

class EmporiaVueUtility : public PollingComponent, public uart::UARTDevice {
 public:
  /**
   * Format known from MGM Firmware version 2.
   */
  struct MeterReadingV2 {
    char header;
    char is_resp;
    char msg_type;
    uint8_t data_len;
    uint8_t unknown0[4];     // Payload Bytes 0 to 3
    uint32_t watt_hours;  // Payload Bytes 4 to 7
    uint8_t unknown8[39];    // Payload Bytes 8 to 46
    uint8_t meter_multiplier;  // Payload Byte  47
    uint8_t unknown48[2];    // Payload Bytes 48 to 49
    uint16_t meter_divisor;   // Payload Bytes 50 to 51
    uint8_t maybe_flags[2];  // Payload Bytes 52 to 53
    uint8_t unknown54[2];    // Payload Bytes 54 to 55
    uint32_t watts;       // Payload Bytes 56 to 59
    uint8_t unknown3[88];    // Payload Bytes 60 to 147
    uint32_t timestamp;   // Payload Bytes 148 to 152
  };

  // A Mac Address or install code response
  struct Addr {
    char header;
    char is_resp;
    char msg_type;
    uint8_t data_len;
    uint8_t addr[8];
    char newline;
  };

  // Firmware version response
  struct Ver {
    char header;
    char is_resp;
    char msg_type;
    uint8_t data_len;
    uint8_t value;
    char newline;
  };

  union input_buffer {
    uint8_t data[260];  // 4 byte header + 255 bytes payload + 1 byte terminator
    struct MeterReadingV2 mr2;
    struct Addr addr;
    struct Ver ver;
  } input_buffer;

  char mgm_mac_address[25] = "";
  char mgm_install_code[25] = "";
  int mgm_firmware_ver = 0;

  uint16_t pos = 0;
  uint16_t data_len;

  // Buffer for accumulating MGM log messages
  char log_buf_[1024];
  uint16_t log_pos_ = 0;

  using steady_time_point = std::chrono::time_point<std::chrono::steady_clock>;
  static constexpr steady_time_point min_steady_time_point =
    steady_time_point::min();
  using steady_clock = std::chrono::steady_clock;

  steady_time_point last_meter_reading = min_steady_time_point;
  bool last_reading_has_error;
  steady_time_point now;

  // Consecutive unscaled V7+ readings since the last scaled one.
  uint8_t unscaled_reading_count = 0;

  // The most recent ZCL Metering Multiplier (V2 payload byte 47). Formerly
  // called meter_div. Both Multiplier and Divisor are uint24 in ZCL, so they
  // are held in uint32_t to avoid truncating larger values.
  uint32_t meter_multiplier = 0;

  // The most recent ZCL Metering Divisor. Formerly called cost_unit.
  uint32_t meter_divisor = 0;

  void set_debug(bool enable) { debug_ = enable; }
  void set_polling_enabled(bool enable) { polling_enabled_ = enable; }
  void set_update_interval(uint32_t update_interval) {
    PollingComponent::set_update_interval(update_interval);
    update_interval_ = std::chrono::milliseconds(update_interval);
  }
  void set_power_sensor(sensor::Sensor *sensor) { power_sensor_ = sensor; }
  void set_power_export_sensor(sensor::Sensor *sensor) {
    power_export_sensor_ = sensor;
  }
  void set_power_import_sensor(sensor::Sensor *sensor) {
    power_import_sensor_ = sensor;
  }
  void set_energy_sensor(sensor::Sensor *sensor) { energy_sensor_ = sensor; }
  void set_energy_export_sensor(sensor::Sensor *sensor) {
    energy_export_sensor_ = sensor;
  }
  void set_energy_import_sensor(sensor::Sensor *sensor) {
    energy_import_sensor_ = sensor;
  }
  void setup() override;
  void update() override;
  void loop() override;
  void dump_config() override;

  /* Helper functions */

  // Turn the wifi led on/off
  void led_wifi(bool state) {
#if USE_LED_PINS
    if (state)
      gpio_set_level(LED_PIN_WIFI, 0);
    else
      gpio_set_level(LED_PIN_WIFI, 1);
#endif
    return;
  }

  // Turn the link led on/off
  void led_link(bool state) {
#if USE_LED_PINS
    if (state)
      gpio_set_level(LED_PIN_LINK, 0);
    else
      gpio_set_level(LED_PIN_LINK, 1);
#endif
    return;
  }

  // Reads and logs everything from serial until it runs
  // out of data or encounters a 0x0d byte (ascii CR)
  void dump_serial_input(bool logit) {
    while (available()) {
      if (input_buffer.data[pos] == 0x0d) {
        break;
      }
      input_buffer.data[pos] = read();
      if (pos == sizeof(input_buffer.data)) {
        if (logit) {
          ESP_LOGE(TAG, "Filled buffer with garbage:");
          ESP_LOG_BUFFER_HEXDUMP(TAG, input_buffer.data, pos, ESP_LOG_ERROR);
        }
        pos = 0;
      } else {
        pos++;
      }
    }
    if (pos > 0 && logit) {
      ESP_LOGE(TAG, "Skipped input:");
      ESP_LOG_BUFFER_HEXDUMP(TAG, input_buffer.data, pos - 1, ESP_LOG_ERROR);
    }
    pos = 0;
    data_len = 0;
  }

  size_t read_msg() {
    if (!available()) {
      return 0;
    }

    while (available()) {
      char c = read();
      uint16_t prev_pos = pos;
      input_buffer.data[pos] = c;
      pos++;

      switch (prev_pos) {
        case 0: {
          if (c != 0x24) {  // 0x24 == "$", the start of a message
            append_log(c);
            pos = 0;
            continue;
          }
          break;
        }
        case 1: {
          if (c != 0x01) {  // 0x01 means "response"
            append_log(c);
            // Check if this byte is itself a new sync
            pos = (c == 0x24) ? 1 : 0;
            if (pos == 1) input_buffer.data[0] = c;
            continue;
          }
          break;
        }
        case 2: {
          // This is the message type byte
          break;
        }
        case 3: {
          // The 3rd byte should be the data length
          data_len = c;
          break;
        }
        case sizeof(input_buffer.data) - 1: {
          ESP_LOGE(TAG, "Buffer overrun");
          pos = 0;
          return 0;
        }
        default: {
          if (pos < data_len + 5) {
            // Still accumulating payload
          } else if (c == 0x0d) {  // 0x0d == "\r", which should end a message
            return pos;
          } else {
            ESP_LOGE(TAG, "Invalid terminator at pos %d: %02x", pos, (uint8_t)c);
            pos = 0;
            return 0;
          }
        }
      }
    }  // while(available())

    return 0;
  }

  int32_t endian_swap(uint32_t in) {
    uint32_t x = 0;
    x += (in & 0x000000FF) << 24;
    x += (in & 0x0000FF00) << 8;
    x += (in & 0x00FF0000) >> 8;
    x += (in & 0xFF000000) >> 24;
    return x;
  }

  float apply_watt_adjustment(int64_t input, uint32_t meter_multiplier,
                              uint32_t meter_divisor) {
    return ((float)input * (float)meter_multiplier) /
           ((float)meter_divisor / 1000.0);
  }

  // Returns false if the reading was ignored (neither used nor an error), so
  // the caller doesn't count it as the latest meter reading.
  bool handle_resp_meter_reading() {
    float watt_hours = 0;
    float watts = 0;
    struct MeterReadingV2 *mr2;
    mr2 = &input_buffer.mr2;

    if (mgm_firmware_ver < 7) {
      ESP_LOGD(TAG, "Parsing V2 Payload");

      // Make sure the packet is as long as we expect
      if (pos < sizeof(struct MeterReadingV2)) {
        ESP_LOGE(TAG, "Short meter reading packet");
        last_reading_has_error = 1;
        return true;
      }

      // Setup Multiplier
      meter_multiplier = parse_multiplier(mr2->meter_multiplier);

      // Setup Divisor (stored big-endian in the V2 payload)
      meter_divisor = ((mr2->meter_divisor & 0x00FF) << 8) +
                      ((mr2->meter_divisor & 0xFF00) >> 8);

      watt_hours = parse_meter_watt_hours_v2(mr2);
      watts = parse_meter_watts_v2(mr2->watts);

      // Extra debugging of non-zero bytes, only on first packet or if
      // debug_ is true
      if ((debug_) || (last_meter_reading == min_steady_time_point)) {
        ESP_LOGD(TAG, "Meter Multiplier: %" PRIu32, meter_multiplier);
        ESP_LOGD(TAG, "Meter Divisor: %" PRIu32, meter_divisor);
        ESP_LOGD(TAG, "Meter Flags: %02x %02x", mr2->maybe_flags[0],
                 mr2->maybe_flags[1]);
        ESP_LOGD(TAG, "Meter Energy Flags: %02x", (uint8_t)mr2->watt_hours);
        ESP_LOGD(TAG, "Meter Power Flags: %02x", (uint8_t)mr2->watts);
        // Unlike the other values, ms_since_reset is in our native byte order
        ESP_LOGD(TAG, "Meter Timestamp: %.f", float(mr2->timestamp) / 1000.0);
        ESP_LOGD(TAG, "Meter Energy: %.3fkWh", watt_hours / 1000.0);
        ESP_LOGD(TAG, "Meter Power:  %3.0fW", watts);

        for (int x = 1; x < pos / 4; x++) {
          int y = x * 4;
          if ((input_buffer.data[y]) || (input_buffer.data[y + 1]) ||
              (input_buffer.data[y + 2]) || (input_buffer.data[y + 3])) {
            ESP_LOGD(
                TAG, "Meter Response Bytes %3d to %3d: %02x %02x %02x %02x",
                y - 4, y - 1, input_buffer.data[y], input_buffer.data[y + 1],
                input_buffer.data[y + 2], input_buffer.data[y + 3]);
          }
        }
      }
    } else {
      ESP_LOGD(TAG, "Parsing V7+ Payload");

      // The V7+ payload is a ZCL Read Attributes Response for the Simple
      // Metering cluster (0x0702). Decode it generically (see
      // zcl_meter_reading.h). The payload begins at input_buffer.data[4] (after
      // the '$' 0x01 <type> <len> header) and is data_len bytes long.
      ParsedV7Reading reading = parse_v7_zcl(&input_buffer.data[4], data_len);

      if (!reading.ok) {
        ESP_LOGE(TAG, "Failed to parse V7+ ZCL meter reading");
        last_reading_has_error = 1;
        return true;
      }

      // The Multiplier and Divisor are required to scale the readings, and real
      // meters send them in every reading. Their absence means a malformed
      // payload, not a value we should assume.
      if (!reading.multiplier_present) {
        ESP_LOGE(TAG, "Meter reading missing Multiplier attribute (0x0301)");
        last_reading_has_error = 1;
        return true;
      }
      if (!reading.divisor_present) {
        ESP_LOGE(TAG, "Meter reading missing Divisor attribute (0x0302)");
        last_reading_has_error = 1;
        return true;
      }

      // Some meters reply to a request with an all-zero reading before the real
      // one. Skip it without treating it as an error, unless that's all the
      // meter ever sends.
      if (reading.is_unscaled()) {
        unscaled_reading_count++;
        if (unscaled_reading_count < MAX_UNSCALED_READINGS) {
          ESP_LOGD(TAG, "Meter reading has zero Multiplier/Divisor, ignoring");
          return false;
        }
        ESP_LOGE(TAG,
                 "Got %d consecutive readings with zero Multiplier/Divisor",
                 unscaled_reading_count);
        unscaled_reading_count = 0;
        last_reading_has_error = 1;
        return true;
      }
      unscaled_reading_count = 0;

      meter_multiplier = parse_multiplier(reading.multiplier);
      meter_divisor = reading.divisor;

      if (reading.watts_present) {
        watts = parse_meter_watts_v7(reading.watts);
      }
      if (reading.import_present) {
        watt_hours = parse_meter_watt_hours_v7(
            reading.import_wh, reading.export_wh, reading.export_present);
      }

      // Extra debugging of non-zero bytes, only on first packet or if
      // debug_ is true
      if ((debug_) || (last_meter_reading == min_steady_time_point)) {
        ESP_LOGD(TAG, "Meter Multiplier: %" PRIu32, meter_multiplier);
        ESP_LOGD(TAG, "Meter Divisor: %" PRIu32, meter_divisor);
        ESP_LOGD(TAG, "Meter Import Energy: %.3fkWh", reading.import_wh / 1000.0);
        if (reading.export_present) {
          ESP_LOGD(TAG, "Meter Export Energy: %.3fkWh",
                   reading.export_wh / 1000.0);
        } else {
          ESP_LOGD(TAG, "Meter Export Energy: not reported");
        }
        ESP_LOGD(TAG, "Meter Net Energy: %.3fkWh", watt_hours / 1000.0);
        ESP_LOGD(TAG, "Meter Power:  %3.0fW", watts);

        for (int x = 1; x < pos / 4; x++) {
          int y = x * 4;
          if ((input_buffer.data[y]) || (input_buffer.data[y + 1]) ||
              (input_buffer.data[y + 2]) || (input_buffer.data[y + 3])) {
            ESP_LOGD(
                TAG, "Meter Response Bytes %3d to %3d: %02x %02x %02x %02x",
                y - 4, y - 1, input_buffer.data[y], input_buffer.data[y + 1],
                input_buffer.data[y + 2], input_buffer.data[y + 3]);
          }
        }
      }
    }
    return true;
  }

  void ask_for_bug_report() {
    ESP_LOGE(TAG, "If you continue to see this, try asking for help at");
    ESP_LOGE(TAG,
             "  "
             "https://community.home-assistant.io/t/"
             "emporia-vue-utility-connect/378347");
    ESP_LOGE(TAG,
             "and include a few lines above this message and the data below "
             "until \"EOF\":");
    ESP_LOGE(TAG, "Full packet:");
    for (int x = 1; x < pos / 4; x++) {
      int y = x * 4;
      if ((input_buffer.data[y]) || (input_buffer.data[y + 1]) ||
          (input_buffer.data[y + 2]) || (input_buffer.data[y + 3])) {
        ESP_LOGE(TAG, "  Meter Response Bytes %3d to %3d: %02x %02x %02x %02x",
                 y - 4, y - 1, input_buffer.data[y], input_buffer.data[y + 1],
                 input_buffer.data[y + 2], input_buffer.data[y + 3]);
      }
    }
    ESP_LOGI(TAG, "MGM Firmware Version: %d", mgm_firmware_ver);
    ESP_LOGE(TAG, "EOF");
  }

  uint32_t parse_multiplier(uint32_t new_multiplier) {
    uint32_t mult = new_multiplier;
    if ((new_multiplier > 10) || (new_multiplier < 1)) {
      ESP_LOGW(TAG, "Unreasonable Multiplier value %" PRIu32 ", ignoring",
               new_multiplier);
      last_reading_has_error = 1;
    } else if ((meter_multiplier != 0) && (new_multiplier != meter_multiplier)) {
      ESP_LOGW(TAG, "Multiplier value changed from %" PRIu32 " to %" PRIu32,
               meter_multiplier, new_multiplier);
      last_reading_has_error = 1;
      mult = new_multiplier;
    }
    return mult;
  }

  float parse_meter_watt_hours_v2(struct MeterReadingV2 *mr) {
    // Keep the last N watt-hour samples so invalid new samples can be discarded
    static float history[MAX_WH_CHANGE_ARY];
    static uint8_t history_pos;
    static bool not_first_run;

    // Counters for deriving consumed and returned separately
    static uint32_t consumed;
    static uint32_t returned;

    float prev_wh;

    float watt_hours;
    int32_t watt_hours_raw;
    float wh_diff;
    float history_avg;
    int8_t x;

    watt_hours_raw = endian_swap(mr->watt_hours);
    if ((watt_hours_raw == 4194304)  //  "missing data" message (0x00 40 00 00)
        || (watt_hours_raw == 0)) {
      ESP_LOGI(TAG, "Watt-hours value missing");
      last_reading_has_error = 1;
      return (0);
    }

    // Handle if a meter multiplier/divisor is in effect
    watt_hours =
        apply_watt_adjustment(watt_hours_raw, meter_multiplier, meter_divisor);

    if (!not_first_run) {
      // Initialize watt-hour filter on first run
      for (x = MAX_WH_CHANGE_ARY; x != 0; x--) {
        history[x - 1] = watt_hours;
      }
      not_first_run = 1;
    }

    // Fetch the previous value from history
    prev_wh = history[history_pos];

    // Insert a new value into filter array
    history_pos++;
    if (history_pos == MAX_WH_CHANGE_ARY) {
      history_pos = 0;
    }
    history[history_pos] = watt_hours;

    history_avg = 0;
    // Calculate avg watt_hours over previous N samples
    for (x = MAX_WH_CHANGE_ARY; x != 0; x--) {
      history_avg += history[x - 1] / MAX_WH_CHANGE_ARY;
    }

    // Get the difference of current value from avg
    if (abs(history_avg - watt_hours) > MAX_WH_CHANGE) {
      ESP_LOGE(TAG, "Unreasonable watt-hours of %f, +%f from moving avg",
               watt_hours, watt_hours - history_avg);
      last_reading_has_error = 1;
      return (watt_hours);
    }

    // Get the difference from previously reported value
    wh_diff = watt_hours - prev_wh;

    if (wh_diff > 0) {  // Energy consumed from grid
      if (consumed > UINT32_MAX - wh_diff) {
        consumed -= UINT32_MAX - wh_diff;
      } else {
        consumed += wh_diff;
      }
    }
    if (wh_diff < 0) {  // Energy sent to grid
      if (returned > UINT32_MAX - wh_diff) {
        returned -= UINT32_MAX - wh_diff;
      } else {
        returned -= wh_diff;
      }
    }

    if (energy_import_sensor_ != nullptr) {
      energy_import_sensor_->publish_state(float(consumed));
    }
    if (energy_export_sensor_ != nullptr) {
      energy_export_sensor_->publish_state(float(returned));
    }
    if (energy_sensor_ != nullptr) {
      energy_sensor_->publish_state(watt_hours);
    }

    return (watt_hours);
  }

  // consumed / returned are already in watt-hours, decoded by parse_v7_zcl().
  // export_present is false when the meter did not report the
  // export summation (e.g. no solar), in which case returned is 0 and the export
  // sensor is left unpublished.
  float parse_meter_watt_hours_v7(double consumed, double returned,
                                  bool export_present) {
    static float consumed_history[MAX_WH_CHANGE_ARY];
    static float returned_history[MAX_WH_CHANGE_ARY];
    static uint8_t history_pos;
    static bool filter_seeded;
    static uint8_t consecutive_rejects;

    if (!filter_seeded) {
      for (int x = 0; x < MAX_WH_CHANGE_ARY; x++) {
        consumed_history[x] = consumed;
        returned_history[x] = returned;
      }
      filter_seeded = 1;
    }

    float consumed_avg = 0;
    float returned_avg = 0;
    for (int x = 0; x < MAX_WH_CHANGE_ARY; x++) {
      consumed_avg += consumed_history[x] / MAX_WH_CHANGE_ARY;
      returned_avg += returned_history[x] / MAX_WH_CHANGE_ARY;
    }

    // Sometimes the reported value is far larger than it should be. Validate the
    // new reading against the moving average before storing it, so a spike
    // never poisons the window.
    if (std::abs(consumed - consumed_avg) > MAX_WH_CHANGE ||
        std::abs(returned - returned_avg) > MAX_WH_CHANGE) {
      consecutive_rejects++;
      ESP_LOGW(TAG,
               "Reported watt-hours too large vs moving average "
               "(consumed %.0f vs avg %.0f, returned %.0f vs avg %.0f). "
               "Skipping (%d/%d).",
               consumed, consumed_avg, returned, returned_avg,
               consecutive_rejects, MAX_WH_REJECTS);
      // This handles two scenarios:
      // 1) An outage/reboot/counter rollover causes a genuine step change in the
      //    baseline. After MAX_WH_REJECTS consecutive rejects we assume this is
      //    real and re-sync the filter to the new level.
      // 2) Transient erroneous blips: a run of up to MAX_WH_REJECTS - 1
      //    consecutive bad samples. Each is ignored and never stored, so the
      //    window stays clean and the counter resets the moment a good sample
      //    returns, at which point it is accepted immediately.
      if (consecutive_rejects < MAX_WH_REJECTS) {
        return (0);
      }
      ESP_LOGW(TAG, "Re-baselining energy filter to new level");
      for (int x = 0; x < MAX_WH_CHANGE_ARY; x++) {
        consumed_history[x] = consumed;
        returned_history[x] = returned;
      }
    }
    consecutive_rejects = 0;

    // Accept: advance the ring buffer with the new reading
    history_pos++;
    if (history_pos == MAX_WH_CHANGE_ARY) {
      history_pos = 0;
    }
    consumed_history[history_pos] = consumed;
    returned_history[history_pos] = returned;

    float net = consumed - returned;

    if (energy_import_sensor_ != nullptr) {
      energy_import_sensor_->publish_state(consumed);
    }
    if (export_present && energy_export_sensor_ != nullptr) {
      energy_export_sensor_->publish_state(returned);
    }
    if (energy_sensor_ != nullptr) {
      energy_sensor_->publish_state(net);
    }

    return (net);
  }

  /*
   * Read the instant watts value.
   *
   * For MGM version 2 (to 6?)
   */
  float parse_meter_watts_v2(int32_t watts_raw) {
    int32_t watts_24bit;
    float watts;

    // Read the instant watts value
    // (it's actually a 24-bit int)
    watts_24bit = (endian_swap(watts_raw) & 0xFFFFFF);

    // Bit 1 of the left most byte indicates a negative value
    if (watts_24bit & 0x800000) {
      if (watts_24bit == 0x800000) {
        // Exactly "negative zero", which means "missing data"
        ESP_LOGI(TAG, "Instant Watts value missing");
        return (0);
      } else if (watts_24bit & 0xC00000) {
        // This is either more than 12MW being returned,
        // or it's a negative number in 1's complement.
        // Since the returned value is a 24-bit value
        // and "watts" is a 32-bit signed int, we can
        // get away with this.
        watts_24bit -= 0xFFFFFF;
      } else {
        // If we get here, then hopefully it's a negative
        // number in signed magnitude format
        watts_24bit = (watts_24bit ^ 0x800000) * -1;
      }
    }

    // Handle the adjustment.
    watts = apply_watt_adjustment(watts_24bit, meter_multiplier, meter_divisor);

    if ((watts >= WATTS_MAX) || (watts < WATTS_MIN)) {
      ESP_LOGE(TAG, "Unreasonable watts value %f", watts);
      last_reading_has_error = 1;
    } else {
      if (power_sensor_ != nullptr) {
        power_sensor_->publish_state(watts);
      }
      if (watts > 0) {
        if (power_import_sensor_ != nullptr) {
          power_import_sensor_->publish_state(watts);
        }
        if (power_export_sensor_ != nullptr) {
          power_export_sensor_->publish_state(0);
        }
      } else {
        if (power_import_sensor_ != nullptr) {
          power_import_sensor_->publish_state(0);
        }
        if (power_export_sensor_ != nullptr) {
          power_export_sensor_->publish_state(-watts);
        }
      }
    }
    return (watts);
  }

  /*
   * Validate and publish the instant watts value.
   *
   * For MGM version 7 and 8. `watts` is already in watts, decoded from the
   * signed int24 InstantaneousDemand attribute by parse_v7_zcl().
   */
  float parse_meter_watts_v7(float watts) {
    if ((watts >= WATTS_MAX) || (watts < WATTS_MIN)) {
      ESP_LOGE(TAG, "Unreasonable watts value %f", watts);
      last_reading_has_error = 1;
    } else {
      if (power_sensor_ != nullptr) {
        power_sensor_->publish_state(watts);
      }
      if (watts > 0) {
        if (power_import_sensor_ != nullptr) {
          power_import_sensor_->publish_state(watts);
        }
        if (power_export_sensor_ != nullptr) {
          power_export_sensor_->publish_state(0);
        }
      } else {
        if (power_import_sensor_ != nullptr) {
          power_import_sensor_->publish_state(0);
        }
        if (power_export_sensor_ != nullptr) {
          power_export_sensor_->publish_state(-watts);
        }
      }
    }
    return (watts);
  }

  void handle_resp_meter_join() {
    // ESP_LOGD(TAG, "Got meter join response");
    // Reusing Ver struct because both have a single byte payload value.
    struct Ver *ver;
    ver = &input_buffer.ver;
    ESP_LOGI(TAG, "Join response value: %d", ver->value);
  }

  int handle_resp_mac_address() {
    // ESP_LOGD(TAG, "Got mac addr response");
    struct Addr *mac;
    mac = &input_buffer.addr;

    snprintf(mgm_mac_address, sizeof(mgm_mac_address),
             "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", mac->addr[7],
             mac->addr[6], mac->addr[5], mac->addr[4], mac->addr[3],
             mac->addr[2], mac->addr[1], mac->addr[0]);
    ESP_LOGI(TAG, "MGM Mac Address: %s", mgm_mac_address);
    return (0);
  }

  int handle_resp_install_code() {
    // ESP_LOGD(TAG, "Got install code response");
    struct Addr *code;
    code = &input_buffer.addr;

    snprintf(mgm_install_code, sizeof(mgm_install_code),
             "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X", code->addr[0],
             code->addr[1], code->addr[2], code->addr[3], code->addr[4],
             code->addr[5], code->addr[6], code->addr[7]);
    ESP_LOGI(TAG, "MGM Install Code: %s (secret)", mgm_install_code);
    return (0);
  }

  int handle_resp_firmware_ver() {
    struct Ver *ver;
    ver = &input_buffer.ver;

    mgm_firmware_ver = ver->value;

    ESP_LOGI(TAG, "MGM Firmware Version: %d", mgm_firmware_ver);
    return (0);
  }

  void send_meter_request() {
    const uint8_t msg[] = {0x24, 0x72, 0x0d};
    ESP_LOGD(TAG, "Sending request for meter reading");
    write_array(msg, sizeof(msg));
    led_link(false);
  }

  void send_meter_join() {
    const uint8_t msg[] = {0x24, 0x6a, 0x0d};
    ESP_LOGI(TAG, "MGM Firmware Version: %d", mgm_firmware_ver);
    ESP_LOGI(TAG, "MGM Mac Address:  %s", mgm_mac_address);
    ESP_LOGI(TAG, "MGM Install Code: %s (secret)", mgm_install_code);
    ESP_LOGI(
        TAG,
        "Trying to re-join the meter.  If you continue to see this message");
    ESP_LOGI(TAG,
             "you may need to move the device closer to your power meter or");
    ESP_LOGI(TAG,
             "contact your utililty and ask them to reprovision the device.");
    ESP_LOGI(TAG,
             "Also confirm that the above mac address & install code match");
    ESP_LOGI(TAG, "what is printed on your device.");
    ESP_LOGE(TAG, "You can also try asking for help at");
    ESP_LOGE(TAG,
             "  "
             "https://community.home-assistant.io/t/"
             "emporia-vue-utility-connect/378347");
    write_array(msg, sizeof(msg));
    led_wifi(false);
  }

  void send_mac_req() {
    const uint8_t msg[] = {0x24, 0x6d, 0x0d};
    ESP_LOGD(TAG, "Sending mac addr request");
    write_array(msg, sizeof(msg));
    led_wifi(false);
  }

  void send_install_code_req() {
    const uint8_t msg[] = {0x24, 0x69, 0x0d};
    ESP_LOGD(TAG, "Sending install code request");
    write_array(msg, sizeof(msg));
    led_wifi(false);
  }

  void send_version_req() {
    const uint8_t msg[] = {0x24, 0x66, 0x0d};
    ESP_LOGD(TAG, "Sending firmware version request");
    write_array(msg, sizeof(msg));
    led_wifi(false);
  }

  void clear_serial_input() {
    write(0x0d);
    flush();
    delay(100);
    while (available()) {
      while (available()) read();
      delay(100);
    }
  }

  void send_loglevel(uint8_t level) {
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "loglevel %d\r", level);
    ESP_LOGI(TAG, "Sending: %s", cmd);
    write_str(cmd);
    flush();
  }

  void append_log(uint8_t c) {
    if (log_pos_ < sizeof(log_buf_) - 1) {
      log_buf_[log_pos_++] = c;
    }
    // Check for \r\n ending and flush
    if (log_pos_ >= 2 &&
        log_buf_[log_pos_ - 2] == '\r' &&
        log_buf_[log_pos_ - 1] == '\n') {
      flush_log();
    }
  }

  void flush_log() {
    // Trim trailing \r\n
    while (log_pos_ > 0 && (log_buf_[log_pos_ - 1] == '\r' || log_buf_[log_pos_ - 1] == '\n')) {
      log_pos_--;
    }
    if (log_pos_ > 0) {
      // Build sanitized output with hex for non-printables
      // Worst case: every char becomes "[XX]" = 4x expansion
      char out_buf[4096];
      uint16_t out_pos = 0;
      for (uint16_t i = 0; i < log_pos_ && out_pos < sizeof(out_buf) - 5; i++) {
        char c = log_buf_[i];
        if (c >= 0x20 && c <= 0x7e) {
          out_buf[out_pos++] = c;
        } else {
          out_pos += snprintf(out_buf + out_pos, sizeof(out_buf) - out_pos, "[%02X]", (uint8_t)c);
        }
      }
      out_buf[out_pos] = '\0';
      ESP_LOGI(TAG, "MGM: %s", out_buf);
    }
    log_pos_ = 0;
  }

 private:
  bool debug_ = false;
  bool polling_enabled_ = true;
  steady_clock::duration update_interval_;
  sensor::Sensor *power_sensor_{nullptr};
  sensor::Sensor *power_export_sensor_{nullptr};
  sensor::Sensor *power_import_sensor_{nullptr};
  sensor::Sensor *energy_sensor_{nullptr};
  sensor::Sensor *energy_export_sensor_{nullptr};
  sensor::Sensor *energy_import_sensor_{nullptr};
  bool ready_to_read_meter_ = false;
};

static inline void set_pin_to_output(gpio_num_t pin) {
  gpio_reset_pin(pin);
  gpio_set_direction(pin, GPIO_MODE_OUTPUT);
}

}  // namespace emporia_vue_utility
}  // namespace esphome
