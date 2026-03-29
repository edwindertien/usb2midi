/*
 * device_parsers.cpp
 *
 * HID report parsers for each supported device type.
 * All parsers receive the raw report buffer as delivered by TinyUSB:
 *   report[0] = report ID (when descriptor has multiple IDs)
 *   report[1..] = payload
 *
 * All axis outputs are signed 16-bit centred at 0.
 * Full-scale maps to ±32767.
 */

#include "device_registry.h"
#include <string.h>

static inline int16_t read_i16(const uint8_t *p) {
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

// Scale raw value from [raw_min..raw_max] to [-32767..+32767]
static int16_t scale_axis(int32_t raw, int32_t raw_min, int32_t raw_max) {
    if (raw_max == raw_min) return 0;
    int32_t centred = raw - (raw_min + raw_max) / 2;
    int32_t half    = (raw_max - raw_min) / 2;
    int32_t scaled  = (int32_t)((int64_t)centred * 32767 / half);
    if (scaled >  32767) scaled =  32767;
    if (scaled < -32767) scaled = -32767;
    return (int16_t)scaled;
}

// ============================================================
//  SpaceMouse (3Dconnexion + Logitech VID variants)
//
//  Report 0x01: TX TY TZ  (int16 LE, 6 bytes, ±350 typical)
//  Report 0x02: RX RY RZ  (int16 LE, 6 bytes)
//  Report 0x03: buttons   (uint16/uint32 LE)
//  Some firmware: report 0x01 with 12 bytes (all 6 axes)
// ============================================================
bool parse_spacemouse(HIDDeviceState &s, const uint8_t *report, uint16_t len) {
    if (len < 2) return false;
    uint8_t        id  = report[0];
    const uint8_t *pay = report + 1;
    uint16_t       n   = len - 1;

    s.axis_count   = 6;
    s.button_count = 16;

    switch (id) {
    case 0x01:
        if (n < 6) return false;
        s.axis[0] = scale_axis(read_i16(pay+0), -350, 350);  // TX
        s.axis[1] = scale_axis(read_i16(pay+2), -350, 350);  // TY
        s.axis[2] = scale_axis(read_i16(pay+4), -350, 350);  // TZ
        s.axis_changed[0] = s.axis_changed[1] = s.axis_changed[2] = true;
        if (n >= 12) {  // combined report variant
            s.axis[3] = scale_axis(read_i16(pay+6),  -350, 350);
            s.axis[4] = scale_axis(read_i16(pay+8),  -350, 350);
            s.axis[5] = scale_axis(read_i16(pay+10), -350, 350);
            s.axis_changed[3] = s.axis_changed[4] = s.axis_changed[5] = true;
        }
        return true;

    case 0x02:
        if (n < 6) return false;
        s.axis[3] = scale_axis(read_i16(pay+0), -350, 350);  // RX
        s.axis[4] = scale_axis(read_i16(pay+2), -350, 350);  // RY
        s.axis[5] = scale_axis(read_i16(pay+4), -350, 350);  // RZ
        s.axis_changed[3] = s.axis_changed[4] = s.axis_changed[5] = true;
        return true;

    case 0x03:
        if (n < 2) return false;
        {
            uint32_t cur = (uint32_t)pay[0] | ((uint32_t)pay[1] << 8);
            if (n >= 4) cur |= ((uint32_t)pay[2] | ((uint32_t)pay[3] << 8)) << 16;
            s.buttons_changed = cur ^ s.buttons;
            s.buttons = cur;
        }
        return true;

    default: return false;
    }
}

// ============================================================
//  Logitech F310 (HID / D-input mode)  VID=046D PID=C21D
//
//  Byte 0-1:  Left stick X   (uint16 LE, 0-65535, centre 32768)
//  Byte 2-3:  Left stick Y
//  Byte 4-5:  Right stick X
//  Byte 6-7:  Right stick Y
//  Byte 8:    Left trigger   (uint8, 0-255)
//  Byte 9:    Right trigger  (uint8, 0-255)
//  Byte 10-11: Buttons       (uint16 LE, 12 buttons)
//  Byte 12:   Hat switch     (nibble, 0=up, 1=up-right .. 8=centre)
// ============================================================
bool parse_f310(HIDDeviceState &s, const uint8_t *report, uint16_t len) {
    if (len < 11) return false;

    s.axis_count   = 6;
    s.button_count = 16;

    auto stick = [&](uint8_t idx) -> int16_t {
        uint16_t raw = (uint16_t)report[idx] | ((uint16_t)report[idx+1] << 8);
        return (int16_t)(raw - 32768);
    };

    s.axis[0] = stick(0);   // Left X
    s.axis[1] = stick(2);   // Left Y
    s.axis[2] = stick(4);   // Right X
    s.axis[3] = stick(6);   // Right Y
    s.axis[4] = scale_axis(report[8], 0, 255);   // L-trigger
    s.axis[5] = scale_axis(report[9], 0, 255);   // R-trigger
    for (int i = 0; i < 6; i++) s.axis_changed[i] = true;

    uint16_t btns = (uint16_t)report[10] | ((uint16_t)report[11] << 8);
    uint32_t cur  = btns & 0x0FFF;

    if (len > 12) {
        uint8_t hat = report[12] & 0x0F;
        if (hat != 8) {
            if (hat == 0 || hat == 1 || hat == 7) cur |= (1u << 12);  // Up
            if (hat == 1 || hat == 2 || hat == 3) cur |= (1u << 13);  // Right
            if (hat == 3 || hat == 4 || hat == 5) cur |= (1u << 14);  // Down
            if (hat == 5 || hat == 6 || hat == 7) cur |= (1u << 15);  // Left
        }
    }

    s.buttons_changed = cur ^ s.buttons;
    s.buttons = cur;
    return true;
}

// ============================================================
//  Thrustmaster USB Joystick  VID=044F PID=B108
//
//  Byte 0-1:  Stick X   (uint16 LE)
//  Byte 2-3:  Stick Y   (uint16 LE)
//  Byte 4:    Z-rot/rudder (uint8)
//  Byte 5:    Throttle  (uint8)
//  Byte 6-7:  Buttons   (uint16)
//  Byte 8:    Hat switch
// ============================================================
bool parse_thrustmaster(HIDDeviceState &s, const uint8_t *report, uint16_t len) {
    if (len < 7) return false;

    s.axis_count   = 4;
    s.button_count = 12;

    auto u16s = [&](uint8_t idx) -> int16_t {
        uint16_t raw = (uint16_t)report[idx] | ((uint16_t)report[idx+1] << 8);
        return (int16_t)(raw - 32768);
    };

    s.axis[0] = u16s(0);                           // Stick X
    s.axis[1] = u16s(2);                           // Stick Y
    s.axis[2] = scale_axis(report[4], 0, 255);     // Rudder
    s.axis[3] = scale_axis(report[5], 0, 255);     // Throttle
    for (int i = 0; i < 4; i++) s.axis_changed[i] = true;

    uint32_t cur = (uint32_t)report[6] | ((uint32_t)report[7] << 8);
    s.buttons_changed = cur ^ s.buttons;
    s.buttons = cur;
    return true;
}

// ============================================================
//  Generic HID fallback
//
//  Auto-maps first 8 axes to consecutive CC20-27 (overridable
//  via JSON), buttons to Note 48+ on channel 4.
//
//  Scans report bytes as int16 pairs for axes, remaining bytes
//  as button bitfields.
// ============================================================
bool parse_generic(HIDDeviceState &s, const uint8_t *report, uint16_t len) {
    if (len < 3) return false;

    // Skip report ID if byte 0 looks like one (non-zero, < 8)
    uint8_t        offset = (report[0] > 0 && report[0] < 8) ? 1 : 0;
    const uint8_t *pay    = report + offset;
    uint16_t       plen   = len - offset;

    uint8_t naxes = 0;
    uint8_t i = 0;
    while (i + 1 < plen && naxes < MAX_AXES) {
        // Skip obvious padding
        if (pay[i] == 0xFF && pay[i+1] == 0xFF) { i += 2; continue; }
        s.axis[naxes]         = read_i16(pay + i);
        s.axis_changed[naxes] = true;
        naxes++;
        i += 2;
    }
    s.axis_count = naxes;

    // Remaining bytes as button bitfield
    uint32_t cur = 0;
    for (uint8_t b = 0; b < 4 && i < plen; b++, i++)
        cur |= ((uint32_t)pay[i] << (b * 8));
    s.buttons_changed = cur ^ s.buttons;
    s.buttons         = cur;
    s.button_count    = 16;

    return naxes > 0;
}