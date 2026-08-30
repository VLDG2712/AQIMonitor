// Globals.h — shared hardware objects and runtime state.
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <TFT_eSPI.h>
#include <Adafruit_AHTX0.h>
#include <ScioSense_ENS160.h>
#include <Adafruit_BMP5xx.h>
#include <Adafruit_NeoPixel.h>
#include <PMS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "Constants.h"

struct SensorData {
  float    temperature  = 0.0f;
  float    humidity     = 0.0f;
  uint16_t eco2         = 0;
  uint16_t tvoc         = 0;
  uint8_t  aqi          = 0;
  float    pressure_hPa = 0.0f;
  float    altitude_m   = 0.0f;
  float    bmp_temp     = 0.0f;
  uint16_t pm1_0        = 0;
  uint16_t pm2_5        = 0;
  uint16_t pm10         = 0;
  float    chipTemp     = 0.0f;
  bool     ready        = false;
  bool     ahtOk        = false;
  bool     ensOk        = false;
  bool     bmpOk        = false;
  bool     pmsOk        = false;
};
struct WiFiState {
  bool     connected     = false;
  uint32_t lastAttemptMs = 0;
};
struct LedState {
  uint8_t  currentErrorIdx = 0;
  uint32_t lastCycleStart  = 0;
};
struct NeoState {
  uint32_t lastUpdate  = 0;
  float    breathVal   = 0.0f;
  float    breathDir   = 1.0f;
  uint8_t  spinPos     = 0;
  uint32_t lastSpin    = 0;
  //Manual effect state
  uint16_t rainbowHue  = 0;
  uint8_t  theaterStep = 0;
  uint32_t lastTheater = 0;
  uint32_t lastRainbow = 0;
  uint32_t lastStrobe  = 0;
  bool     strobeOn    = false;
  uint32_t lastFire    = 0;
  uint32_t lastSparkle = 0;
  uint32_t lastStatic  = 0;
  uint8_t  fireHeat[16] = {0};
};

extern TFT_eSPI         tft;
extern Adafruit_AHTX0   aht;
extern ScioSense_ENS160 ens160;
extern Adafruit_BMP5xx  bmp;
extern Adafruit_NeoPixel neo;
extern HardwareSerial   pmsSerial;
extern PMS              pms;
extern PMS::DATA        pmsData;
extern WebServer*       server;
extern SemaphoreHandle_t cfgMutex;
extern TaskHandle_t      neoTaskHandle;
extern TaskHandle_t      webTaskHandle;

extern SensorData sensors;
extern WiFiState wifiState;
extern LedState led;
extern NeoState neoState;

// Set once at boot from RTC memory; true when the fast WiFi path
// (cached BSSID/channel/IP) was usable.
extern RTC_DATA_ATTR uint8_t  rtcBssid[6];
extern RTC_DATA_ATTR uint8_t  rtcChannel;
extern RTC_DATA_ATTR uint32_t rtcIp;
extern RTC_DATA_ATTR uint32_t rtcGateway;
extern RTC_DATA_ATTR uint32_t rtcSubnet;
extern RTC_DATA_ATTR bool     rtcValid;

