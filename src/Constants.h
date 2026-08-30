// Constants.h — pin map, palette, timings and effect IDs.
#pragma once

#include <Arduino.h>
#include "secrets.h"


constexpr const char*    DEFAULT_SSID             = SECRET_WIFI_SSID;
constexpr const char*    DEFAULT_PASSWORD         = SECRET_WIFI_PASSWORD;
const uint16_t DEFAULT_SERVER_PORT      = 9091;
const float    DEFAULT_TEMP_OFFSET      = -4.9f;
const float    DEFAULT_HUM_OFFSET       = +12.0f;
const uint32_t DEFAULT_WARMUP_MS        = 120000;
const uint32_t DEFAULT_SENSOR_INTERVAL  = 2000;
const uint8_t  DEFAULT_NEO_BRIGHTNESS   = 175;

//Pin definitions
const uint8_t PIN_NEO        = 32;
const uint8_t PIN_PMS_RX     = 16;
const uint8_t PIN_PMS_TX     = 17;
const uint8_t PIN_LED        = 2;
const uint8_t PIN_TFT_BL     = 13;
const uint8_t NEO_COUNT      = 16;

//I2C addresses
const uint8_t ADDR_ENS160    = 0x53;
const uint8_t ADDR_BMP580    = 0x47;

//Cyberpunk palette
const uint16_t C_BG          = 0x0000;
const uint16_t C_SURFACE     = 0x0841;
const uint16_t C_HEADER      = 0x07FF;
const uint16_t C_DIVIDER     = 0x18C3;
const uint16_t C_LABEL       = 0x7BEF;
const uint16_t C_DIM         = 0x39E7;
const uint16_t C_TEMP        = 0xFD20;
const uint16_t C_HUMID       = 0x07FF;
const uint16_t C_TVOC        = 0xF81F;
const uint16_t C_PRESSURE    = 0xF81F;
const uint16_t C_CO2_GOOD    = 0x07E0;
const uint16_t C_CO2_MED     = 0xFFE0;
const uint16_t C_CO2_BAD     = 0xFD20;
const uint16_t C_CO2_UGLY    = 0xF800;
const uint16_t C_AQI_1       = 0x07E0;
const uint16_t C_AQI_2       = 0x07FF;
const uint16_t C_AQI_3       = 0xFFE0;
const uint16_t C_AQI_4       = 0xFD20;
const uint16_t C_AQI_5       = 0xF800;
const uint16_t C_WARN        = 0xFFE0;
const uint16_t C_PM_GOOD     = 0x07E0;
const uint16_t C_PM_MED      = 0xFFE0;
const uint16_t C_PM_BAD      = 0xFD20;
const uint16_t C_PM_UGLY     = 0xF800;

//LED timing
const uint32_t LED_HEARTBEAT_MS = 2000;
const uint16_t LED_FLASH_ON_MS  = 150;
const uint16_t LED_FLASH_OFF_MS = 250;
const uint16_t LED_ERROR_GAP_MS = 2000;
const uint32_t WDT_TIMEOUT_S    = 10;

//Effect IDs 
const uint8_t FX_STATIC    = 0;
const uint8_t FX_BREATH    = 1;
const uint8_t FX_SPIN      = 2;
const uint8_t FX_RAINBOW   = 3;
const uint8_t FX_STROBE    = 4;
const uint8_t FX_FIRE      = 5;
const uint8_t FX_THEATER   = 6;
const uint8_t FX_SPARKLE   = 7;
const uint8_t FX_COLORCYCLE= 8;

//RTC memory 

