// Display.cpp — AQI text labels.
#include "Display.h"

const char* aqiLabel(uint8_t aqi) {
  switch (aqi) {
    case 1:  return "Excellent";
    case 2:  return "Good     ";
    case 3:  return "Moderate ";
    case 4:  return "Poor     ";
    case 5:  return "Unhealthy";
    default: return "Warmup   ";
  }
}
