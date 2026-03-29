#pragma once
// ============================================================
//  usbh_helper.h  —  pio-usb host pin config + USBHost object
//
//  pio_usb.h is vendored in lib/pio_usb/ and exposed via
//  -I lib/pio_usb in platformio.ini build_flags.
//
//  Include order matters:
//  1. hardware/pio.h first — SDK defines pio_sm_set_jmp_pin,
//     preventing the duplicate definition in usb_rx.pio.h
//  2. pio_usb.h — defines pio_usb_configuration_t etc.
//  3. Adafruit_TinyUSB.h — gives Adafruit_USBH_Host
// ============================================================

#include "hardware/pio.h"
#include "pio_usb.h"
#include "Adafruit_TinyUSB.h"

#ifndef PIN_USB_HOST_DP
  #define PIN_USB_HOST_DP 12
#endif

// Defined here; include this header only from main.cpp.
Adafruit_USBH_Host USBHost;

static inline void rp2040_configure_pio_usb(void) {
    static pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp = PIN_USB_HOST_DP;
    USBHost.configure_pio_usb(1, &pio_cfg);
}