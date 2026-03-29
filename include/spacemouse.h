#pragma once
#include <stdint.h>
#include <string.h>

// ── VID / PID ─────────────────────────────────────────────────────────────
#define VID_3DCONNEXION            0x256Fu
#define PID_SPACEMOUSE_COMPACT     0xC635u
#define PID_SPACEMOUSE_PRO         0xC631u
#define PID_SPACEMOUSE_PRO_WIREL   0xC632u
#define PID_SPACEMOUSE_ENTERPRISE  0xC633u
#define PID_SPACEMOUSE_MODULE      0xC636u

// Older models shipped under Logitech VID 0x046D
#define VID_LOGITECH               0x046Du
#define PID_SPACENAVIGATOR         0xC626u
#define PID_SPACEEXPLORER          0xC627u
#define PID_SPACEMOUSE_PRO_LOGITECH 0xC628u  // SpaceMouse Pro (Logitech era)
#define PID_SPACEPILOT_PRO         0xC629u
#define PID_SPACENAVIGATOR_NB      0xC62Bu  // SpaceNavigator for Notebooks

inline bool spacemouse_vid_pid_match(uint16_t vid, uint16_t pid) {
    if (vid == VID_3DCONNEXION)
        return pid == PID_SPACEMOUSE_COMPACT    ||
               pid == PID_SPACEMOUSE_PRO        ||
               pid == PID_SPACEMOUSE_PRO_WIREL  ||
               pid == PID_SPACEMOUSE_ENTERPRISE ||
               pid == PID_SPACEMOUSE_MODULE;
    if (vid == VID_LOGITECH)
        return pid == PID_SPACENAVIGATOR        ||
               pid == PID_SPACEEXPLORER         ||
               pid == PID_SPACEMOUSE_PRO_LOGITECH ||
               pid == PID_SPACEPILOT_PRO        ||
               pid == PID_SPACENAVIGATOR_NB;
    return false;
}

// ── State ─────────────────────────────────────────────────────────────────
struct SpaceMouseState {
    int16_t  tx = 0, ty = 0, tz = 0;
    int16_t  rx = 0, ry = 0, rz = 0;
    uint32_t buttons         = 0;
    uint32_t buttons_changed = 0;

    void reset() { *this = SpaceMouseState{}; }
};

// Pass the full TinyUSB buffer (report ID is byte 0).
bool spacemouse_parse(SpaceMouseState &state,
                      const uint8_t   *report_data,
                      uint16_t         report_len);