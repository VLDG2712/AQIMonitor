// Display.cpp — static layout and per-reading updates.
#include "Display.h"

#include "Config.h"
#include "Constants.h"
#include "Globals.h"


uint16_t aqiColor(uint8_t aqi) {
  switch (aqi) {
    case 1:  return C_AQI_1;
    case 2:  return C_AQI_2;
    case 3:  return C_AQI_3;
    case 4:  return C_AQI_4;
    case 5:  return C_AQI_5;
    default: return C_LABEL;
  }
}

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

uint16_t co2Color(uint16_t co2) {
  if (co2 < 600)  return C_CO2_GOOD;
  if (co2 < 1000) return C_CO2_MED;
  if (co2 < 1500) return C_CO2_BAD;
  return C_CO2_UGLY;
}

uint16_t pmColor(uint16_t pm25) {
  if (pm25 < 12)  return C_PM_GOOD;
  if (pm25 < 35)  return C_PM_MED;
  if (pm25 < 55)  return C_PM_BAD;
  return C_PM_UGLY;
}


//Display

void drawStaticLayout() {
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, 240, 18, C_SURFACE);
  tft.setTextColor(C_HEADER, C_SURFACE);
  tft.setTextSize(1);
  tft.setCursor(5, 5);
  tft.print("HOMELAB AIR MONITOR");
  tft.drawFastHLine(0, 18, 240, C_HEADER);
  tft.setTextColor(C_LABEL, C_BG);
  tft.setCursor(5,   25); tft.print("TEMP");
  tft.setCursor(130, 25); tft.print("HUMIDITY");
  tft.drawFastHLine(0, 75, 240, C_DIVIDER);
  tft.setCursor(5,   82); tft.print("eCO2 (ppm)");
  tft.setCursor(130, 82); tft.print("TVOC (ppb)");
  tft.drawFastHLine(0, 135, 240, C_DIVIDER);
  tft.setCursor(5,   142); tft.print("AQI");
  tft.setCursor(80,  142); tft.print("PRESSURE");
  tft.drawFastHLine(0, 195, 240, C_DIVIDER);
  tft.setCursor(5,   200); tft.print("PM1.0");
  tft.setCursor(85,  200); tft.print("PM2.5");
  tft.setCursor(165, 200); tft.print("PM10");
  tft.drawFastHLine(0, 225, 240, C_DIVIDER);
}

void updateDisplay() {
  tft.fillRect(5, 35, 120, 35, C_BG);
  tft.setTextColor(C_TEMP, C_BG);
  tft.setTextSize(3);
  tft.setCursor(5, 35);
  tft.print(sensors.temperature, 1);
  tft.print("C");

  tft.fillRect(130, 35, 110, 35, C_BG);
  tft.setTextColor(C_HUMID, C_BG);
  tft.setTextSize(3);
  tft.setCursor(130, 35);
  tft.print(sensors.humidity, 1);
  tft.print("%");

  tft.fillRect(5, 92, 120, 35, C_BG);
  tft.setTextColor(co2Color(sensors.eco2), C_BG);
  tft.setTextSize(3);
  tft.setCursor(5, 92);
  tft.print(sensors.eco2);

  tft.fillRect(130, 92, 110, 35, C_BG);
  tft.setTextColor(C_TVOC, C_BG);
  tft.setTextSize(3);
  tft.setCursor(130, 92);
  tft.print(sensors.tvoc);

  tft.fillRect(5, 152, 70, 40, C_BG);
  if (sensors.ready) {
    tft.setTextColor(aqiColor(sensors.aqi), C_BG);
    tft.setTextSize(3);
    tft.setCursor(5, 152);
    tft.print(sensors.aqi);
    tft.setTextSize(1);
    tft.setCursor(5, 178);
    tft.print(aqiLabel(sensors.aqi));
  } else {
    tft.setTextColor(C_WARN, C_BG);
    tft.setTextSize(1);
    tft.setCursor(5, 152);
    uint32_t elapsed  = millis();
    uint32_t secsLeft = elapsed < cfg.warmup_ms
                        ? (cfg.warmup_ms - elapsed) / 1000 : 0;
    tft.printf("Warmup\n%lus", secsLeft);
  }

  tft.fillRect(80, 152, 160, 40, C_BG);
  tft.setTextColor(C_PRESSURE, C_BG);
  tft.setTextSize(2);
  tft.setCursor(80, 152);
  tft.print(sensors.pressure_hPa, 1);
  tft.print("hPa");
  tft.setTextColor(C_DIM, C_BG);
  tft.setTextSize(1);
  tft.setCursor(80, 178);
  tft.printf("%.0fm alt", sensors.altitude_m);

  tft.fillRect(5, 210, 235, 14, C_BG);
  tft.setTextSize(2);
  tft.setTextColor(pmColor(sensors.pm1_0), C_BG);
  tft.setCursor(5, 210);
  tft.print(sensors.pm1_0);
  tft.setTextColor(pmColor(sensors.pm2_5), C_BG);
  tft.setCursor(85, 210);
  tft.print(sensors.pm2_5);
  tft.setTextColor(pmColor(sensors.pm10), C_BG);
  tft.setCursor(165, 210);
  tft.print(sensors.pm10);

  tft.fillRect(5, 228, 235, 12, C_BG);
  tft.setTextColor(C_DIM, C_BG);
  tft.setTextSize(1);
  tft.setCursor(5, 229);
  tft.printf("CPU:%.0fC  ", sensors.chipTemp);
  if (wifiState.connected) {
    tft.print(WiFi.localIP());
    if (rtcValid) {
      tft.setTextColor(C_AQI_1, C_BG);
      tft.print(" [F]");
    }
  } else {
    tft.print("WiFi reconnecting...");
  }

  {
    int bx = 5, by = 239, bw = 120, bh = 6;
    tft.fillRect(bx, by, bw, bh, C_BG);
    tft.drawRect(bx, by, bw, bh, C_DIM);
    int fillW = (cfg.neo_brightness * (bw - 2)) / 255;
    uint16_t fillCol = cfg.neo_enabled ? (cfg.neo_manual ? C_AQI_2 : C_AQI_1) : C_CO2_UGLY;
    if (fillW > 0) tft.fillRect(bx + 1, by + 1, fillW, bh - 2, fillCol);
    tft.setTextColor(C_DIM, C_BG);
    tft.setCursor(bx + bw + 8, by);
    if (!cfg.neo_enabled)    tft.print("LED:off");
    else if (cfg.neo_manual) tft.printf("LED:M%d", cfg.neo_brightness);
    else                     tft.printf("LED:%d",  cfg.neo_brightness);
  }
}

