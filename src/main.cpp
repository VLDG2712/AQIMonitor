// main.cpp — boot, task creation and the main sampling loop.
//
// Split out of the original single-file sketch: this file now only wires
// the modules together. See Constants.h for the pin map and palette.
#include <Arduino.h>
#include <esp_task_wdt.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "Config.h"
#include "Constants.h"
#include "Display.h"
#include "Ui.h"
#include "Globals.h"
#include "NeoPixel.h"
#include "Network.h"
#include "Sensors.h"
#include "StatusLed.h"
#include "WebApi.h"

void neoTaskFn(void* param) {
  for (;;) {
    updateNeo();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void webTaskFn(void* param) {
  for (;;) {
    if (server) server->handleClient();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}


//Setup

void setup() {
  Serial.begin(115200);
  Serial.println("\nHomelab Air Monitor v2 booting...");
  Serial.printf("RTC: %s\n", rtcValid ? "fast connect" : "full scan");

  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  loadConfig();

  neo.begin();
  neo.setBrightness(cfg.neo_brightness);
  neo.clear();
  neo.show();

  // Startup self-test: drives the ring directly, before any task exists.
  // If this shows colour but the ring is dark in normal operation, the wiring
  // is fine and the fault is in the animation task. If it stays dark, the
  // problem is electrical (data pin, power, or a dead first pixel).
  Serial.println("NEO: self-test start");
  neo.setBrightness(60);
  const uint32_t selfTest[3] = {
    neo.Color(255, 0, 0), neo.Color(0, 255, 0), neo.Color(0, 0, 255)
  };
  for (uint8_t c = 0; c < 3; c++) {
    neo.fill(selfTest[c]);
    neo.show();
    delay(350);
  }
  neo.clear();
  neo.show();
  neo.setBrightness(cfg.neo_brightness);
  Serial.println("NEO: self-test done");

  // TFT
  tft.init();
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, HIGH);
  tft.invertDisplay(true);
  tft.setRotation(0);
  tft.fillScreen(C_BG);
  tft.setTextColor(C_HEADER, C_BG);
  tft.setTextSize(2);
  tft.setCursor(10, 40); tft.println("Homelab Air");
  tft.setCursor(10, 65); tft.println("Monitor v2");
  tft.setTextSize(1);

  // WiFi
  tft.setTextColor(C_LABEL, C_BG);
  tft.setCursor(10, 100);
  tft.println(rtcValid ? "WiFi fast connect..." : "Connecting WiFi...");
  wifiConnect();
  uint32_t wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
    delay(100);
    esp_task_wdt_reset();
    updateLed();
    neoSpinner(neo.Color(0, 255, 255));
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiState.connected = true;
    if (!rtcValid) {
      memcpy(rtcBssid, WiFi.BSSID(), 6);
      rtcChannel = WiFi.channel();
      rtcIp      = (uint32_t)WiFi.localIP();
      rtcGateway = (uint32_t)WiFi.gatewayIP();
      rtcSubnet  = (uint32_t)WiFi.subnetMask();
      rtcValid   = true;
    }
    tft.setTextColor(C_AQI_1, C_BG);
    tft.setCursor(10, 115);
    tft.println(rtcValid ? "WiFi OK! [fast]" : "WiFi OK!");
    tft.setTextColor(C_DIM, C_BG);
    tft.setCursor(10, 128);
    tft.print(WiFi.localIP());
    Serial.println("WiFi: " + WiFi.localIP().toString());
  } else {
    tft.setTextColor(C_CO2_UGLY, C_BG);
    tft.setCursor(10, 115);
    tft.println("WiFi failed - will retry");
    rtcValid = false; rtcIp = 0;
  }

  // AHT21
  tft.setCursor(10, 148);
  if (aht.begin()) {
    sensors.ahtOk = true;
    tft.setTextColor(C_AQI_1, C_BG);
    tft.println("AHT21 OK");
  } else {
    tft.setTextColor(C_CO2_UGLY, C_BG);
    tft.println("AHT21 ERROR");
  }

  // ENS160
  tft.setCursor(10, 163);
  ens160.begin();
  if (ens160.available()) {
    ens160.setMode(ENS160_OPMODE_STD);
    sensors.ensOk = true;
    tft.setTextColor(C_AQI_1, C_BG);
    tft.println("ENS160 OK");
  } else {
    tft.setTextColor(C_CO2_UGLY, C_BG);
    tft.println("ENS160 ERROR");
  }

  // BMP580
  tft.setCursor(10, 178);
  if (bmp.begin(ADDR_BMP580, &Wire)) {
    sensors.bmpOk = true;
    tft.setTextColor(C_AQI_1, C_BG);
    tft.println("BMP580 OK");
  } else {
    tft.setTextColor(C_CO2_UGLY, C_BG);
    tft.println("BMP580 ERROR");
  }

  // PMS5003
  tft.setCursor(10, 193);
  pmsSerial.begin(9600, SERIAL_8N1, PIN_PMS_RX, PIN_PMS_TX);
  pms.passiveMode();
  delay(100);
  pms.wakeUp();
  sensors.pmsOk = true;
  tft.setTextColor(C_AQI_1, C_BG);
  tft.println("PMS5003 OK");

  // Web server
  webServerBegin();

  tft.setTextColor(C_AQI_1, C_BG);
  tft.setCursor(10, 208);
  tft.println("Ready!");

  delay(1500);

  cfgMutex = xSemaphoreCreateMutex();
  if (cfgMutex == nullptr) Serial.println("FATAL: cfgMutex alloc failed");

  Serial.printf("Free heap before tasks: %u bytes\n", ESP.getFreeHeap());

  // 2048 was tight for a task doing float maths and calling into
  // Adafruit_NeoPixel, which allocates for the ESP32 RMT backend.
  BaseType_t neoOk = xTaskCreatePinnedToCore(
    neoTaskFn,
    "NeoPixel",
    4096,
    NULL,
    2,
    &neoTaskHandle,
    1
  );
  // Previously unchecked: a failure here leaves the ring permanently dark
  // while every other subsystem works normally, which is very hard to
  // diagnose from the outside.
  Serial.printf("NEO task create: %s\n", neoOk == pdPASS ? "OK" : "FAILED");

  BaseType_t webOk = xTaskCreatePinnedToCore(
    webTaskFn,
    "WebServer",
    4096,
    NULL,
    1,
    &webTaskHandle,
    0
  );
  Serial.printf("WEB task create: %s\n", webOk == pdPASS ? "OK" : "FAILED");

  uiInit();
}

// Loop 

void loop() {
  esp_task_wdt_reset();
  updateLed();
  wifiCheck();
  uiTick();

  static uint32_t lastRead = 0;
  uint32_t now = millis();

  if (now - lastRead >= cfg.sensor_interval) {
    lastRead = now;
    readSensors();
    uiUpdate();
    Serial.printf(
      "[%lus] T:%.1fC H:%.1f%% CO2:%d TVOC:%d AQI:%d "
      "P:%.1fhPa ALT:%.0fm PM2.5:%d CPU:%.0fC WiFi:%s NEO:%s\n",
      now / 1000,
      sensors.temperature, sensors.humidity,
      sensors.eco2, sensors.tvoc, sensors.aqi,
      sensors.pressure_hPa, sensors.altitude_m,
      sensors.pm2_5, sensors.chipTemp,
      rtcValid ? "FAST" : "FULL",
      cfg.neo_manual ? "MANUAL" : "SENSOR"
    );
  }
}
