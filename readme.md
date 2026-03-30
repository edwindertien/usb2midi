# USB2MIDI — Universal USB HID to USB-MIDI Converter

A firmware for the **Waveshare RP2350-USB-A** board that converts any USB HID device (SpaceMouse, gamepad, joystick, or unknown device) into USB-MIDI output. Device mappings are configurable via JSON files on LittleFS — no reflashing needed to remap axes or buttons.

---

## Features

- **USB-A host** — connects HID devices via pio-usb on GP12/GP13
- **Composite USB-C** — presents simultaneously as USB-MIDI device and CDC serial port
- **Multi-device** — up to 4 simultaneous HID devices, each on its own MIDI channel
- **Configurable mappings** — per-device JSON files on LittleFS (axis CC, invert, scale, deadzone, note base)
- **Generic fallback** — unknown devices auto-mapped to CC30–CC37 on MIDI channel 4
- **OLED display** — SSD1306 64×32 showing axis bars and button dots at 5fps
- **Status LED** — WS2812B on GP16 showing connection and data state
- **Rich debug output** — CDC serial shows device mount/unmount, VID/PID, config loaded, axis values, CC output

---

## Hardware

### Board: Waveshare RP2350-USB-A

| Feature | Details |
|---------|---------|
| MCU | RP2350A |
| USB-C | Native USB — CDC serial + USB-MIDI (composite) |
| USB-A | pio-usb host on GP12 (D+) / GP13 (D−) |
| Status LED | WS2812B RGB on GP16 |
| Crystal | 12MHz |
| Flash | 2MB |
| PSRAM | None |

### Required Hardware Fix — Desolder R13

**The board cannot work as a USB host without this modification.**

The RP2350-USB-A ships with a 1.5 kΩ resistor R13 permanently pulling D+ high on the USB-A port. This makes pio-usb permanently see a phantom device and prevents real device detection.

**R13 is located directly beneath pin 6 of the board** (3rd from the right on the header closest to the RP2350 chip, closest resistor to pin 1 of the chip).

To remove it:
1. Use a fine soldering iron tip or hot-air station
2. Touch both pads simultaneously — the resistor will slide off
3. Verify removal with a multimeter: D+ (GP12 via R11) should now read 0V at idle with no device connected

After removal, full-speed devices (SpaceMouse, F310, etc.) will be detected correctly on both boot-with-device-connected and hot-plug.

> **Note:** After removing R13, the USB-A port can no longer act as a USB device — only as a host. This is exactly what we want.

### OLED Display Wiring — SSD1306 64×32

Connect the SSD1306 module to **I2C0** (Wire) on the RP2350:

| OLED Pin | Board Pin | GPIO |
|----------|-----------|------|
| SDA | Header pin | GP4 |
| SCL | Header pin | GP5 |
| VCC | 3.3V | — |
| GND | GND | — |

I2C address: `0x3C` (standard for most SSD1306 breakout modules).

> **Important:** Use `Wire` (I2C0), not `Wire1`. Using Wire1 on these pins will cause the firmware to hang silently at startup.

### Status LED

The WS2812B is onboard at GP16. No external wiring needed.

| Colour | Meaning |
|--------|---------|
| Red (steady) | Powered, waiting for USB-A device |
| Cyan flash | Device just mounted |
| Blue (steady) | Device connected, idle |
| Green flash | MIDI data being sent |
| Orange flash | Device disconnected |

---

## Firmware Architecture

```
Core 0 (main loop)          Core 1 (USB host)
─────────────────────       ─────────────────
USB-MIDI output             pio-usb task loop
CDC serial debug            tuh_mount_cb
OLED update (5fps)          tuh_umount_cb
NeoPixel update             tuh_hid_report_received_cb
LittleFS config load
Spin-lock shared state ◄──► Spin-lock shared state
Event queue drain ◄────────── Event queue push
```

All shared device state is protected by a hardware spin-lock. Core 1 never calls Serial, Wire, or the NeoPixel driver — it only pushes events to a ring buffer that core 0 drains in `loop()`.

---

## Project Structure

```
src/
  main.cpp              — core 0/1 setup, USB callbacks, MIDI/OLED/LED output
  config.cpp            — LittleFS JSON loader/saver
  device_parsers.cpp    — HID report parsers for each device type

include/
  hid_device.h          — HIDDeviceState, HIDDevice, MAX_DEVICES
  device_registry.h     — VID/PID classification, DeviceType enum, parser dispatch
  config.h              — DeviceConfig struct, axis config, config_load/save
  mapping.h             — axis_to_cc() with deadzone, invert, scale
  oled.h                — SSD1306 64×32 display driver
  neo.h                 — WS2812B bit-bang driver (no PIO library)
  usbh_helper.h         — USBHost object + rp2040_configure_pio_usb()

lib/
  pio_usb/              — vendored Pico-PIO-USB v0.5.3

data/
  mappings/
    046D_C628.json      — SpaceMouse Pro (Logitech VID)
    046D_C216.json      — Logitech F310 XInput mode
    046D_C21D.json      — Logitech F310 DirectInput mode
    044F_B108.json      — Thrustmaster USB Joystick
```

---

## Building and Flashing

### Prerequisites

- VSCode with PlatformIO extension
- earlphilhower arduino-pico core (installed via platformio.ini)

### Build

```bash
pio run -e usb2midi
```

### Flash firmware

```bash
pio run -e usb2midi --target upload
```

The combined firmware + LittleFS image is flashed in one step. The `data/mappings/` JSON files are included automatically.

### Flash LittleFS only (mappings updated, firmware unchanged)

If you only changed JSON mapping files and do not want to reflash the firmware:

```bash
pio run -e usb2midi --target buildfs
```

This produces `.pio/build/usb2midi/littlefs.bin`. Convert to UF2 and flash:

```bash
picotool uf2 convert .pio/build/usb2midi/littlefs.bin littlefs.uf2 \
  --family rp2350-non-secure --offset 0x10180000
```

Then hold BOOTSEL, plug in USB-C, and drag `littlefs.uf2` to the RPI-RP2 drive.

> **Tip:** The easier route is just `pio run --target upload` — it always flashes both firmware and filesystem together.

---

## Default MIDI Mappings

| Device | MIDI Ch | CC Range | Note Base | Deadzone |
|--------|---------|----------|-----------|----------|
| SpaceMouse (all variants) | 1 | CC1–CC6 | C2 (36) | 800 |
| F310 XInput (046D:C216) | 2 | CC14–CC17 | C3 (48) | 1500 |
| F310 DirectInput (046D:C21D) | 2 | CC14–CC19 | C3 (48) | 1000 |
| Thrustmaster USB JS | 3 | CC20–CC23 | C4 (60) | 600 |
| Generic / unknown | 4 | CC30–CC37 | C5 (72) | 500 |

All defaults can be overridden per-device with a JSON file.

### SpaceMouse axis layout

| Axis | CC | Movement |
|------|----|---------|
| A0 | CC1 | TX — left/right |
| A1 | CC2 | TY — up/down |
| A2 | CC3 | TZ — forward/back |
| A3 | CC4 | RX — pitch |
| A4 | CC5 | RY — roll |
| A5 | CC6 | RZ — yaw |

All axes map to CC 0–127, centre (rest) = 64.

---

## JSON Mapping Files

Each recognised device can have a JSON file at `/mappings/<VID>_<PID>.json` on LittleFS. If no file exists, built-in defaults are used.

### Schema

```json
{
  "_comment": "Human-readable description",
  "name":       "Display name (shown in serial debug)",
  "midi_ch":    1,
  "cc_base":    1,
  "note_base":  36,
  "note_vel":   100,
  "deadzone":   800,
  "axes": [
    { "cc": 1, "invert": false, "scale": 1.0, "_name": "axis 0 label" },
    { "cc": 2, "invert": false, "scale": 1.0, "_name": "axis 1 label" }
  ]
}
```

### Fields

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Device name shown in serial debug output |
| `midi_ch` | 1–16 | MIDI channel for all output from this device |
| `cc_base` | 0–127 | Default CC number for axis[0] (used if axis `cc` is 0) |
| `note_base` | 0–127 | MIDI note for button[0]; button[n] = note_base + n |
| `note_vel` | 0–127 | Note On velocity |
| `deadzone` | 0–32767 | Axis deadzone in internal units (±32767 full scale) |
| `axes[n].cc` | 0–127 | Override CC number for axis n (0 = use cc_base + n) |
| `axes[n].invert` | bool | Invert axis direction |
| `axes[n].scale` | float | Multiply axis value before deadzone (e.g. 2.0 for double sensitivity) |

### Example — remap SpaceMouse TX to Ableton Filter Cutoff

```json
{
  "name": "SpaceMouse Pro",
  "midi_ch": 1,
  "cc_base": 1,
  "note_base": 36,
  "note_vel": 100,
  "deadzone": 800,
  "axes": [
    { "cc": 74, "invert": false, "scale": 1.5, "_name": "TX → Filter Cutoff" },
    { "cc": 2,  "invert": false, "scale": 1.0, "_name": "TY" },
    { "cc": 3,  "invert": false, "scale": 1.0, "_name": "TZ" },
    { "cc": 4,  "invert": false, "scale": 1.0, "_name": "RX" },
    { "cc": 71, "invert": true,  "scale": 1.0, "_name": "RY → Filter Resonance (inverted)" },
    { "cc": 6,  "invert": false, "scale": 1.0, "_name": "RZ" }
  ]
}
```

---

## Adding Support for a New Device

### Step 1 — Capture the VID and PID

Plug the device in and watch the CDC serial debug output:

```
[+] Mount   dev=1  ABCD:1234  Generic
    HID instance count = 1
    "ABCD:1234"  ch=4  cc_base=30  note_base=72  [default]
```

Note the `VID:PID` (here `ABCD:1234`).

### Step 2 — Capture raw reports

The firmware prints the first 5 raw HID reports for unrecognised devices in debug mode. Move axes and press buttons while watching the serial output:

```
[raw XInput 8 bytes] 80 80 80 7F 08 00 00 FF
[raw XInput 8 bytes] FF 80 80 7F 08 00 00 FF   ← axis moved
[raw XInput 8 bytes] 80 80 FF 7F 08 00 00 FF   ← different axis
[raw XInput 8 bytes] 80 80 80 7F 18 00 00 FF   ← button pressed (byte 4 changed)
```

You can also capture HID reports with a USB analyser tool on your computer (USBPcap on Windows, `usbmon` on Linux, Wireshark with USBPcap).

### Step 3 — Decode the report layout

Work through the bytes systematically:

1. **Find the report ID** — if byte 0 is always the same small number (1–8), it's a report ID. Some devices use multiple report IDs for different data (e.g. SpaceMouse uses 0x01 for translation and 0x02 for rotation).

2. **Find axes** — move one axis at a time and watch which bytes change. Common formats:
   - **uint8** centred at `0x80` (range 0–255) — e.g. F310 XInput sticks
   - **int16 LE** centred at 0 (range −32768 to +32767) — e.g. SpaceMouse
   - **uint16 LE** centred at 0x8000 (range 0–65535) — e.g. F310 DirectInput sticks
   - **uint8** unipolar 0–255 — e.g. triggers and throttles

3. **Find buttons** — press one button at a time and watch which bits change. Usually a bitmask across one or two bytes.

4. **Find hat switches** — if a D-pad nibble cycles through values 0–7 (direction) and 8 (centre), it's a hat switch and needs special decoding into 4 virtual Up/Right/Down/Left buttons.

### Step 4 — Classify the device

Add the VID/PID to `device_registry.h`:

```cpp
// In the VID/PID constants section:
#define VID_MY_BRAND    0xABCDu
#define PID_MY_DEVICE   0x1234u

// In classify_device():
if (vid == VID_MY_BRAND) {
    if (pid == PID_MY_DEVICE) return DEV_MY_DEVICE;
}
```

Add the new type to the `DeviceType` enum:

```cpp
enum DeviceType {
    DEV_UNKNOWN = 0,
    DEV_SPACEMOUSE,
    DEV_F310_DI,
    DEV_F310_XI,
    DEV_THRUSTMASTER,
    DEV_MY_DEVICE,    // ← new
    DEV_GENERIC,
};
```

Add the type name for debug output:

```cpp
inline const char* device_type_name(DeviceType t) {
    switch (t) {
    // ... existing cases ...
    case DEV_MY_DEVICE: return "MyDevice";
    // ...
    }
}
```

### Step 5 — Write the parser

Add a parser function declaration to `device_registry.h`:

```cpp
bool parse_my_device(HIDDeviceState &s, const uint8_t *report, uint16_t len);
```

Add it to the `parse_report` dispatcher:

```cpp
case DEV_MY_DEVICE: return parse_my_device(s, report, len);
```

Implement it in `device_parsers.cpp`. Use the captured report layout:

```cpp
bool parse_my_device(HIDDeviceState &s, const uint8_t *report, uint16_t len) {
    if (len < 8) return false;   // minimum expected length

    s.axis_count   = 2;
    s.button_count = 8;

    // uint8 axis centred at 0x80:
    auto stick8 = [](uint8_t b) -> int16_t {
        return (int16_t)((int32_t)(b - 0x80) * 256);
    };
    s.axis[0] = stick8(report[0]);   // X axis at byte 0
    s.axis[1] = stick8(report[1]);   // Y axis at byte 1
    s.axis_changed[0] = s.axis_changed[1] = true;

    // Buttons at byte 2, bitmask:
    uint32_t cur = report[2];
    s.buttons_changed = cur ^ s.buttons;
    s.buttons = cur;

    return true;
}
```

#### Parser helper reference

```cpp
// int16 LE (e.g. SpaceMouse, range ±32767)
int16_t read_i16(const uint8_t *p) {
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

// uint8 centred at 0x80 → int16
int16_t stick8(uint8_t b) {
    return (int16_t)((int32_t)(b - 0x80) * 256);
}

// uint16 LE centred at 0x8000 → int16 (e.g. F310 DInput)
int16_t stick16(uint8_t lo, uint8_t hi) {
    return (int16_t)((uint16_t)lo | ((uint16_t)hi << 8)) - 0x8000;
}

// uint8 unipolar 0–255 → int16 0..+32767 (triggers, throttles)
int16_t scale_axis(int32_t raw, int32_t min, int32_t max) { ... }

// Hat switch nibble (0=up, 1=up-right, ..., 7=up-left, 8=centre)
// → bits 12–15 of buttons word
uint32_t hat = report[n] & 0x0F;
if (hat != 8) {
    if (hat==7||hat==0||hat==1) hat_bits |= (1u<<12);  // Up
    if (hat==1||hat==2||hat==3) hat_bits |= (1u<<13);  // Right
    if (hat==3||hat==4||hat==5) hat_bits |= (1u<<14);  // Down
    if (hat==5||hat==6||hat==7) hat_bits |= (1u<<15);  // Left
}
```

### Step 6 — Add default config

In `config.cpp`, add a case to `default_config()`:

```cpp
case DEV_MY_DEVICE:
    snprintf(cfg.name, sizeof(cfg.name), "MyDevice");
    cfg.midi_ch   = 5;    // pick an unused channel
    cfg.cc_base   = 40;   // pick an unused CC range
    cfg.note_base = 84;   // C6
    cfg.deadzone  = 500;
    break;
```

### Step 7 — Create the JSON mapping file

Create `data/mappings/ABCD_1234.json` (VID_PID in uppercase hex):

```json
{
  "_comment": "MyDevice (ABCD:1234)",
  "name":      "MyDevice",
  "midi_ch":   5,
  "cc_base":   40,
  "note_base": 84,
  "note_vel":  100,
  "deadzone":  500,
  "axes": [
    { "cc": 40, "invert": false, "scale": 1.0, "_name": "X" },
    { "cc": 41, "invert": false, "scale": 1.0, "_name": "Y" }
  ]
}
```

### Step 8 — Flash and test

```bash
pio run -e usb2midi --target upload
```

Plug in the device. Serial output should show:

```
[+] Mount   dev=1  ABCD:1234  MyDevice
    "MyDevice"  ch=5  cc_base=40  note_base=84  [JSON]
[MyDevice ch5] A0:  +0->64  A1:  +0->64
```

Open Ableton Live's MIDI monitor and confirm CCs and notes arrive correctly.

---

## Known Device Quirks

### Logitech F310 — XInput vs DirectInput mode

The F310 has a small switch on the back:
- **X position** — XInput mode, PID `C216`. Reports 4 axes (sticks) as single bytes centred at `0x80`, plus buttons in bytes 4–5.
- **D position** — DirectInput mode, PID `C21D`. Standard HID gamepad with 6 axes (4 sticks + 2 triggers as uint16) and hat switch.

DirectInput mode gives more axes and is the recommended mode.

### F310 XInput — permanent mode LED bit

In XInput mode, bit 3 of byte 4 is always set (the Xbox Guide/mode LED state). The parser masks this out with `& ~0x08u` so it does not appear as a stuck button.

### SpaceMouse — dual report IDs

The SpaceMouse sends two report types per physical movement:
- Report ID `0x01` — TX/TY/TZ (translation)
- Report ID `0x02` — RX/RY/RZ (rotation)
- Report ID `0x03` — button bitmask

The parser accumulates both into the same `HIDDeviceState`. Because reports arrive separately, a single `loop()` iteration may process only 3 of the 6 axes — this is normal.

### XInput devices — vendor class

XInput uses `bInterfaceClass = 0xFF` (vendor-specific), not `0x03` (HID). TinyUSB's HID class driver claims XInput interfaces only if it has been compiled with `CFG_TUH_HID > 0` **and** the device happens to present its XInput interface in a way TinyUSB accepts. If `tuh_hid_instance_count()` returns 0 for a device, it is a pure XInput device requiring raw endpoint access — see the XInput raw endpoint approach in the codebase comments.

---

## Troubleshooting

**No device detected on USB-A**
— Check R13 has been removed. With a multimeter, verify D+ (GP12) reads 0V at idle. If it reads 3.3V, R13 is still present.

**Device lights up but no MIDI output**
— Open the CDC serial monitor. Check that `[+] Mount` appears and a valid config is loaded. If `HID instance count = 0`, the device uses a non-HID class and needs a raw endpoint parser.

**LittleFS mount failed**
— Reflash the full image: `pio run --target upload`. The filesystem may not have been initialised.

**JSON file not loaded (shows `[default]`)**
— Filename must be `<VID>_<PID>.json` with VID and PID in uppercase 4-digit hex. Check `ls data/mappings/` and the serial debug output for the exact VID:PID the device reports.

**OLED not working / firmware hangs at startup**
— Verify SDA is on GP4 and SCL is on GP5. Using any other I2C bus (Wire1, etc.) or swapping SDA/SCL will cause a silent hang at `Wire.begin()`.

**CDC serial works but no MIDI in DAW**
— In Ableton, go to Preferences → MIDI and enable the USB2MIDI device on the input side. Check that the MIDI channel in the JSON matches what you're monitoring in Ableton.

**USB host stops working after adding a new library**
— Some libraries claim PIO state machines. pio-usb requires PIO0. The NeoPixel driver in this project uses direct GPIO bit-bang specifically to avoid PIO conflicts. If adding a new library breaks the host, check whether it uses PIO and if so, force it to PIO1.