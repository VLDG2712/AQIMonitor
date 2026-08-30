// Sensors.cpp — AHT21, ENS160, BMP580 and PMS5003 sampling.
#include "Sensors.h"

#include "Config.h"
#include "Constants.h"
#include "Globals.h"


//Sensors
void readSensors() {
  if (sensors.ahtOk) {
    sensors_event_t hum, temp;
    aht.getEvent(&hum, &temp);
    sensors.temperature = temp.temperature + cfg.temp_offset;
    sensors.humidity    = hum.relative_humidity + cfg.hum_offset;
    sensors.humidity    = constrain(sensors.humidity, 0.0f, 100.0f);
  }

  if (sensors.ensOk) {
    ens160.set_envdata(sensors.temperature, sensors.humidity);
    if (ens160.available()) {
      ens160.measure(true);
      sensors.eco2 = ens160.geteCO2();
      sensors.tvoc = ens160.getTVOC();
      sensors.aqi  = ens160.getAQI();
    }
  }

  if (sensors.bmpOk) {
    // Adafruit_BMP5xx::readPressure() already returns hPa (see its doc comment
    // in Adafruit_BMP5xx.cpp) — no Pa->hPa conversion needed here.
    sensors.pressure_hPa = bmp.readPressure();
    sensors.bmp_temp     = bmp.readTemperature();
    sensors.altitude_m   = bmp.readAltitude(1013.25f);
  }

  if (sensors.pmsOk && pms.readUntil(pmsData, 200)) {
    sensors.pm1_0 = pmsData.PM_AE_UG_1_0;
    sensors.pm2_5 = pmsData.PM_AE_UG_2_5;
    sensors.pm10  = pmsData.PM_AE_UG_10_0;
  }

  sensors.chipTemp = (float)temperatureRead();
  sensors.ready    = (millis() >= cfg.warmup_ms);
}

