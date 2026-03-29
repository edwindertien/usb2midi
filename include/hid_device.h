#pragma once
#include <stdint.h>
#include <string.h>

#define MAX_AXES    8
#define MAX_BUTTONS 32
#define MAX_DEVICES 4

struct HIDDeviceState {
    int16_t  axis[MAX_AXES]         = {};
    bool     axis_changed[MAX_AXES] = {};
    uint8_t  axis_count             = 0;

    uint32_t buttons         = 0;
    uint32_t buttons_changed = 0;
    uint8_t  button_count    = 0;

    void reset() { *this = HIDDeviceState{}; }
};

// Forward-declare so HIDDevice can hold a DeviceConfig by value
struct DeviceConfig;

struct HIDDevice {
    uint8_t  dev_addr  = 0;
    uint8_t  instance  = 0;
    uint16_t vid       = 0;
    uint16_t pid       = 0;
    bool     connected = false;
    bool     dirty     = false;

    // Filled from config_load() on mount
    uint8_t  midi_ch   = 1;
    uint8_t  cc_base   = 1;
    uint8_t  note_base = 36;
    uint8_t  note_vel  = 100;
    int16_t  deadzone  = 800;
    char     name[32]  = {};

    HIDDeviceState state;

    void clear() { *this = HIDDevice{}; }
};