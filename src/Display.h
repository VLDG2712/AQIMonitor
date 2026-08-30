// Display.h — TFT rendering and value-to-colour mapping.
#pragma once

#include <Arduino.h>

uint16_t    aqiColor(uint8_t aqi);
// Also used by the /air handler, not just the TFT.
const char* aqiLabel(uint8_t aqi);
uint16_t    co2Color(uint16_t co2);
uint16_t    pmColor(uint16_t pm25);

void drawStaticLayout();
void updateDisplay();
