#pragma once
#include <stdint.h>
#include "hid_device.h"

// ============================================================
//  Device MIDI config — loaded from LittleFS or built-in defaults
//
//  JSON file location: /mappings/<VID_HEX>_<PID_HEX>.json
//  Example: /mappings/046D_C628.json  (SpaceMouse Pro Logitech)
//
//  JSON schema:
//  {
//    "name":        "SpaceMouse Pro",
//    "midi_ch":     1,          // 1-16
//    "cc_base":     1,          // first CC for axis[0]
//    "note_base":   36,         // first note for button[0]
//    "note_vel":    100,        // Note On velocity
//    "deadzone":    800,        // axis deadzone (0-32767 scale)
//    "axes": [                  // optional per-axis overrides
//      { "cc": 1, "invert": false, "scale": 1.0 },
//      ...
//    ]
//  }
//
//  Any field not present uses the built-in default for that device type.
// ============================================================

#define CONFIG_DIR "/mappings"

struct AxisConfig {
    uint8_t cc      = 0;       // 0 = use cc_base + axis_index
    bool    invert  = false;
    float   scale   = 1.0f;   // multiplier applied before deadzone
};

struct DeviceConfig {
    char     name[32]             = "Unknown";
    uint8_t  midi_ch              = 1;
    uint8_t  cc_base              = 1;
    uint8_t  note_base            = 36;
    uint8_t  note_vel             = 100;
    int16_t  deadzone             = 800;
    AxisConfig axes[MAX_AXES]     = {};
    bool     loaded               = false;  // true if from LittleFS
};

// Initialise LittleFS. Call once in setup().
bool config_init();

// Load config for a specific VID:PID.
// Falls back to built-in defaults if no JSON file exists.
DeviceConfig config_load(uint16_t vid, uint16_t pid);

// Save a config back to LittleFS (for future: web UI or serial configurator)
bool config_save(uint16_t vid, uint16_t pid, const DeviceConfig &cfg);

// Resolve the CC number for a given axis index
inline uint8_t axis_cc(const DeviceConfig &cfg, uint8_t axis_idx) {
    if (axis_idx < MAX_AXES && cfg.axes[axis_idx].cc != 0)
        return cfg.axes[axis_idx].cc;
    return cfg.cc_base + axis_idx;
}