#pragma once
#include <stdint.h>
#include "hid_device.h"

// ── VID / PID constants ───────────────────────────────────────────────────
#define VID_3DCONNEXION              0x256Fu
#define PID_SPACEMOUSE_COMPACT       0xC635u
#define PID_SPACEMOUSE_PRO           0xC631u
#define PID_SPACEMOUSE_PRO_WIREL     0xC632u
#define PID_SPACEMOUSE_ENTERPRISE    0xC633u
#define PID_SPACEMOUSE_MODULE        0xC636u

#define VID_LOGITECH                 0x046Du
#define PID_SPACENAVIGATOR           0xC626u
#define PID_SPACEEXPLORER            0xC627u
#define PID_SPACEMOUSE_PRO_LOGITECH  0xC628u
#define PID_SPACEPILOT_PRO           0xC629u
#define PID_SPACENAVIGATOR_NB        0xC62Bu
#define PID_F310_DINPUT              0xC21Du   // DirectInput mode
#define PID_F310_XINPUT              0xC216u   // XInput mode  ← your device

#define VID_THRUSTMASTER             0x044Fu
#define PID_TM_USB_JOYSTICK          0xB108u

// ── Device type ───────────────────────────────────────────────────────────
enum DeviceType {
    DEV_UNKNOWN = 0,
    DEV_SPACEMOUSE,
    DEV_F310_DI,    // DirectInput — standard HID report
    DEV_F310_XI,    // XInput — different report layout
    DEV_THRUSTMASTER,
    DEV_GENERIC,
};

inline DeviceType classify_device(uint16_t vid, uint16_t pid) {
    if (vid == VID_3DCONNEXION) {
        if (pid == PID_SPACEMOUSE_COMPACT    ||
            pid == PID_SPACEMOUSE_PRO        ||
            pid == PID_SPACEMOUSE_PRO_WIREL  ||
            pid == PID_SPACEMOUSE_ENTERPRISE ||
            pid == PID_SPACEMOUSE_MODULE)
            return DEV_SPACEMOUSE;
    }
    if (vid == VID_LOGITECH) {
        if (pid == PID_SPACENAVIGATOR         ||
            pid == PID_SPACEEXPLORER          ||
            pid == PID_SPACEMOUSE_PRO_LOGITECH||
            pid == PID_SPACEPILOT_PRO         ||
            pid == PID_SPACENAVIGATOR_NB)
            return DEV_SPACEMOUSE;
        if (pid == PID_F310_DINPUT) return DEV_F310_DI;
        if (pid == PID_F310_XINPUT) return DEV_F310_XI;
    }
    if (vid == VID_THRUSTMASTER)
        if (pid == PID_TM_USB_JOYSTICK)
            return DEV_THRUSTMASTER;
    return DEV_GENERIC;
}

inline const char* device_type_name(DeviceType t) {
    switch (t) {
    case DEV_SPACEMOUSE:   return "SpaceMouse";
    case DEV_F310_DI:      return "F310(DInput)";
    case DEV_F310_XI:      return "F310(XInput)";
    case DEV_THRUSTMASTER: return "Thrustmaster";
    default:               return "Generic";
    }
}

// ── Parser declarations ───────────────────────────────────────────────────
bool parse_spacemouse   (HIDDeviceState &s, const uint8_t *report, uint16_t len);
bool parse_f310_dinput  (HIDDeviceState &s, const uint8_t *report, uint16_t len);
bool parse_f310_xinput  (HIDDeviceState &s, const uint8_t *report, uint16_t len);
bool parse_thrustmaster (HIDDeviceState &s, const uint8_t *report, uint16_t len);
bool parse_generic      (HIDDeviceState &s, const uint8_t *report, uint16_t len);

inline bool parse_report(DeviceType type, HIDDeviceState &s,
                         const uint8_t *report, uint16_t len) {
    switch (type) {
    case DEV_SPACEMOUSE:   return parse_spacemouse(s, report, len);
    case DEV_F310_DI:      return parse_f310_dinput(s, report, len);
    case DEV_F310_XI:      return parse_f310_xinput(s, report, len);
    case DEV_THRUSTMASTER: return parse_thrustmaster(s, report, len);
    default:               return parse_generic(s, report, len);
    }
}