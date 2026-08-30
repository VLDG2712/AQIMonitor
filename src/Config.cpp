// Config.cpp — defaults and LittleFS persistence for the runtime config.
#include "Config.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <FS.h>

#include "Constants.h"


void applyDefaults() {
  strlcpy(cfg.wifi_ssid,     DEFAULT_SSID,     sizeof(cfg.wifi_ssid));
  strlcpy(cfg.wifi_password, DEFAULT_PASSWORD,  sizeof(cfg.wifi_password));
  cfg.server_port     = DEFAULT_SERVER_PORT;
  cfg.temp_offset     = DEFAULT_TEMP_OFFSET;
  cfg.hum_offset      = DEFAULT_HUM_OFFSET;
  cfg.warmup_ms       = DEFAULT_WARMUP_MS;
  cfg.sensor_interval = DEFAULT_SENSOR_INTERVAL;
  cfg.neo_brightness  = DEFAULT_NEO_BRIGHTNESS;
  cfg.neo_enabled     = true;
  cfg.neo_manual      = false;
  cfg.neo_effect      = FX_BREATH;
  cfg.neo_r           = 0;
  cfg.neo_g           = 200;
  cfg.neo_b           = 100;
  cfg.neo_speed       = 128;
}

void loadConfig() {
  applyDefaults();
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS failed — using defaults");
    return;
  }
  if (!LittleFS.exists("/config.json")) {
    Serial.println("No config.json — using defaults");
    return;
  }
  File f = LittleFS.open("/config.json", "r");
  if (!f) return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.printf("config.json error: %s\n", err.c_str());
    return;
  }

  if (doc["wifi_ssid"].is<const char*>())
    strlcpy(cfg.wifi_ssid, doc["wifi_ssid"], sizeof(cfg.wifi_ssid));
  if (doc["wifi_password"].is<const char*>())
    strlcpy(cfg.wifi_password, doc["wifi_password"], sizeof(cfg.wifi_password));

  cfg.server_port     = doc["server_port"]     | DEFAULT_SERVER_PORT;
  cfg.temp_offset     = doc["temp_offset"]     | DEFAULT_TEMP_OFFSET;
  cfg.hum_offset      = doc["hum_offset"]      | DEFAULT_HUM_OFFSET;
  cfg.warmup_ms       = doc["warmup_ms"]       | DEFAULT_WARMUP_MS;
  cfg.sensor_interval = doc["sensor_interval"] | DEFAULT_SENSOR_INTERVAL;
  cfg.neo_brightness  = doc["neo_brightness"]  | DEFAULT_NEO_BRIGHTNESS;
  cfg.neo_enabled     = doc["neo_enabled"]     | true;
  cfg.neo_manual      = doc["neo_manual"]      | false;
  cfg.neo_effect      = doc["neo_effect"]      | (uint8_t)FX_BREATH;
  cfg.neo_r           = doc["neo_r"]           | (uint8_t)0;
  cfg.neo_g           = doc["neo_g"]           | (uint8_t)200;
  cfg.neo_b           = doc["neo_b"]           | (uint8_t)100;
  cfg.neo_speed       = doc["neo_speed"]       | (uint8_t)128;

  Serial.printf("Config loaded — SSID:%s Port:%d Brightness:%d Manual:%d Effect:%d\n",
                cfg.wifi_ssid, cfg.server_port, cfg.neo_brightness,
                cfg.neo_manual, cfg.neo_effect);
}

void saveConfig() {
  File f = LittleFS.open("/config.json", "w");
  if (!f) { Serial.println("Failed to write config.json"); return; }

  JsonDocument doc;
  doc["wifi_ssid"]       = cfg.wifi_ssid;
  doc["wifi_password"]   = cfg.wifi_password;
  doc["server_port"]     = cfg.server_port;
  doc["temp_offset"]     = cfg.temp_offset;
  doc["hum_offset"]      = cfg.hum_offset;
  doc["warmup_ms"]       = cfg.warmup_ms;
  doc["sensor_interval"] = cfg.sensor_interval;
  doc["neo_brightness"]  = cfg.neo_brightness;
  doc["neo_enabled"]     = cfg.neo_enabled;
  doc["neo_manual"]      = cfg.neo_manual;
  doc["neo_effect"]      = cfg.neo_effect;
  doc["neo_r"]           = cfg.neo_r;
  doc["neo_g"]           = cfg.neo_g;
  doc["neo_b"]           = cfg.neo_b;
  doc["neo_speed"]       = cfg.neo_speed;

  serializeJson(doc, f);
  f.close();
  Serial.println("Config saved");
}

//Helpers
