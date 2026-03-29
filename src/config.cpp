#include "config.h"
#include "device_registry.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

static bool fs_ready = false;

bool config_init() {
    if (!LittleFS.begin()) {
        Serial.println("[cfg] LittleFS mount failed — using built-in defaults");
        return false;
    }
    // Create mappings directory if it doesn't exist
    if (!LittleFS.exists(CONFIG_DIR))
        LittleFS.mkdir(CONFIG_DIR);
    fs_ready = true;
    Serial.println("[cfg] LittleFS ready");
    return true;
}

// ── Built-in defaults per device type ────────────────────────────────────
static DeviceConfig default_config(uint16_t vid, uint16_t pid) {
    DeviceConfig cfg;
    DeviceType type = classify_device(vid, pid);

    // Name
    switch (type) {
    case DEV_SPACEMOUSE:   snprintf(cfg.name, sizeof(cfg.name), "SpaceMouse");        break;
    case DEV_F310:         snprintf(cfg.name, sizeof(cfg.name), "Logitech F310");     break;
    case DEV_THRUSTMASTER: snprintf(cfg.name, sizeof(cfg.name), "Thrustmaster JS");   break;
    default:               snprintf(cfg.name, sizeof(cfg.name), "%04X:%04X", vid, pid); break;
    }

    // MIDI config
    switch (type) {
    case DEV_SPACEMOUSE:
        cfg.midi_ch   = 1;
        cfg.cc_base   = 1;    // CC1-CC6
        cfg.note_base = 36;   // C2
        cfg.deadzone  = 800;
        break;
    case DEV_F310:
        cfg.midi_ch   = 2;
        cfg.cc_base   = 14;   // CC14-CC19
        cfg.note_base = 48;   // C3
        cfg.deadzone  = 1000;
        break;
    case DEV_THRUSTMASTER:
        cfg.midi_ch   = 3;
        cfg.cc_base   = 20;   // CC20-CC23
        cfg.note_base = 60;   // C4
        cfg.deadzone  = 600;
        break;
    default:
        cfg.midi_ch   = 4;
        cfg.cc_base   = 30;   // CC30-CC37
        cfg.note_base = 72;   // C5
        cfg.deadzone  = 500;
        break;
    }

    cfg.note_vel = 100;
    cfg.loaded   = false;
    return cfg;
}

DeviceConfig config_load(uint16_t vid, uint16_t pid) {
    DeviceConfig cfg = default_config(vid, pid);

    if (!fs_ready) return cfg;

    char path[48];
    snprintf(path, sizeof(path), "%s/%04X_%04X.json", CONFIG_DIR, vid, pid);

    File f = LittleFS.open(path, "r");
    if (!f) return cfg;   // no file — return defaults

    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("[cfg] JSON error in %s: %s\n", path, err.c_str());
        return cfg;
    }

    // Override defaults with whatever is in the JSON
    if (doc.containsKey("name"))      strlcpy(cfg.name, doc["name"], sizeof(cfg.name));
    if (doc.containsKey("midi_ch"))   cfg.midi_ch   = doc["midi_ch"];
    if (doc.containsKey("cc_base"))   cfg.cc_base   = doc["cc_base"];
    if (doc.containsKey("note_base")) cfg.note_base = doc["note_base"];
    if (doc.containsKey("note_vel"))  cfg.note_vel  = doc["note_vel"];
    if (doc.containsKey("deadzone"))  cfg.deadzone  = doc["deadzone"];

    // Per-axis overrides
    if (doc.containsKey("axes")) {
        JsonArray axes = doc["axes"];
        uint8_t i = 0;
        for (JsonObject ax : axes) {
            if (i >= MAX_AXES) break;
            if (ax.containsKey("cc"))     cfg.axes[i].cc     = ax["cc"];
            if (ax.containsKey("invert")) cfg.axes[i].invert = ax["invert"];
            if (ax.containsKey("scale"))  cfg.axes[i].scale  = ax["scale"];
            i++;
        }
    }

    cfg.loaded = true;
    Serial.printf("[cfg] Loaded %s → ch%d cc_base=%d note_base=%d\n",
                  path, cfg.midi_ch, cfg.cc_base, cfg.note_base);
    return cfg;
}

bool config_save(uint16_t vid, uint16_t pid, const DeviceConfig &cfg) {
    if (!fs_ready) return false;

    char path[48];
    snprintf(path, sizeof(path), "%s/%04X_%04X.json", CONFIG_DIR, vid, pid);

    File f = LittleFS.open(path, "w");
    if (!f) return false;

    StaticJsonDocument<1024> doc;
    doc["name"]      = cfg.name;
    doc["midi_ch"]   = cfg.midi_ch;
    doc["cc_base"]   = cfg.cc_base;
    doc["note_base"] = cfg.note_base;
    doc["note_vel"]  = cfg.note_vel;
    doc["deadzone"]  = cfg.deadzone;

    JsonArray axes = doc.createNestedArray("axes");
    for (uint8_t i = 0; i < MAX_AXES; i++) {
        JsonObject ax = axes.createNestedObject();
        ax["cc"]     = cfg.axes[i].cc;
        ax["invert"] = cfg.axes[i].invert;
        ax["scale"]  = cfg.axes[i].scale;
    }

    serializeJsonPretty(doc, f);
    f.close();
    return true;
}