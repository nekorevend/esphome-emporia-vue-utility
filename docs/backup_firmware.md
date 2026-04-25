# Back up the original firmware

Prerequisites:
- You need the [ESPHome CLI](installing_esphome.md) installed.
- You need the Vue device to be [wired to a USB to TTY adapter](wiring_and_usb.md).

Reminder: triple check your wiring, making sure you are not connecting +5V to anything other than pin 6.  You can now plug it into your computer and the Vue device should power up and start working as normal.

**1. Determine which USB port it is connected to:**
  - on Windows, it shows up as `COM3`, `COM4`, etc
  - on Mac and Linux, look in the filesystem: `/dev/ttyUSB0`, `/dev/ttyUSB1`, or `/dev/tty.usbserial-xxxx`.

**2. To backup the firmware:**

Note: the `esptool` command is used here, which comes installed with the ESPHome CLI.
```
# esptool --port <usb_port> --chip esp32 -b 115200 read-flash 0x0 0x400000 <filename>
```

Examples:
```
(on Windows)
# esptool --port COM3 --chip esp32 -b 115200 read-flash 0x0 0x400000 vue_original_firmare.bin

(on a Mac)
# esptool --port /dev/tty.usbserial-BG01UJXF --chip esp32 -b 115200 read-flash 0x0 0x400000 vue_original_firmare.bin
```

Reminder: if it doesn't work, swap pins 1 and 2.

**3. If you need to restore:**

```
# esptool --port <usb_port> --chip esp32 -b 115200 write-flash --flash-freq 80m 0x0 <filename>
```
