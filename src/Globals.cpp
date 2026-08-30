// Globals.cpp — single definition point for shared state.
#include "Globals.h"
#include "Config.h"

TFT_eSPI         tft     = TFT_eSPI();
Adafruit_AHTX0   aht;
ScioSense_ENS160 ens160(ADDR_ENS160);
Adafruit_BMP5xx  bmp;
Adafruit_NeoPixel neo(NEO_COUNT, PIN_NEO, NEO_GRB + NEO_KHZ800);
HardwareSerial   pmsSerial(2);
PMS              pms(pmsSerial);
PMS::DATA        pmsData;
WebServer*       server = nullptr;
SemaphoreHandle_t cfgMutex      = nullptr;
TaskHandle_t      neoTaskHandle = nullptr;
TaskHandle_t      webTaskHandle = nullptr;

Config cfg;

SensorData sensors;
WiFiState wifiState;
LedState led;
NeoState neoState;

RTC_DATA_ATTR uint8_t  rtcBssid[6] = {0};
RTC_DATA_ATTR uint8_t  rtcChannel  = 0;
RTC_DATA_ATTR uint32_t rtcIp       = 0;
RTC_DATA_ATTR uint32_t rtcGateway  = 0;
RTC_DATA_ATTR uint32_t rtcSubnet   = 0;
RTC_DATA_ATTR bool     rtcValid    = false;

