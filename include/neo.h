#pragma once
/*
 * neo.h — Single WS2812B neopixel status indicator
 *
 * Hardware: WS2812B on GPIO16 (confirmed from Waveshare RP2350-USB-A schematic)
 * Library:  Adafruit NeoPixel (in platformio.ini lib_deps)
 *
 * Colour semantics:
 *   Idle (no device)    : slow breathing blue
 *   Device connected    : dim steady green
 *   HID data received   : brief red flash   (rxFlash)
 *   MIDI data sent      : brief green flash  (txFlash)
 *   Device mounted      : cyan flash         (mountFlash)
 *   Device removed      : orange flash       (unmountFlash)
 *   Error               : long red flash     (errorFlash)
 */

#include <Adafruit_NeoPixel.h>

#define NEO_PIN    16
#define NEO_COUNT  1
#define NEO_BRIGHT 40   // 0-255, WS2812B at full is very bright

class NeoStatus {
public:
    void begin() {
        _px.begin();
        _px.setBrightness(NEO_BRIGHT);
        _px.clear();
        _px.show();
    }

    // Call every loop() — handles timed flashes and breathing non-blocking
    void update() {
        uint32_t now = millis();

        // Flash timeout — return to base colour
        if (_flash_until && now >= _flash_until) {
            _flash_until = 0;
            _show(_base_r, _base_g, _base_b);
        }

        // Breathing when idle and no flash active
        if (!_flash_until && _breathe) {
            if (now - _last_breathe > 16) {  // ~60fps
                float t = (float)(now % 3000) / 3000.0f;
                float v = 0.5f + 0.5f * sinf(t * 6.2832f);
                _show((uint8_t)(_base_r * v),
                      (uint8_t)(_base_g * v),
                      (uint8_t)(_base_b * v));
                _last_breathe = now;
            }
        }
    }

    // Set persistent base colour shown between flashes
    void setBase(uint8_t r, uint8_t g, uint8_t b, bool breathe = false) {
        _base_r = r; _base_g = g; _base_b = b;
        _breathe = breathe;
        if (!_flash_until) _show(r, g, b);
    }

    // Momentary flash overriding base colour
    void flash(uint8_t r, uint8_t g, uint8_t b, uint32_t ms = 30) {
        _show(r, g, b);
        _flash_until = millis() + ms;
    }

    // ── Semantic helpers ──────────────────────────────────────────────────
    void idle()          { setBase(0,  0, 20, true);  }  // breathing blue
    void connected()     { setBase(0,  8,  0, false); }  // dim green
    void rxFlash()       { flash(40,  0,  0,  20);    }  // red:    HID in
    void txFlash()       { flash( 0, 40,  0,  20);    }  // green:  MIDI out
    void mountFlash()    { flash( 0, 20, 40,  80);    }  // cyan:   device mounted
    void unmountFlash()  { flash(40, 10,  0,  80);    }  // orange: device removed
    void errorFlash()    { flash(40,  0,  0, 200);    }  // long red: error

private:
    Adafruit_NeoPixel _px{NEO_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800};
    uint8_t  _base_r = 0, _base_g = 0, _base_b = 0;
    bool     _breathe      = false;
    uint32_t _flash_until  = 0;
    uint32_t _last_breathe = 0;

    void _show(uint8_t r, uint8_t g, uint8_t b) {
        _px.setPixelColor(0, _px.Color(r, g, b));
        _px.show();
    }
};

// Global instance — defined in main.cpp as: NeoStatus neo;
extern NeoStatus neo;