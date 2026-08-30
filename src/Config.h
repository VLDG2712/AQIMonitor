// Config.h — persisted runtime configuration (LittleFS /config.json).
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

//Runtime config 
struct Config {
  char     wifi_ssid[64];
  char     wifi_password[64];
  uint16_t server_port;
  float    temp_offset;
  float    hum_offset;
  uint32_t warmup_ms;
  uint32_t sensor_interval;
  uint8_t  neo_brightness;
  bool     neo_enabled;
  //Manual NeoPixel override
  bool     neo_manual;   // false=sensor controlled, true=manual
  uint8_t  neo_effect;   // FX_* constant above
  uint8_t  neo_r;
  uint8_t  neo_g;
  uint8_t  neo_b;
  uint8_t  neo_speed;    // 0-255 effect speed
};

extern Config cfg;
// Guards cfg against concurrent access from the web task (core 0)
// and the NeoPixel task (core 1).
extern SemaphoreHandle_t cfgMutex;

void applyDefaults();
void loadConfig();
void saveConfig();
