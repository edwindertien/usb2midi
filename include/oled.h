#pragma once
/*
 * oled.h — SSD1306 64×32 OLED display for USB2MIDI
 *
 * Wiring: GP4=SDA, GP5=SCL — I2C0 (Wire), addr 0x3C
 *
 * Layout (64×32 pixels):
 *
 *  x=0        x=23  x=24              x=63
 *  |<- axes ->|      |<-- buttons -->|
 *  | 6 bars   |      | 4×4px dots    |
 *  | 3px+1gap |      | 7 cols×4 rows |
 *
 * Axis bars: 3px wide, full 32px height, centre at y=16
 * Button dots: 4×4px, 1px gap, filled=pressed, outline=released
 * Refresh: 5fps (200ms) — 5ms I2C block every 200ms = 2.5% overhead
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

#define BAR_W        3
#define BAR_GAP      1
#define BAR_STRIDE   (BAR_W + BAR_GAP)          // 4px per bar
#define BAR_COUNT    6
#define BAR_AREA_W   (BAR_COUNT * BAR_STRIDE - BAR_GAP)  // 23px
#define BAR_CENTRE   (OLED_H / 2)               // y=16

#define BTN_X_START  25
#define BTN_SIZE     4
#define BTN_GAP      1
#define BTN_STRIDE   (BTN_SIZE + BTN_GAP)        // 5px
#define BTN_COLS     7
#define BTN_ROWS     4
#define BTN_Y_START  1

class OLEDDisplay {
public:
    bool begin() {
        // I2C0 on GP4(SDA)/GP5(SCL) — use Wire, not Wire1
        Wire.setSDA(OLED_SDA);
        Wire.setSCL(OLED_SCL);
        Wire.begin();
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

    // Call from loop() only — blocking I2C, core 0 only
    void update(const HIDDevice *devices, int ndevices) {
        if (!_ok) return;
        uint32_t now = millis();
        if (now - _last_update < 200) return;  // 5fps
        _last_update = now;

        _disp.clearDisplay();

        // Find first connected device
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
    Adafruit_SSD1306 _disp{OLED_W, OLED_H, &Wire, -1};
    bool     _ok          = false;
    uint32_t _last_update = 0;

    void _draw_idle() {
        _disp.drawFastHLine(0, BAR_CENTRE, BAR_AREA_W, SSD1306_WHITE);
        _disp.drawFastHLine(BTN_X_START, 14, 14, SSD1306_WHITE);
        _disp.drawFastHLine(BTN_X_START, 16, 14, SSD1306_WHITE);
        _disp.drawFastHLine(BTN_X_START, 18, 14, SSD1306_WHITE);
    }

    void _draw_axes(const HIDDeviceState &s) {
        // Centre line across all bars
        _disp.drawFastHLine(0, BAR_CENTRE, BAR_AREA_W, SSD1306_WHITE);

        for (int a = 0; a < BAR_COUNT && a < s.axis_count; a++) {
            int x = a * BAR_STRIDE;
            int32_t pix = (int32_t)(s.axis[a]) * (BAR_CENTRE - 1) / 32767;
            if (pix >  BAR_CENTRE-1) pix =  BAR_CENTRE-1;
            if (pix < -(BAR_CENTRE-1)) pix = -(BAR_CENTRE-1);

            if (pix > 0)
                _disp.fillRect(x, BAR_CENTRE - (int)pix, BAR_W, (int)pix, SSD1306_WHITE);
            else if (pix < 0)
                _disp.fillRect(x, BAR_CENTRE, BAR_W, (int)(-pix), SSD1306_WHITE);
            else
                _disp.drawFastHLine(x, BAR_CENTRE, BAR_W, SSD1306_WHITE);
        }
    }

    void _draw_buttons(const HIDDeviceState &s) {
        int max_btns = BTN_COLS * BTN_ROWS;
        for (int b = 0; b < max_btns && b < s.button_count && b < 32; b++) {
            int col = b % BTN_COLS;
            int row = b / BTN_COLS;
            int x   = BTN_X_START + col * BTN_STRIDE;
            int y   = BTN_Y_START + row * BTN_STRIDE;
            if ((s.buttons >> b) & 1)
                _disp.fillRect(x, y, BTN_SIZE, BTN_SIZE, SSD1306_WHITE);
            else
                _disp.drawRect(x, y, BTN_SIZE, BTN_SIZE, SSD1306_WHITE);
        }
    }
};

extern OLEDDisplay oled;