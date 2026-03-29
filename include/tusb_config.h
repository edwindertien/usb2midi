#pragma once

// ============================================================
//  Custom TinyUSB configuration for Waveshare RP2350-USB-A
//  Overrides the library defaults to enable HID host class.
//
//  TinyUSB looks for this file when CFG_TUSB_CONFIG_FILE is
//  defined, or when it's on the include path before the
//  library's own tusb_config.h.
// ============================================================

// MCU
#define CFG_TUSB_MCU              OPT_MCU_RP2040   // covers RP2350 too

// OS
#define CFG_TUSB_OS               OPT_OS_PICO

// ── Device (USB-C) ───────────────────────────────────────────
#define CFG_TUD_ENABLED           1
#define CFG_TUD_CDC               1
#define CFG_TUD_MIDI              0   // overridden to 1 in midi build via build_flags
#define CFG_TUD_HID               0
#define CFG_TUD_MSC               0
#define CFG_TUD_VENDOR            0

// ── Host (USB-A via pio-usb) ─────────────────────────────────
#define CFG_TUH_ENABLED           1
#define CFG_TUH_RPI_PIO_USB       1   // use pio-usb as host controller
#define CFG_TUH_MAX_SPEED         OPT_MODE_FULL_SPEED

#define CFG_TUH_DEVICE_MAX        2
#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUH_HUB               1

// HID host class — THIS is what makes tuh_hid_mount_cb fire
// Value = max number of HID interfaces across all connected devices
#define CFG_TUH_HID               4