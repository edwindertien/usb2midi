/*
 * main.cpp — USB2MIDI multi-device
 * USB-C: composite CDC serial + USB-MIDI
 * USB-A: HID host via pio-usb (GP12/13)
 */

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <LittleFS.h>
#include "tusb.h"
#include "usbh_helper.h"
#include "hid_device.h"
#include "device_registry.h"
#include "config.h"
#include "mapping.h"

// ── USB-C device side ────────────────────────────────────────────────────
Adafruit_USBD_MIDI usb_midi;
#include <MIDI.h>
MIDI_CREATE_INSTANCE(Adafruit_USBD_MIDI, usb_midi, MIDI);

// ── Device table ──────────────────────────────────────────────────────────
static HIDDevice    g_devices[MAX_DEVICES];
static spin_lock_t *g_lock = nullptr;
static uint8_t      prev_cc[MAX_DEVICES][MAX_AXES];
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
static void apply_config(int slot, const DeviceConfig &cfg) {
    g_devices[slot].midi_ch   = cfg.midi_ch;
    g_devices[slot].cc_base   = cfg.cc_base;
    g_devices[slot].note_base = cfg.note_base;
    g_devices[slot].note_vel  = cfg.note_vel;
    g_devices[slot].deadzone  = cfg.deadzone;
    strlcpy(g_devices[slot].name, cfg.name, sizeof(g_devices[slot].name));
    g_cfg[slot] = cfg;
}

// ── Event queue: core 1 → core 0 (serial is core 0 only) ─────────────────
enum EventType : uint8_t { EVT_MOUNT=1, EVT_UMOUNT=2, EVT_ARMED=3, EVT_NOARM=4 };
struct Event { EventType type; uint8_t dev_addr; uint16_t vid, pid; int8_t slot; };
static volatile uint8_t evt_head = 0, evt_tail = 0;
static Event evt_buf[8];
static void evt_push(Event e) {
    uint8_t next = (evt_head+1) & 7;
    if (next != evt_tail) { evt_buf[evt_head] = e; evt_head = next; }
}
static bool evt_pop(Event &e) {
    if (evt_tail == evt_head) return false;
    e = evt_buf[evt_tail]; evt_tail = (evt_tail+1) & 7; return true;
}

// ── Raw report dump buffer (XInput diagnostics, written core 1, read core 0)
static uint8_t          raw_buf[20];
static volatile uint8_t raw_len        = 0;
static volatile bool    raw_ready      = false;
static volatile bool    raw_dump_reset = false;  // set by core 0 on unmount

// ── LED (built-in GPIO LED, not neopixel) ────────────────────────────────
static void led_update(bool any_connected, bool any_dirty) {
    static uint32_t last = 0;
    uint32_t interval = any_dirty ? 80u : any_connected ? 300u : 1000u;
    if (millis() - last > interval) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        last = millis();
    }
}

// ── Pending mount queue: core 1 signals, core 0 calls config_load ─────────
// LittleFS is not safe to call from core 1, so config_load runs on core 0.
struct PendingMount { uint8_t dev_addr; uint16_t vid, pid; bool active; };
static PendingMount g_pending[MAX_DEVICES] = {};

// ── Core 0: setup ─────────────────────────────────────────────────────────
void setup() {
    g_lock = spin_lock_instance(spin_lock_claim_unused(true));
    memset(prev_cc, 255, sizeof(prev_cc));

    pinMode(LED_BUILTIN, OUTPUT);

    // USB device stack first — nothing else before this
    usb_midi.begin();
    MIDI.begin(MIDI_CHANNEL_OMNI);
    Serial.begin(115200);

    // LittleFS — non-fatal
    config_init();
}

// ── Core 0: loop ──────────────────────────────────────────────────────────
void loop() {
    MIDI.read();

    // Compute LED state
    bool any_conn = false, any_dirty = false;
    {
        uint32_t s = spin_lock_blocking(g_lock);
        for (int i = 0; i < MAX_DEVICES; i++) {
            if (g_devices[i].connected) any_conn = true;
            if (g_devices[i].connected && g_devices[i].dirty) any_dirty = true;
        }
        spin_unlock(g_lock, s);
    }
    led_update(any_conn, any_dirty);

    // ── Boot banner ───────────────────────────────────────────────────────
    static bool banner_done = false;
    if (!banner_done && Serial) {
        Serial.println("\r\n=== USB2MIDI Waveshare RP2350-USB-A ===");
        Serial.printf ("Host: GP%d(D+) GP%d(D-)\r\n",
                       PIN_USB_HOST_DP, PIN_USB_HOST_DP+1);
        File dir = LittleFS.open("/mappings", "r");
        if (dir && dir.isDirectory()) {
            Serial.println("LittleFS: OK");
            Serial.println("Mappings:");
            File f = dir.openNextFile();
            while (f) {
                Serial.printf("  %-24s %lu bytes\r\n",
                              f.name(), (unsigned long)f.size());
                f = dir.openNextFile();
            }
        } else {
            Serial.println("LittleFS: no /mappings - using built-in defaults");
        }
        Serial.println("Waiting for HID devices on USB-A...\r\n");
        Serial.flush();
        banner_done = true;
    }

    // ── Event queue drain ─────────────────────────────────────────────────
    Event evt;
    while (evt_pop(evt)) {
        switch (evt.type) {
        case EVT_MOUNT:
            if (Serial) {
                DeviceType t = classify_device(evt.vid, evt.pid);
                Serial.printf("[+] Mount   dev=%u  %04X:%04X  %s\r\n",
                              evt.dev_addr, evt.vid, evt.pid,
                              device_type_name(t));
                Serial.flush();
            }
            break;
        case EVT_ARMED:
            if (Serial && evt.slot >= 0) {
                uint32_t sv = spin_lock_blocking(g_lock);
                char name[32];
                strlcpy(name, g_devices[evt.slot].name, sizeof(name));
                uint8_t ch = g_devices[evt.slot].midi_ch;
                uint8_t cc = g_devices[evt.slot].cc_base;
                uint8_t nb = g_devices[evt.slot].note_base;
                spin_unlock(g_lock, sv);
                Serial.printf("    \"%s\"  ch=%d  cc_base=%d  note_base=%d  [%s]\r\n",
                              name, ch, cc, nb,
                              g_cfg[evt.slot].loaded ? "JSON" : "default");
                Serial.flush();
            }
            break;
        case EVT_NOARM:
            if (Serial) {
                // We reuse EVT_NOARM with vid=count as a diagnostic when vid < 8
                if (evt.vid < 8 && evt.pid == 0)
                    Serial.printf("    HID instance count = %u\r\n", evt.vid);
                else
                    Serial.printf("    WARNING: failed to arm HID dev=%u\r\n", evt.dev_addr);
                Serial.flush();
            }
            break;
        case EVT_UMOUNT:
            if (Serial) {
                Serial.printf("[-] Unmount dev=%u  %04X:%04X\r\n",
                              evt.dev_addr, evt.vid, evt.pid);
                Serial.flush();
            }
            raw_dump_reset = true;  // reset XInput dump counter for next plug
            break;
        }
    }

    // ── Pending mounts — complete on core 0 (config_load is LittleFS) ────────
    for (auto &p : g_pending) {
        if (!p.active) continue;
        p.active = false;
        DeviceConfig cfg = config_load(p.vid, p.pid);
        uint32_t sv = spin_lock_blocking(g_lock);
        int slot = free_slot();
        if (slot >= 0) {
            g_devices[slot].clear();
            g_devices[slot].dev_addr  = p.dev_addr;
            g_devices[slot].vid       = p.vid;
            g_devices[slot].pid       = p.pid;
            g_devices[slot].connected = true;
            apply_config(slot, cfg);
            memset(prev_cc[slot], 255, MAX_AXES);
        }
        spin_unlock(g_lock, sv);
        evt_push({EVT_ARMED, p.dev_addr, p.vid, p.pid, (int8_t)(slot)});
    }

    // ── Raw XInput report dump ────────────────────────────────────────────
    if (raw_ready && Serial) {
        Serial.printf("[raw XInput %u bytes] ", raw_len);
        for (uint8_t i = 0; i < raw_len; i++) Serial.printf("%02X ", raw_buf[i]);
        Serial.println();
        Serial.flush();
        raw_ready = false;
    }

    // ── Process device slots ──────────────────────────────────────────────
    for (int slot = 0; slot < MAX_DEVICES; slot++) {
        uint32_t s = spin_lock_blocking(g_lock);
        if (!g_devices[slot].connected || !g_devices[slot].dirty) {
            spin_unlock(g_lock, s); continue;
        }
        HIDDevice snap = g_devices[slot];
        g_devices[slot].dirty = false;
        spin_unlock(g_lock, s);

        const DeviceConfig &cfg = g_cfg[slot];

        // MIDI CC
        for (int a = 0; a < snap.state.axis_count && a < MAX_AXES; a++) {
            if (!snap.state.axis_changed[a]) continue;
            uint8_t cc  = axis_cc(cfg, a);
            uint8_t val = axis_to_cc(snap.state.axis[a], cfg, a);
            if (val != prev_cc[slot][a]) {
                MIDI.sendControlChange(cc, val, snap.midi_ch);
                prev_cc[slot][a] = val;
            }
        }
        // MIDI notes
        if (snap.state.buttons_changed) {
            for (int b = 0; b < snap.state.button_count && b < 32; b++) {
                if (!(snap.state.buttons_changed & (1u<<b))) continue;
                uint8_t note = snap.note_base + b;
                if ((snap.state.buttons >> b) & 1)
                    MIDI.sendNoteOn(note, snap.note_vel, snap.midi_ch);
                else
                    MIDI.sendNoteOff(note, 0, snap.midi_ch);
            }
        }

        // CDC debug
        if (!Serial) continue;
        static uint32_t last_print[MAX_DEVICES] = {};
        if (millis() - last_print[slot] < 50) continue;
        last_print[slot] = millis();
        Serial.printf("[%s ch%d] ", snap.name, snap.midi_ch);
        for (int a = 0; a < snap.state.axis_count && a < 6; a++)
            Serial.printf("A%d:%+5d->%3d ",
                          a, snap.state.axis[a],
                          axis_to_cc(snap.state.axis[a], cfg, a));
        if (snap.state.buttons)
            Serial.printf("BTN:%08lX", (unsigned long)snap.state.buttons);
        Serial.println();
        Serial.flush();
    }
}

// ── Core 1: USB-A host ────────────────────────────────────────────────────
void setup1() { rp2040_configure_pio_usb(); USBHost.begin(1); }
void loop1()  { USBHost.task(); }

// ── TinyUSB callbacks — core 1, no Serial ────────────────────────────────

extern "C" void tuh_mount_cb(uint8_t dev_addr) {
    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    evt_push({EVT_MOUNT, dev_addr, vid, pid, -1});

    uint8_t count = tuh_hid_instance_count(dev_addr);
    evt_push({EVT_NOARM, dev_addr, (uint16_t)count, 0, -1}); // log count

    bool armed = false;
    for (uint8_t inst = 0; inst < count; inst++)
        if (tuh_hid_receive_report(dev_addr, inst)) { armed = true; break; }
    if (!armed) { evt_push({EVT_NOARM, dev_addr, vid, pid, -1}); return; }

    // Don't call config_load here — LittleFS is not core 1 safe.
    // Signal core 0 to complete the mount with a pending entry.
    for (auto &p : g_pending) {
        if (!p.active) {
            p = {dev_addr, vid, pid, true};
            break;
        }
    }
}

extern "C" void tuh_umount_cb(uint8_t dev_addr) {
    uint16_t vid = 0, pid = 0;
    uint32_t sv = spin_lock_blocking(g_lock);
    int slot = find_device(dev_addr);
    if (slot >= 0) {
        vid = g_devices[slot].vid;
        pid = g_devices[slot].pid;
        g_devices[slot].connected = false;
        g_devices[slot].dirty     = false;
    }
    spin_unlock(g_lock, sv);
    evt_push({EVT_UMOUNT, dev_addr, vid, pid, (int8_t)slot});
}

void tuh_hid_mount_cb(uint8_t, uint8_t, uint8_t const*, uint16_t) {}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t) {
    uint32_t sv = spin_lock_blocking(g_lock);
    int slot = find_device(dev_addr);
    if (slot >= 0) {
        g_devices[slot].connected = false;
        g_devices[slot].dirty     = false;
    }
    spin_unlock(g_lock, sv);
    evt_push({EVT_UMOUNT, dev_addr, 0, 0, (int8_t)slot});
}

// ── Raw report dump buffer (for XInput diagnostics) ──────────────────────

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                 uint8_t const *report, uint16_t len) {
    uint32_t sv = spin_lock_blocking(g_lock);
    int slot = find_device(dev_addr);
    if (slot < 0) {
        spin_unlock(g_lock, sv);
        tuh_hid_receive_report(dev_addr, instance);
        return;
    }
    DeviceType type = classify_device(g_devices[slot].vid, g_devices[slot].pid);
    memset(g_devices[slot].state.axis_changed, 0, MAX_AXES);
    g_devices[slot].state.buttons_changed = 0;
    HIDDeviceState local = g_devices[slot].state;
    spin_unlock(g_lock, sv);

    // Capture first 5 raw reports for XInput so core 0 can print them
    if (type == DEV_F310_XI && !raw_ready) {
        static uint8_t n = 0;
        if (raw_dump_reset) { n = 0; raw_dump_reset = false; }
        if (n < 5) {
            n++;
            raw_len = (uint8_t)(len < sizeof(raw_buf) ? len : sizeof(raw_buf));
            memcpy(raw_buf, report, raw_len);
            raw_ready = true;
        }
    }

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