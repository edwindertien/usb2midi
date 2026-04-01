#include "config.h"
#include "device_registry.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

static bool fs_ready = false;

bool config_init() {
    if (!LittleFS.begin()) {
        return false;
    }
    if (!LittleFS.exists(CONFIG_DIR))
        LittleFS.mkdir(CONFIG_DIR);
    fs_ready = true;
    return true;
}

// ── Built-in defaults per device type ────────────────────────────────────
static DeviceConfig default_config(uint16_t vid, uint16_t pid) {
    DeviceConfig cfg;
    DeviceType type = classify_device(vid, pid);

    switch (type) {
    case DEV_SPACEMOUSE:   snprintf(cfg.name, sizeof(cfg.name), "SpaceMouse");      break;
    case DEV_F310_DI:      snprintf(cfg.name, sizeof(cfg.name), "F310 DInput");     break;
    case DEV_F310_XI:      snprintf(cfg.name, sizeof(cfg.name), "F310 XInput");     break;
    case DEV_THRUSTMASTER: snprintf(cfg.name, sizeof(cfg.name), "Thrustmaster JS"); break;
    case DEV_BETAFPV:      snprintf(cfg.name, sizeof(cfg.name), "BetaFPV JS");      break;
    default:               snprintf(cfg.name, sizeof(cfg.name), "%04X:%04X", vid, pid); break;
    }

    switch (type) {
    case DEV_SPACEMOUSE:
        cfg.midi_ch=1; cfg.cc_base=1;  cfg.note_base=36; cfg.deadzone=800;  break;
    case DEV_F310_DI:
    case DEV_F310_XI:
        cfg.midi_ch=2; cfg.cc_base=14; cfg.note_base=48; cfg.deadzone=1500; break;
    case DEV_THRUSTMASTER:
        cfg.midi_ch=3; cfg.cc_base=20; cfg.note_base=60; cfg.deadzone=600;  break;
    case DEV_BETAFPV:
        cfg.midi_ch=4; cfg.cc_base=30; cfg.note_base=72; cfg.deadzone=500;  break;
    default:
        cfg.midi_ch=4; cfg.cc_base=30; cfg.note_base=72; cfg.deadzone=500;  break;
    }

    // XInput Y axes are inverted by default
    if (type == DEV_F310_XI) {
        cfg.axes[1].invert = true;  // left Y
        cfg.axes[3].invert = true;  // right Y
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
    if (!f) return cfg;

    // ArduinoJson v7 — use JsonDocument (StaticJsonDocument is deprecated)
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) return cfg;

    // Use doc["key"].is<T>() instead of containsKey (deprecated in v7)
    if (doc["name"].is<const char*>())  strlcpy(cfg.name, doc["name"], sizeof(cfg.name));
    if (doc["midi_ch"].is<int>())       cfg.midi_ch   = doc["midi_ch"];
    if (doc["cc_base"].is<int>())       cfg.cc_base   = doc["cc_base"];
    if (doc["note_base"].is<int>())     cfg.note_base = doc["note_base"];
    if (doc["note_vel"].is<int>())      cfg.note_vel  = doc["note_vel"];
    if (doc["deadzone"].is<int>())      cfg.deadzone  = doc["deadzone"];

    if (doc["axes"].is<JsonArray>()) {
        uint8_t i = 0;
        for (JsonObject ax : doc["axes"].as<JsonArray>()) {
            if (i >= MAX_AXES) break;
            if (ax["cc"].is<int>())       cfg.axes[i].cc     = ax["cc"];
            if (ax["invert"].is<bool>())  cfg.axes[i].invert = ax["invert"];
            if (ax["scale"].is<float>())  cfg.axes[i].scale  = ax["scale"];
            i++;
        }
    }

    cfg.loaded = true;
    return cfg;
}

bool config_save(uint16_t vid, uint16_t pid, const DeviceConfig &cfg) {
    if (!fs_ready) return false;

    char path[48];
    snprintf(path, sizeof(path), "%s/%04X_%04X.json", CONFIG_DIR, vid, pid);

    File f = LittleFS.open(path, "w");
    if (!f) return false;

    // ArduinoJson v7 API
    JsonDocument doc;
    doc["name"]      = cfg.name;
    doc["midi_ch"]   = cfg.midi_ch;
    doc["cc_base"]   = cfg.cc_base;
    doc["note_base"] = cfg.note_base;
    doc["note_vel"]  = cfg.note_vel;
    doc["deadzone"]  = cfg.deadzone;

    JsonArray axes = doc["axes"].to<JsonArray>();
    for (uint8_t i = 0; i < MAX_AXES; i++) {
        JsonObject ax = axes.add<JsonObject>();
        ax["cc"]     = cfg.axes[i].cc;
        ax["invert"] = cfg.axes[i].invert;
        ax["scale"]  = cfg.axes[i].scale;
    }

    serializeJsonPretty(doc, f);
    f.close();
    return true;
}