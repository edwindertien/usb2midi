/*
 * main.cpp — USB2MIDI multi-device with LittleFS config
 *
 * Each connected HID device gets a MIDI config loaded from
 * /mappings/<VID>_<PID>.json on LittleFS, falling back to
 * built-in defaults if no file exists.
 *
 * Device → MIDI channel mapping (defaults, all overridable):
 *   SpaceMouse        ch1   CC1-6    Note C2+
 *   Logitech F310     ch2   CC14-19  Note C3+
 *   Thrustmaster      ch3   CC20-23  Note C4+
 *   Generic / unknown ch4   CC30-37  Note C5+
 */

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include "tusb.h"
#include "usbh_helper.h"
#include "hid_device.h"
#include "device_registry.h"
#include "config.h"
#include "mapping.h"

#ifdef BUILD_MIDI
Adafruit_USBD_MIDI usb_midi;
#include <MIDI.h>
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);
#endif

// ── Device table — spin-lock protected ───────────────────────────────────
static HIDDevice    g_devices[MAX_DEVICES];
static spin_lock_t *g_lock = nullptr;

// Previous CC per slot/axis — 255 forces resend on connect
static uint8_t prev_cc[MAX_DEVICES][MAX_AXES];
// DeviceConfig per slot (loaded at mount time, read only after that)
static DeviceConfig g_cfg[MAX_DEVICES];

static int find_device(uint8_t dev_addr) {
    for (int i = 0; i < MAX_DEVICES; i++)
        if (g_devices[i].connected && g_devices[i].dev_addr == dev_addr) return i;
    return -1;
}
static int free_slot() {
    for (int i = 0; i < MAX_DEVICES; i++)
        if (!g_devices[i].connected) return i;
    return -1;
}

// Apply a loaded config to a device slot
static void apply_config(int slot, const DeviceConfig &cfg) {
    g_devices[slot].midi_ch   = cfg.midi_ch;
    g_devices[slot].cc_base   = cfg.cc_base;
    g_devices[slot].note_base = cfg.note_base;
    g_devices[slot].note_vel  = cfg.note_vel;
    g_devices[slot].deadzone  = cfg.deadzone;
    strlcpy(g_devices[slot].name, cfg.name, sizeof(g_devices[slot].name));
    g_cfg[slot] = cfg;
}

// ── Core 0: setup ─────────────────────────────────────────────────────────
void setup() {
    g_lock = spin_lock_instance(spin_lock_claim_unused(true));
    memset(prev_cc, 255, sizeof(prev_cc));

    pinMode(LED_BUILTIN, OUTPUT);

#ifdef BUILD_MIDI
    usb_midi.begin();
    MIDI.begin(MIDI_CHANNEL_OMNI);
#endif
    Serial.begin(115200);

    // LittleFS — non-fatal, falls back to defaults
    config_init();
}

// ── Core 0: loop ──────────────────────────────────────────────────────────
void loop() {
#ifdef BUILD_MIDI
    MIDI.read();
#endif

    // LED heartbeat
    static uint32_t last_blink = 0;
    bool any_conn = false, any_dirty = false;
    {
        uint32_t s = spin_lock_blocking(g_lock);
        for (int i = 0; i < MAX_DEVICES; i++) {
            if (g_devices[i].connected) any_conn = true;
            if (g_devices[i].connected && g_devices[i].dirty) any_dirty = true;
        }
        spin_unlock(g_lock, s);
    }
    uint32_t blink = any_conn ? (any_dirty ? 80u : 300u) : 1000u;
    if (millis() - last_blink > blink) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        last_blink = millis();
    }

    // Process each device slot
    for (int slot = 0; slot < MAX_DEVICES; slot++) {
        // Snapshot under lock
        uint32_t s = spin_lock_blocking(g_lock);
        if (!g_devices[slot].connected || !g_devices[slot].dirty) {
            spin_unlock(g_lock, s);
            continue;
        }
        HIDDevice snap = g_devices[slot];
        g_devices[slot].dirty = false;
        spin_unlock(g_lock, s);

        const DeviceConfig &cfg = g_cfg[slot];

        // ── MIDI output ───────────────────────────────────────────────────
#ifdef BUILD_MIDI
        for (int a = 0; a < snap.state.axis_count && a < MAX_AXES; a++) {
            if (!snap.state.axis_changed[a]) continue;
            uint8_t cc  = axis_cc(cfg, a);
            uint8_t val = axis_to_cc(snap.state.axis[a], cfg, a);
            if (val != prev_cc[slot][a]) {
                MIDI.sendControlChange(cc, val, snap.midi_ch);
                prev_cc[slot][a] = val;
            }
        }
        if (snap.state.buttons_changed) {
            for (int b = 0; b < snap.state.button_count && b < 32; b++) {
                if (!(snap.state.buttons_changed & (1u << b))) continue;
                uint8_t note = snap.note_base + b;
                if ((snap.state.buttons >> b) & 1)
                    MIDI.sendNoteOn(note, snap.note_vel, snap.midi_ch);
                else
                    MIDI.sendNoteOff(note, 0, snap.midi_ch);
            }
        }
#endif

        // ── Debug output ──────────────────────────────────────────────────
#ifdef BUILD_DEBUG
        if (!Serial) continue;
        static uint32_t last_print[MAX_DEVICES] = {};
        if (millis() - last_print[slot] < 50) continue;
        last_print[slot] = millis();
        Serial.printf("[%s ch%d] ", snap.name, snap.midi_ch);
        for (int a = 0; a < snap.state.axis_count && a < 6; a++)
            Serial.printf("A%d:%+5d(CC%d=%d) ",
                          a, snap.state.axis[a],
                          axis_cc(cfg, a),
                          axis_to_cc(snap.state.axis[a], cfg, a));
        if (snap.state.buttons)
            Serial.printf(" BTN:%08lX", (unsigned long)snap.state.buttons);
        Serial.println();
        Serial.flush();
#endif
    }
}

// ── Core 1 ────────────────────────────────────────────────────────────────
void setup1() { rp2040_configure_pio_usb(); USBHost.begin(1); }
void loop1()  { USBHost.task(); }

// ── TinyUSB host callbacks ─────────────────────────────────────────────────

extern "C" void tuh_mount_cb(uint8_t dev_addr) {
    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    uint8_t count = tuh_hid_instance_count(dev_addr);
    if (count == 0) return;

    bool armed = false;
    for (uint8_t inst = 0; inst < count; inst++)
        if (tuh_hid_receive_report(dev_addr, inst)) { armed = true; break; }
    if (!armed) return;

    // Load config on core 1 — LittleFS reads are safe from any core
    DeviceConfig cfg = config_load(vid, pid);

    uint32_t sv = spin_lock_blocking(g_lock);
    int slot = free_slot();
    if (slot >= 0) {
        g_devices[slot].clear();
        g_devices[slot].dev_addr  = dev_addr;
        g_devices[slot].vid       = vid;
        g_devices[slot].pid       = pid;
        g_devices[slot].connected = true;
        apply_config(slot, cfg);
        memset(prev_cc[slot], 255, MAX_AXES);
    }
    spin_unlock(g_lock, sv);
}

extern "C" void tuh_umount_cb(uint8_t dev_addr) {
    uint32_t sv = spin_lock_blocking(g_lock);
    int slot = find_device(dev_addr);
    if (slot >= 0) {
        // Send cleanup MIDI — can't call MIDI object from core 1,
        // so mark as disconnected and let core 0 handle cleanup
        g_devices[slot].connected = false;
        g_devices[slot].dirty     = false;
    }
    spin_unlock(g_lock, sv);
}

void tuh_hid_mount_cb(uint8_t, uint8_t, uint8_t const*, uint16_t) {}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t) {
    uint32_t sv = spin_lock_blocking(g_lock);
    int slot = find_device(dev_addr);
    if (slot >= 0) { g_devices[slot].connected = false; g_devices[slot].dirty = false; }
    spin_unlock(g_lock, sv);
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                 uint8_t const *report, uint16_t len) {
    uint32_t sv = spin_lock_blocking(g_lock);
    int slot = find_device(dev_addr);
    if (slot < 0) { spin_unlock(g_lock, sv); tuh_hid_receive_report(dev_addr, instance); return; }
    DeviceType type = classify_device(g_devices[slot].vid, g_devices[slot].pid);
    memset(g_devices[slot].state.axis_changed, 0, sizeof(g_devices[slot].state.axis_changed));
    g_devices[slot].state.buttons_changed = 0;
    HIDDeviceState local = g_devices[slot].state;
    spin_unlock(g_lock, sv);

    if (parse_report(type, local, report, len)) {
        uint32_t sv2 = spin_lock_blocking(g_lock);
        if (find_device(dev_addr) == slot) {
            g_devices[slot].state = local;
            g_devices[slot].dirty = true;
        }
        spin_unlock(g_lock, sv2);
    }

    tuh_hid_receive_report(dev_addr, instance);
}