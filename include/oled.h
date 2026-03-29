#pragma once
/*
 * oled.h — SSD1306 64×32 OLED display for USB2MIDI
 *
 * Wiring: GP4=SDA, GP5=SCL, I2C addr 0x3C
 *
 * Layout (64×32 pixels):
 *
 *  x=0          x=23  x=24                x=63
 *  |<-- axes ---|----->|<--- buttons ------>|
 *  |  6 bars    |      | up to 20 dots      |
 *  | 3px + 1gap |      | 4×4px, 1px gap     |
 *
 * Axis bars:
 *   Each bar is 3px wide, full 32px height
 *   Centre line at y=16 (rest position)
 *   Value fills from centre up (positive) or down (negative)
 *   Bar x positions: 0, 4, 8, 12, 16, 20
 *
 * Button dots:
 *   4×4px dots, 1px gap between
 *   5 columns × 4 rows = 20 buttons max
 *   x start: 25, y start: 0
 *   Lit = button pressed, dim = released
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "hid_device.h"

#define OLED_W       64
#define OLED_H       32
#define OLED_ADDR    0x3C
#define OLED_SDA     4
#define OLED_SCL     5

#define BAR_W        3      // bar width in pixels
#define BAR_GAP      1      // gap between bars
#define BAR_STRIDE   (BAR_W + BAR_GAP)   // 4
#define BAR_COUNT    6
#define BAR_AREA_W   (BAR_COUNT * BAR_STRIDE - BAR_GAP)  // 23
#define BAR_H        OLED_H  // full height
#define BAR_CENTRE   (OLED_H / 2)  // y=16

#define BTN_X_START  25     // first button column x
#define BTN_SIZE     4      // dot size
#define BTN_GAP      1      // gap between dots
#define BTN_STRIDE   (BTN_SIZE + BTN_GAP)  // 5
#define BTN_COLS     7      // fits in 41px: 7×5-1=34, leaving margin
#define BTN_ROWS     4      // 4×5-1=19, fits in 32px with 13px spare... use 5px margin top
#define BTN_Y_START  1      // small top margin

class OLEDDisplay {
public:
    bool begin() {
        Wire1.setSDA(OLED_SDA);
        Wire1.setSCL(OLED_SCL);
        Wire1.begin();
        if (!_disp.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR, false, false)) {
            return false;
        }
        _disp.setRotation(0);
        _disp.clearDisplay();
        _disp.display();
        _ok = true;
        return true;
    }

    bool ok() const { return _ok; }

    // Update display from current device state
    // Call from loop() at a modest rate (~30fps max) to avoid I2C overhead
    void update(const HIDDevice *devices, int ndevices) {
        if (!_ok) return;
        uint32_t now = millis();
        if (now - _last_update < 33) return;  // ~30fps cap
        _last_update = now;

        _disp.clearDisplay();

        // Find first connected device to display
        // (Future: multi-device cycling)
        const HIDDevice *dev = nullptr;
        for (int i = 0; i < ndevices; i++) {
            if (devices[i].connected) { dev = &devices[i]; break; }
        }

        if (!dev) {
            _draw_idle();
        } else {
            _draw_axes(dev->state);
            _draw_buttons(dev->state);
        }

        _disp.display();
    }

private:
    Adafruit_SSD1306 _disp{OLED_W, OLED_H, &Wire1, -1};
    bool     _ok          = false;
    uint32_t _last_update = 0;

    void _draw_idle() {
        // Small "waiting" indicator — just a dim horizontal line at centre
        _disp.drawFastHLine(0, BAR_CENTRE, BAR_AREA_W, SSD1306_WHITE);
        // "---" in button area
        _disp.drawFastHLine(BTN_X_START, 14, 14, SSD1306_WHITE);
        _disp.drawFastHLine(BTN_X_START, 16, 14, SSD1306_WHITE);
        _disp.drawFastHLine(BTN_X_START, 18, 14, SSD1306_WHITE);
    }

    void _draw_axes(const HIDDeviceState &s) {
        // Centre divider — faint line across all bars
        _disp.drawFastHLine(0, BAR_CENTRE, BAR_AREA_W, SSD1306_WHITE);

        for (int a = 0; a < BAR_COUNT && a < s.axis_count; a++) {
            int x = a * BAR_STRIDE;

            // Map axis value (-32767..+32767) to pixel offset from centre
            // Positive = up (smaller y), negative = down (larger y)
            int32_t val = s.axis[a];
            int32_t half = BAR_CENTRE - 1;  // 15px each direction
            int32_t pix  = (int32_t)(val * half / 32767);
            if (pix >  half) pix =  half;
            if (pix < -half) pix = -half;

            // Draw bar: from centre toward top for positive, toward bottom for negative
            if (pix > 0) {
                // positive: y goes from (centre - pix) to centre
                _disp.fillRect(x, BAR_CENTRE - (int)pix, BAR_W, (int)pix, SSD1306_WHITE);
            } else if (pix < 0) {
                // negative: y goes from centre to (centre + |pix|)
                _disp.fillRect(x, BAR_CENTRE, BAR_W, (int)(-pix), SSD1306_WHITE);
            } else {
                // at rest — draw single-pixel tick at centre
                _disp.fillRect(x, BAR_CENTRE, BAR_W, 1, SSD1306_WHITE);
            }
        }

        // Vertical separators between bars (optional, only if space)
        // (skipped — bars are already visually distinct by the gap)
    }

    void _draw_buttons(const HIDDeviceState &s) {
        int max_btns = BTN_COLS * BTN_ROWS;
        for (int b = 0; b < max_btns && b < s.button_count && b < 32; b++) {
            int col = b % BTN_COLS;
            int row = b / BTN_COLS;
            int x   = BTN_X_START + col * BTN_STRIDE;
            int y   = BTN_Y_START + row * BTN_STRIDE;

            bool pressed = (s.buttons >> b) & 1;
            if (pressed) {
                _disp.fillRect(x, y, BTN_SIZE, BTN_SIZE, SSD1306_WHITE);
            } else {
                // Just draw a 1px border for released buttons
                _disp.drawRect(x, y, BTN_SIZE, BTN_SIZE, SSD1306_WHITE);
            }
        }
    }
};

extern OLEDDisplay oled;