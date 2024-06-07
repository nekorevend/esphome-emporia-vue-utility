#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"

// Extra meter reading response debugging
#define DEBUG_VUE_RESPONSE true

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
    byte unknown0[4];     // Payload Bytes 0 to 3
    uint32_t watt_hours;  // Payload Bytes 4 to 7
    byte unknown8[39];    // Payload Bytes 8 to 46
    uint8_t meter_div;    // Payload Byte  47
    byte unknown48[2];    // Payload Bytes 48 to 49
    uint16_t cost_unit;   // Payload Bytes 50 to 51
    byte maybe_flags[2];  // Payload Bytes 52 to 53
    byte unknown54[2];    // Payload Bytes 54 to 55
    uint32_t watts;       // Payload Bytes 56 to 59
    byte unknown3[88];    // Payload Bytes 60 to 147
    uint32_t timestamp;   // Payload Bytes 148 to 152
  };

  /**
   * Format known from MGM Firmware version 7 and 8.
   */
  struct MeterReadingV7 {
    byte header;
    byte is_resp;
    byte msg_type;
    uint8_t data_len;
    byte unknown0;     // Payload Byte  0 : Always 0x18
    byte increment;    // Payload Byte  1 : Increments on each reading and rolls
                       // over
    byte unknown2[5];  // Payload Bytes 2 to 6
    uint32_t import_wh;  // Payload Bytes 7 to 10
    byte unknown11[6];   // Payload Bytes 11 to 16
    uint32_t export_wh;  // Payload Bytes 17 to 20
    byte unknown21[19];  // Payload Bytes 21 to 39
    uint32_t watts;  // Payload Bytes 40 to 43 : Starts with 0x2A, only use the
                     // last 24 bits.
  } __attribute__((packed));

  // A Mac Address or install code response
  struct Addr {
    char header;
    char is_resp;
    char msg_type;
    uint8_t data_len;
    byte addr[8];
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
    byte data[260];  // 4 byte header + 255 bytes payload + 1 byte terminator
    struct MeterReadingV2 mr2;
    struct MeterReadingV7 mr7;
    struct Addr addr;
    struct Ver ver;
  } input_buffer;

  char mgm_mac_address[25] = "";
  char mgm_install_code[25] = "";
  int mgm_firmware_ver = 0;

  uint16_t pos = 0;
  uint16_t data_len;

  time_t last_meter_reading = 0;
  bool last_reading_has_error;
  time_t now;

  // The most recent meter divisor, meter reading payload V2 byte 47
  uint8_t meter_div = 0;

  // The most recent cost unit
  uint16_t cost_unit = 0;

  void set_update_interval(uint32_t update_interval) {
    update_interval_ = update_interval / 1000;
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
      digitalWrite(LED_PIN_WIFI, 0);
    else
      digitalWrite(LED_PIN_WIFI, 1);
#endif
    return;
  }

  // Turn the link led on/off
  void led_link(bool state) {
#if USE_LED_PINS
    if (state)
      digitalWrite(LED_PIN_LINK, 0);
    else
      digitalWrite(LED_PIN_LINK, 1);
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
        case 0:
          if (c != 0x24) {  // 0x24 == "$", the start of a message
            ESP_LOGE(TAG, "Invalid input at position %d: 0x%x", pos, c);
            dump_serial_input(true);
            pos = 0;
            return 0;
          }
          break;
        case 1:
          if (c != 0x01) {  // 0x01 means "response"
            ESP_LOGE(TAG, "Invalid input at position %d 0x%x", pos, c);
            dump_serial_input(true);
            pos = 0;
            return 0;
          }
          break;
        case 2:
          // This is the message type byte
          break;
        case 3:
          // The 3rd byte should be the data length
          data_len = c;
          break;
        case sizeof(input_buffer.data) - 1:
          ESP_LOGE(TAG, "Buffer overrun");
          dump_serial_input(true);
          return 0;
        default:
          if (pos < data_len + 5) {
            ;
          } else if (c == 0x0d) {  // 0x0d == "/r", which should end a message
            return pos;
          } else {
            ESP_LOGE(TAG, "Invalid terminator at pos %d 0x%x", pos, c);
            ESP_LOGE(TAG, "Following char is 0x%x", read());
            dump_serial_input(true);
            return 0;
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

  void handle_resp_meter_reading() {
    int32_t input_value;
    float watt_hours;
    float watts;
    struct MeterReadingV2 *mr2;
    mr2 = &input_buffer.mr2;
    struct MeterReadingV7 *mr7;
    mr7 = &input_buffer.mr7;

    if (mgm_firmware_ver < 7) {
      ESP_LOGD(TAG, "Parsing V2 Payload");

      // Make sure the packet is as long as we expect
      if (pos < sizeof(struct MeterReadingV2)) {
        ESP_LOGE(TAG, "Short meter reading packet");
        last_reading_has_error = 1;
        return;
      }

      // Setup Meter Divisor
      if ((mr2->meter_div > 10) || (mr2->meter_div < 1)) {
        ESP_LOGW(TAG, "Unreasonable MeterDiv value %d, ignoring",
                 mr2->meter_div);
        last_reading_has_error = 1;
        ask_for_bug_report();
      } else if ((meter_div != 0) && (mr2->meter_div != meter_div)) {
        ESP_LOGW(TAG, "MeterDiv value changed from %d to %d", meter_div,
                 mr2->meter_div);
        last_reading_has_error = 1;
        meter_div = mr2->meter_div;
      } else {
        meter_div = mr2->meter_div;
      }

      // Setup Cost Unit
      cost_unit =
          ((mr2->cost_unit & 0x00FF) << 8) + ((mr2->cost_unit & 0xFF00) >> 8);

      watt_hours = parse_meter_watt_hours_v2(mr2);
      watts = parse_meter_watts_v2(mr2);

      // Extra debugging of non-zero bytes, only on first packet or if
      // DEBUG_VUE_RESPONSE is true
      if ((DEBUG_VUE_RESPONSE) || (last_meter_reading == 0)) {
        ESP_LOGD(TAG, "Meter Divisor: %d", meter_div);
        ESP_LOGD(TAG, "Meter Cost Unit: %d", cost_unit);
        ESP_LOGD(TAG, "Meter Flags: %02x %02x", mr2->maybe_flags[0],
                 mr2->maybe_flags[1]);
        ESP_LOGD(TAG, "Meter Energy Flags: %02x", (byte)mr2->watt_hours);
        ESP_LOGD(TAG, "Meter Power Flags: %02x", (byte)mr2->watts);
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

      // Quick validate, look for a magic number.
      if (input_buffer.data[44] != 0x2A) {
        ESP_LOGE(TAG, "Byte 44 was %02x instead of %02x", input_buffer.data[44],
                 0x2A);
        last_reading_has_error = 1;
        return;
      }

      watts = parse_meter_watts_v7(mr7->watts);
      watt_hours = parse_meter_watt_hours_v7(mr7);
    }
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

    // Handle if a meter divisor is in effect
    watt_hours = (float)watt_hours_raw * (float)meter_div;

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

  float parse_meter_watt_hours_v7(struct MeterReadingV7 *mr) {
    uint32_t consumed;
    uint32_t returned;
    static uint32_t prev_consumed;
    static uint32_t prev_returned;
    int32_t net = 0;

    consumed = mr->import_wh;
    returned = mr->export_wh;
    int32_t consumed_diff = int32_t(consumed) - int32_t(prev_consumed);
    int32_t returned_diff = int32_t(returned) - int32_t(prev_returned);

    // Sometimes the reported value is far larger than it should be. Let's
    // ignore it.
    if (std::abs(consumed_diff) > MAX_WH_CHANGE ||
        std::abs(returned_diff) > MAX_WH_CHANGE) {
      ESP_LOGW(TAG,
               "Reported watt-hour change is too large vs previous reading. "
               "Skipping.");
      // The `prev_consumed` and `prev_returned` will still be given the current
      // reading even if the value is erroneous.
      //
      // This approach should handle two scenarios:
      // 1) Some sort of outage causes a long gap between the previous reading
      // (or is 0 after a reboot) and the current reading. In this case, the
      // difference from the previous reading can be "too" large, but actually
      // be expected. 2) I have seen erroneous blips of a single sample with a
      // value that is way too big.
      //
      // The code handles scenario #1 by ignoring the current reading but then
      // continuing on as normal after. The code handles scenario #2 by ignoring
      // the current reading, then ignoring the followup reading, then
      // continuing on as normal.
      //
      // At worst, two consecutive samples will be ignored.
      prev_consumed = consumed;
      prev_returned = returned;
      return (0);
    }

    net = consumed - returned;

    if (energy_import_sensor_ != nullptr) {
      energy_import_sensor_->publish_state(float(consumed));
    }
    if (energy_export_sensor_ != nullptr) {
      energy_export_sensor_->publish_state(float(returned));
    }
    if (energy_sensor_ != nullptr) {
      energy_sensor_->publish_state(net);
    }

    prev_consumed = consumed;
    prev_returned = returned;

    return (net);
  }

  /*
   * Read the instant watts value.
   *
   * For MGM version 2 (to 6?)
   */
  float parse_meter_watts_v2(struct MeterReadingV2 *mr) {
    int32_t watts_raw;
    float watts;

    // Read the instant watts value
    // (it's actually a 24-bit int)
    watts_raw = (endian_swap(mr->watts) & 0xFFFFFF);

    // Bit 1 of the left most byte indicates a negative value
    if (watts_raw & 0x800000) {
      if (watts_raw == 0x800000) {
        // Exactly "negative zero", which means "missing data"
        ESP_LOGI(TAG, "Instant Watts value missing");
        return (0);
      } else if (watts_raw & 0xC00000) {
        // This is either more than 12MW being returned,
        // or it's a negative number in 1's complement.
        // Since the returned value is a 24-bit value
        // and "watts" is a 32-bit signed int, we can
        // get away with this.
        watts_raw -= 0xFFFFFF;
      } else {
        // If we get here, then hopefully it's a negative
        // number in signed magnitude format
        watts_raw = (watts_raw ^ 0x800000) * -1;
      }
    }

    // Handle if a meter divisor is in effect
    watts = (float)watts_raw * (float)meter_div;

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
   * Read the instant watts value.
   *
   * For MGM version 7 and 8
   */
  float parse_meter_watts_v7(int32_t watts) {
    // Read the instant watts value
    // (it's actually a 24-bit int)
    watts >>= 8;

    if ((watts >= WATTS_MAX) || (watts < WATTS_MIN)) {
      ESP_LOGE(TAG, "Unreasonable watts value %d", watts);
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
    const byte msg[] = {0x24, 0x72, 0x0d};
    ESP_LOGD(TAG, "Sending request for meter reading");
    write_array(msg, sizeof(msg));
    led_link(false);
  }

  void send_meter_join() {
    const byte msg[] = {0x24, 0x6a, 0x0d};
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
    ESP_LOGE(TAG, "You can also file a bug at");
    ESP_LOGE(TAG, "  https://forms.gle/duMdU2i7wWHdbK5TA");
    write_array(msg, sizeof(msg));
    led_wifi(false);
  }

  void send_mac_req() {
    const byte msg[] = {0x24, 0x6d, 0x0d};
    ESP_LOGD(TAG, "Sending mac addr request");
    write_array(msg, sizeof(msg));
    led_wifi(false);
  }

  void send_install_code_req() {
    const byte msg[] = {0x24, 0x69, 0x0d};
    ESP_LOGD(TAG, "Sending install code request");
    write_array(msg, sizeof(msg));
    led_wifi(false);
  }

  void send_version_req() {
    const byte msg[] = {0x24, 0x66, 0x0d};
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

 private:
  uint32_t update_interval_;
  sensor::Sensor *power_sensor_{nullptr};
  sensor::Sensor *power_export_sensor_{nullptr};
  sensor::Sensor *power_import_sensor_{nullptr};
  sensor::Sensor *energy_sensor_{nullptr};
  sensor::Sensor *energy_export_sensor_{nullptr};
  sensor::Sensor *energy_import_sensor_{nullptr};
};

}  // namespace emporia_vue_utility
}  // namespace esphome