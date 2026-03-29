#pragma once
#include <stdint.h>
#include "config.h"

// Axis → 7-bit MIDI CC, with per-device deadzone, invert and scale
inline uint8_t axis_to_cc(int16_t raw, const DeviceConfig &cfg, uint8_t axis_idx) {
    float v = (float)raw;

    // Per-axis scale
    if (axis_idx < MAX_AXES) {
        v *= cfg.axes[axis_idx].scale;
        if (cfg.axes[axis_idx].invert) v = -v;
    }

    // Deadzone
    if (v > -(float)cfg.deadzone && v < (float)cfg.deadzone) v = 0.0f;

    // Clamp to ±32767
    if (v >  32767.0f) v =  32767.0f;
    if (v < -32767.0f) v = -32767.0f;

    // Map to 0-127, centre=64
    int32_t cc = (int32_t)(v * 63.0f / 32767.0f) + 64;
    if (cc < 0)   cc = 0;
    if (cc > 127) cc = 127;
    return (uint8_t)cc;
}