#pragma once
/*
 * neo.h — WS2812B single LED on GP16
 *
 * Drives the WS2812B using direct GPIO bit-banging timed for 120MHz.
 * No PIO library — zero conflict with pio-usb or anything else.
 *
 * Timing at 120MHz (1 cycle = 8.33ns):
 *   T0H ~350ns = ~42 nops    T0L ~800ns = ~96 nops
 *   T1H ~700ns = ~84 nops    T1L ~600ns = ~72 nops
 *   Reset > 50µs low (delayMicroseconds)
 *
 * Total per-LED transmission: 24 bits × ~1.25µs = ~30µs
 * Interrupts disabled only during that 30µs window.
 *
 * MUST only be called from core 0.
 *
 * Colour scheme:
 *   Red          = powered, no device
 *   Blue         = device connected, idle
 *   Green flash  = MIDI data sent
 *   Cyan flash   = device mounted
 *   Orange flash = device unmounted
 */

#include <Arduino.h>
#include <hardware/gpio.h>
#include <hardware/sync.h>

#define NEO_PIN  16
#define NEO_DIM  6    // max brightness per channel (0-255)

class NeoStatus {
public:
    void begin() {
        gpio_init(NEO_PIN);
        gpio_set_dir(NEO_PIN, GPIO_OUT);
        gpio_put(NEO_PIN, 0);
        _write(0, 0, 0);
    }

    // Call every loop() — handles flash timeouts
    void update() {
        uint32_t now = millis();
        if (_flash_until && now >= _flash_until) {
            _flash_until = 0;
            _write(_base_r, _base_g, _base_b);
        }
    }

    void setBase(uint8_t r, uint8_t g, uint8_t b) {
        _base_r = r; _base_g = g; _base_b = b;
        if (!_flash_until) _write(r, g, b);
    }

    void flash(uint8_t r, uint8_t g, uint8_t b, uint32_t ms = 40) {
        _write(r, g, b);
        _flash_until = millis() + ms;
    }

    void powered()      { setBase(NEO_DIM, 0, 0);           }  // red
    void connected()    { setBase(0, 0, NEO_DIM);           }  // blue
    void midiOut()      { flash(0, NEO_DIM, 0, 40);         }  // green
    void mountFlash()   { flash(0, NEO_DIM, NEO_DIM, 80);   }  // cyan
    void unmountFlash() { flash(NEO_DIM, NEO_DIM/2, 0, 80); }  // orange

private:
    uint8_t  _base_r = 0, _base_g = 0, _base_b = 0;
    uint32_t _flash_until = 0;

    // Send 1 bit — tight nop loops calibrated for 120MHz
    __attribute__((noinline, optimize("O1")))
    void _bit(bool b) {
        if (b) {
            gpio_put(NEO_PIN, 1);
            for (volatile int i = 0; i < 9; i++) __asm("nop");  // ~700ns
            gpio_put(NEO_PIN, 0);
            for (volatile int i = 0; i < 7; i++) __asm("nop");  // ~600ns
        } else {
            gpio_put(NEO_PIN, 1);
            for (volatile int i = 0; i < 4; i++) __asm("nop");  // ~350ns
            gpio_put(NEO_PIN, 0);
            for (volatile int i = 0; i < 10; i++) __asm("nop"); // ~800ns
        }
    }

    void _write(uint8_t r, uint8_t g, uint8_t b) {
        uint32_t irq = save_and_disable_interrupts();
        // This module uses RGB order (not standard GRB)
        for (int i = 7; i >= 0; i--) _bit((r >> i) & 1);
        for (int i = 7; i >= 0; i--) _bit((g >> i) & 1);
        for (int i = 7; i >= 0; i--) _bit((b >> i) & 1);
        restore_interrupts(irq);
        gpio_put(NEO_PIN, 0);
        delayMicroseconds(60);  // latch
    }
};

extern NeoStatus neo;