#include "emporia_vue_utility.h"

#include "esphome/core/log.h"

namespace esphome {
namespace emporia_vue_utility {

void EmporiaVueUtility::setup() {
#if USE_LED_PINS
  pinMode(LED_PIN_LINK, OUTPUT);
  pinMode(LED_PIN_WIFI, OUTPUT);
#endif
  led_link(false);
  led_wifi(false);
  clear_serial_input();
}

void EmporiaVueUtility::update() {
  if (ready_to_read_meter_) {
    send_meter_request();
  }
}

void EmporiaVueUtility::loop() {
  static const time_t delayed_start_time =
      ::time(nullptr) + INITIAL_STARTUP_DELAY;
  static time_t next_expected_meter_request = 0;
  static time_t next_meter_join = delayed_start_time + METER_REJOIN_INTERVAL;
  static time_t next_version_request = 0;
  static uint8_t startup_step = 0;
  char msg_type = 0;
  size_t msg_len = 0;

  msg_len = read_msg();
  now = ::time(nullptr);

  /* sanity checks! */
  if (next_expected_meter_request >
      now + (INITIAL_STARTUP_DELAY + METER_REJOIN_INTERVAL)) {
    ESP_LOGD(TAG, "Time jumped back (%lld > %lld + %lld); resetting",
             (long long)next_expected_meter_request, (long long)now,
             (long long)(INITIAL_STARTUP_DELAY + METER_REJOIN_INTERVAL));
    next_meter_join = 0;
    next_expected_meter_request = now + update_interval_;
  }

  if (msg_len != 0) {
    msg_type = input_buffer.data[2];

    switch (msg_type) {
      case 'r':  // Meter reading
        led_link(true);
        if (now < last_meter_reading + int(update_interval_ / 4)) {
          // Sometimes a duplicate message is sent in quick succession.
          // Ignoring the duplicate.
          ESP_LOGD(TAG, "Got extra message %ds after the previous message.",
                   now - last_meter_reading);
          break;
        }
        last_reading_has_error = 0;
        handle_resp_meter_reading();
        if (last_reading_has_error) {
          ask_for_bug_report();
        } else {
          last_meter_reading = now;
          next_meter_join = now + METER_REJOIN_INTERVAL;
        }
        break;
      case 'j':  // Meter join
        handle_resp_meter_join();
        led_wifi(true);
        if (startup_step == 3) {
          send_meter_request();
          startup_step++;
        }
        break;
      case 'f':
        if (!handle_resp_firmware_ver()) {
          led_wifi(true);
          if (startup_step == 0) {
            startup_step++;
            send_mac_req();
          }
        }
        break;
      case 'm':  // Mac address
        if (!handle_resp_mac_address()) {
          led_wifi(true);
          if (startup_step == 1) {
            startup_step++;
            send_install_code_req();
          }
        }
        break;
      case 'i':
        if (!handle_resp_install_code()) {
          led_wifi(true);
          if (startup_step == 2) {
            startup_step++;
          }
        }
        break;
      case 'e':
        // Unknown response type, but we can ignore.
        ESP_LOGI(TAG, "Got 'e'-type message with value: %d",
                 input_buffer.data[4]);
        break;
      default:
        ESP_LOGE(TAG, "Unhandled response type '%c'", msg_type);
        ESP_LOG_BUFFER_HEXDUMP(TAG, input_buffer.data, msg_len, ESP_LOG_ERROR);
        break;
    }
    pos = 0;
  }

  if (mgm_firmware_ver < 1 && now >= next_version_request) {
    // Something's wrong, do the startup sequence again.
    startup_step = 0;
    ready_to_read_meter_ = false;
    send_version_req();
    // Throttle this just in case.
    next_version_request = now + MGM_FIRMWARE_REQUEST_INTERVAL;
  }

  if (now >= delayed_start_time) {
    if (now > next_meter_join) {
      startup_step = 9;  // Cancel startup messages
      send_meter_join();
      next_meter_join = now + METER_REJOIN_INTERVAL;
      return;
    }

    if (startup_step == 0)
      send_version_req();
    else if (startup_step == 1)
      send_mac_req();
    else if (startup_step == 2)
      send_install_code_req();
    else if (startup_step == 3)
      send_meter_join();
    else {
      ready_to_read_meter_ = true;
      next_expected_meter_request = now + update_interval_;
    }
  }
}

void EmporiaVueUtility::dump_config() {
  ESP_LOGCONFIG(TAG, "Emporia Vue Utility Connect");
  ESP_LOGCONFIG(TAG, "  MGM Firmware Version: %d", this->mgm_firmware_ver);
  ESP_LOGCONFIG(TAG, "  MGM MAC Address:  %s", this->mgm_mac_address);
  ESP_LOGCONFIG(TAG, "  MGM Install Code: %s (secret)", this->mgm_install_code);
  LOG_UPDATE_INTERVAL(this);
}
}  // namespace emporia_vue_utility
}  // namespace esphome