# spacemouse_test — PlatformIO / earlphilhower

SpaceMouse USB host test for **Waveshare RP2350-USB-A**.

- USB-A host: **GP12 (D+) / GP13 (D-)** via Pico-PIO-USB
- USB-C device: **CDC serial** for debug output
- CPU: **120 MHz** (required for pio-usb timing)

---

## Setup

### 1. Install libraries via PlatformIO

`platformio.ini` pulls these automatically on first build:

| Library | Source |
|---|---|
| Adafruit TinyUSB Library ≥ 3.4 | `adafruit/Adafruit TinyUSB Library` |
| Pico PIO USB ≤ 0.5.3 | `sekigon-gonnoc/Pico PIO USB` |

> **Pin version of Pico PIO USB to 0.5.3.**  
> Version 0.6.0 introduced a breaking API change; 0.5.3 is the last
> version that works with the `configure_pio_usb()` pattern used here.

### 2. Build

```bash
pio run
```

First build fetches the earlphilhower toolchain (~200 MB). Subsequent builds are fast.

### 3. Flash

```bash
# With picotool (board powered normally):
pio run --target upload

# Or drag-and-drop UF2:
# Hold BOOTSEL, connect USB-C, release, then:
cp .pio/build/waveshare_rp2350/firmware.uf2 /media/$USER/RPI-RP2/
```

### 4. Monitor

```bash
pio device monitor
```

Plug a SpaceMouse into the USB-A port. Expected output:

```
=== SpaceMouse USB Host Test ===
Board : Waveshare RP2350-USB-A
Host  : GP12 (D+) / GP13 (D-)
Plug SpaceMouse into USB-A port...

[host] mount  dev=1 inst=0  256F:C635
[+] SpaceMouse connected!
    TX  TY  TZ  |  RX  RY  RZ  |  BTN
TX: +123 TY:  -45 TZ:   +0  |  RX: +200 RY:  -10 RZ:  +55  |  0000
    Button  1 PRESSED
    Button  1 released
```

---

## File structure

```
spacemouse_pio/
├── platformio.ini
├── include/
│   ├── usbh_helper.h     pio-usb pin config + USBHost object declaration
│   └── spacemouse.h      VID/PID table, SpaceMouseState, parser API
└── src/
    ├── main.cpp          Core 0 (Serial output) + Core 1 (USB host task)
    └── spacemouse.cpp    HID report parser
```

---

## How the USB host API works (earlphilhower + Adafruit TinyUSB)

```
setup1()                         loop1()
  rp2040_configure_pio_usb()       USBHost.task()  ←─── fires callbacks
  USBHost.begin(1)                       │
                                         ▼
                            tuh_hid_mount_cb()
                            tuh_hid_report_received_cb()
                            tuh_hid_umount_cb()
```

- `USBHost` is `Adafruit_USBH_Host` declared in `usbh_helper.h`
- `rp2040_configure_pio_usb()` wraps `pio_usb_configuration_t` setup internally — you never construct it directly
- `USBHost.begin(1)` and `USBHost.task()` must both be on **core 1** (`setup1` / `loop1`)
- `tuh_hid_receive_report()` must be called at the end of `tuh_hid_report_received_cb()` to re-arm; forgetting this stops all future reports

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| `Adafruit_USBH_Host` not declared | Add `adafruit/Adafruit TinyUSB Library` to `lib_deps` |
| `pio_usb_configuration_t` not declared | Add `sekigon-gonnoc/Pico PIO USB @ ^0.5.3` to `lib_deps` |
| SpaceMouse not detected | Check GP12/GP13 wiring; try a shorter/better USB cable; check 1.5 kΩ D+ pull-up |
| Reports arrive but values are zero | Your firmware uses the 12-byte combined variant — already handled by parser |
| `[host] mount` appears but no data | `tuh_hid_receive_report()` not called on mount — check callback |
| Build fails: CPU speed error | Set `board_build.f_cpu = 120000000L` (120 MHz) in `platformio.ini` |
