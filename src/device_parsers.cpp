#include "device_registry.h"
#include <string.h>

static inline int16_t read_i16(const uint8_t *p) {
    return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static int16_t scale_axis(int32_t raw, int32_t raw_min, int32_t raw_max) {
    if (raw_max == raw_min) return 0;
    int32_t centred = raw - (raw_min + raw_max) / 2;
    int32_t half    = (raw_max - raw_min) / 2;
    int32_t scaled  = (int32_t)((int64_t)centred * 32767 / half);
    if (scaled >  32767) scaled =  32767;
    if (scaled < -32767) scaled = -32767;
    return (int16_t)scaled;
}

// ── SpaceMouse ────────────────────────────────────────────────────────────
bool parse_spacemouse(HIDDeviceState &s, const uint8_t *report, uint16_t len) {
    if (len < 2) return false;
    uint8_t id = report[0]; const uint8_t *p = report+1; uint16_t n = len-1;
    s.axis_count = 6; s.button_count = 16;
    switch (id) {
    case 0x01:
        if (n < 6) return false;
        s.axis[0] = scale_axis(read_i16(p+0), -350, 350);
        s.axis[1] = scale_axis(read_i16(p+2), -350, 350);
        s.axis[2] = scale_axis(read_i16(p+4), -350, 350);
        s.axis_changed[0] = s.axis_changed[1] = s.axis_changed[2] = true;
        if (n >= 12) {
            s.axis[3] = scale_axis(read_i16(p+6),  -350, 350);
            s.axis[4] = scale_axis(read_i16(p+8),  -350, 350);
            s.axis[5] = scale_axis(read_i16(p+10), -350, 350);
            s.axis_changed[3] = s.axis_changed[4] = s.axis_changed[5] = true;
        }
        return true;
    case 0x02:
        if (n < 6) return false;
        s.axis[3] = scale_axis(read_i16(p+0), -350, 350);
        s.axis[4] = scale_axis(read_i16(p+2), -350, 350);
        s.axis[5] = scale_axis(read_i16(p+4), -350, 350);
        s.axis_changed[3] = s.axis_changed[4] = s.axis_changed[5] = true;
        return true;
    case 0x03:
        if (n < 2) return false;
        { uint32_t cur = (uint32_t)p[0] | ((uint32_t)p[1]<<8);
          if (n >= 4) cur |= ((uint32_t)p[2]|((uint32_t)p[3]<<8))<<16;
          s.buttons_changed = cur ^ s.buttons; s.buttons = cur; }
        return true;
    default: return false;
    }
}

// ── F310 DirectInput (PID C21D) ───────────────────────────────────────────
// Standard HID gamepad report — TinyUSB strips report ID
bool parse_f310_dinput(HIDDeviceState &s, const uint8_t *report, uint16_t len) {
    if (len < 11) return false;
    s.axis_count = 6; s.button_count = 16;
    auto stick = [&](uint8_t i) -> int16_t {
        uint16_t r = (uint16_t)report[i] | ((uint16_t)report[i+1]<<8);
        return (int16_t)(r - 32768);
    };
    s.axis[0] = stick(0); s.axis[1] = stick(2);
    s.axis[2] = stick(4); s.axis[3] = stick(6);
    s.axis[4] = scale_axis(report[8], 0, 255);
    s.axis[5] = scale_axis(report[9], 0, 255);
    for (int i=0;i<6;i++) s.axis_changed[i]=true;
    uint16_t btns = (uint16_t)report[10]|((uint16_t)report[11]<<8);
    uint32_t cur = btns & 0x0FFF;
    if (len > 12) {
        uint8_t hat = report[12] & 0x0F;
        if (hat != 8) {
            if (hat==0||hat==1||hat==7) cur|=(1u<<12);
            if (hat==1||hat==2||hat==3) cur|=(1u<<13);
            if (hat==3||hat==4||hat==5) cur|=(1u<<14);
            if (hat==5||hat==6||hat==7) cur|=(1u<<15);
        }
    }
    s.buttons_changed = cur ^ s.buttons; s.buttons = cur;
    return true;
}

// ── F310 XInput (PID C216) ────────────────────────────────────────────────
// 8-byte compact HID report (confirmed from capture):
//
//  Byte 0: Left stick X    (uint8, centre=0x80)
//  Byte 1: Left stick Y    (uint8, centre=0x80)
//  Byte 2: Right stick X   (uint8, centre=0x80)
//  Byte 3: Right stick Y   (uint8, centre=0x7F approx)
//  Byte 4: low nibble = hat (0=up,1=up-R,2=R,3=dn-R,4=dn,5=dn-L,6=L,7=up-L,8=centre)
//          high nibble + byte 5 = face/shoulder buttons
//  Byte 5: more buttons
//  Byte 6: Constant 0x00
//  Byte 7: Constant 0xFF
//
// Button layout in bits 12-15 (virtual): Up, Right, Down, Left
// Face buttons in bits 4-11 from bytes 4-5 high nibble
bool parse_f310_xinput(HIDDeviceState &s, const uint8_t *report, uint16_t len) {
    if (len < 6) return false;

    s.axis_count   = 4;
    s.button_count = 16;

    auto stick8 = [](uint8_t b) -> int16_t {
        return (int16_t)((int32_t)(b - 0x80) * 256);
    };
    s.axis[0] = stick8(report[0]);  // Left X
    s.axis[1] = stick8(report[1]);  // Left Y
    s.axis[2] = stick8(report[2]);  // Right X
    s.axis[3] = stick8(report[3]);  // Right Y
    for (int i = 0; i < 4; i++) s.axis_changed[i] = true;

    // Face/shoulder buttons: upper nibble of byte 4 + all of byte 5
    // Bit 3 of byte 4 is mode LED — mask it out
    // Shift upper nibble of byte 4 down to bits 0-3, byte 5 to bits 4-11
    uint32_t face = ((uint32_t)(report[4] & 0xF0) >> 4) |
                    ((uint32_t)report[5] << 4);

    // Hat switch → 4 virtual buttons in bits 12-15
    uint8_t hat = report[4] & 0x0F;
    uint32_t hat_bits = 0;
    if (hat != 8) {  // 8 = centred
        if (hat == 7 || hat == 0 || hat == 1) hat_bits |= (1u << 12); // Up
        if (hat == 1 || hat == 2 || hat == 3) hat_bits |= (1u << 13); // Right
        if (hat == 3 || hat == 4 || hat == 5) hat_bits |= (1u << 14); // Down
        if (hat == 5 || hat == 6 || hat == 7) hat_bits |= (1u << 15); // Left
    }

    uint32_t cur = face | hat_bits;
    s.buttons_changed = cur ^ s.buttons;
    s.buttons = cur;
    return true;
}

// ── Thrustmaster USB Joystick (B108 and B305 variants) ───────────────────
//
// Confirmed layout from B305 capture:
//   Bytes 0-1: Stick X    (uint16 LE, centre ~32768)
//   Bytes 2-3: Stick Y    (uint16 LE, centre ~32768)
//   Bytes 4-5: Rudder/Z   (uint16 LE, centre ~32768)
//   Byte  6:   Throttle   (uint8, 0=full throttle, 255=idle — unipolar)
//   Byte  7:   Buttons    (uint8 bitmask, 6 buttons in bits 0-5)
//   Byte  8:   POV hat    (low nibble: 0=up,1=up-R,2=R,...,7=up-L,8=centre)
//
// Note: A0 at rest showed +15360 rather than 0 — the stick physical
// centre is not at 32768. This is normal for uncalibrated joysticks;
// the deadzone handles it.
bool parse_thrustmaster(HIDDeviceState &s, const uint8_t *report, uint16_t len) {
    if (len < 8) return false;

    s.axis_count   = 4;
    s.button_count = 12;  // 6 face + 4 hat virtual buttons

    // uint16 LE centred at 32768 → signed int16
    auto u16c = [&](uint8_t i) -> int16_t {
        return (int16_t)((uint16_t)report[i] | ((uint16_t)report[i+1] << 8)) - 32768;
    };

    s.axis[0] = u16c(0);  // Stick X
    s.axis[1] = u16c(2);  // Stick Y
    s.axis[2] = u16c(4);  // Rudder/twist
    // Throttle: uint8, 0=max, 255=min → invert and scale to int16
    s.axis[3] = scale_axis(255 - report[6], 0, 255);
    for (int i = 0; i < 4; i++) s.axis_changed[i] = true;

    // Buttons bits 0-5
    uint32_t cur = report[7] & 0x3F;

    // POV hat → virtual buttons in bits 8-11
    if (len >= 9) {
        uint8_t hat = report[8] & 0x0F;
        if (hat != 8) {
            if (hat==7||hat==0||hat==1) cur |= (1u <<  8);  // Up
            if (hat==1||hat==2||hat==3) cur |= (1u <<  9);  // Right
            if (hat==3||hat==4||hat==5) cur |= (1u << 10);  // Down
            if (hat==5||hat==6||hat==7) cur |= (1u << 11);  // Left
        }
    }

    s.buttons_changed = cur ^ s.buttons;
    s.buttons = cur;
    return true;
}

// ── Generic HID fallback ──────────────────────────────────────────────────
bool parse_generic(HIDDeviceState &s, const uint8_t *report, uint16_t len) {
    if (len < 3) return false;
    uint8_t offset = (report[0] > 0 && report[0] < 8) ? 1 : 0;
    const uint8_t *pay = report + offset;
    uint16_t plen = len - offset;
    uint8_t naxes = 0, i = 0;
    while (i+1 < plen && naxes < MAX_AXES) {
        if (pay[i]==0xFF && pay[i+1]==0xFF) { i+=2; continue; }
        s.axis[naxes] = read_i16(pay+i);
        s.axis_changed[naxes] = true;
        naxes++; i+=2;
    }
    s.axis_count = naxes;
    uint32_t cur = 0;
    for (uint8_t b=0; b<4 && i<plen; b++,i++) cur|=((uint32_t)pay[i]<<(b*8));
    s.buttons_changed = cur ^ s.buttons;
    s.buttons = cur; s.button_count = 16;
    return naxes > 0;
}