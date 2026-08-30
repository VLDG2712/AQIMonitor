// NeoPixel.cpp — sensor-driven and manual ring effects.
#include "NeoPixel.h"

#include "Config.h"
#include "Constants.h"
#include "Globals.h"

//NeoPixel — sensor-mode effects

void neoSpinner(uint32_t color, uint8_t tailLen) {
  uint32_t now = millis();
  if (now - neoState.lastSpin < 40) return;
  neoState.lastSpin = now;

  neo.clear();
  for (uint8_t i = 0; i < tailLen; i++) {
    uint8_t pos = (neoState.spinPos + i) % NEO_COUNT;
    uint8_t brightness = 255 / (tailLen - i);
    uint8_t r = ((color >> 16) & 0xFF) * brightness / 255;
    uint8_t g = ((color >> 8)  & 0xFF) * brightness / 255;
    uint8_t b = ((color)       & 0xFF) * brightness / 255;
    neo.setPixelColor(pos, neo.Color(r, g, b));
  }
  neo.show();
  neoState.spinPos = (neoState.spinPos + 1) % NEO_COUNT;
}

void neoBreath(uint8_t r, uint8_t g, uint8_t b, float speed = 0.02f) {
  uint32_t now = millis();
  if (now - neoState.lastUpdate < 20) return;
  neoState.lastUpdate = now;

  neoState.breathVal += neoState.breathDir * speed;
  if (neoState.breathVal >= 1.0f) { neoState.breathVal = 1.0f; neoState.breathDir = -1.0f; }
  if (neoState.breathVal <= 0.0f) { neoState.breathVal = 0.0f; neoState.breathDir =  1.0f; }

  uint8_t bright = (uint8_t)(neoState.breathVal * cfg.neo_brightness);
  neo.fill(neo.Color(r * bright / 255, g * bright / 255, b * bright / 255));
  neo.show();
}

void neoFlash(uint8_t r, uint8_t g, uint8_t b) {
  uint32_t now = millis();
  if (now - neoState.lastUpdate < 150) return;
  neoState.lastUpdate = now;

  static bool flashOn = false;
  flashOn = !flashOn;
  neo.fill(flashOn ? neo.Color(r, g, b) : neo.Color(0, 0, 0));
  neo.show();
}

//NeoPixel manual-mode effects

// Spin/Chase w speed control
void neoSpinManual(uint8_t r, uint8_t g, uint8_t b, uint8_t speed, uint8_t tailLen = 5) {
  uint32_t now = millis();
  uint32_t interval = map(255 - speed, 0, 255, 10, 200);
  if (now - neoState.lastSpin < interval) return;
  neoState.lastSpin = now;

  neo.clear();
  for (uint8_t i = 0; i < tailLen; i++) {
    uint8_t pos = (neoState.spinPos + i) % NEO_COUNT;
    uint8_t brt = 255 / (tailLen - i);
    neo.setPixelColor(pos, neo.Color(r * brt / 255, g * brt / 255, b * brt / 255));
  }
  neo.show();
  neoState.spinPos = (neoState.spinPos + 1) % NEO_COUNT;
}

// Rainbow
void neoRainbow(uint8_t speed) {
  uint32_t now = millis();
  uint32_t interval = map(255 - speed, 0, 255, 10, 200);
  if (now - neoState.lastRainbow < interval) return;
  neoState.lastRainbow = now;

  for (uint8_t i = 0; i < NEO_COUNT; i++) {
    uint16_t hue = neoState.rainbowHue + (i * 65536UL / NEO_COUNT);
    neo.setPixelColor(i, neo.gamma32(neo.ColorHSV(hue)));
  }
  neo.show();
  neoState.rainbowHue += 512;
}

// Strobe
void neoStrobe(uint8_t r, uint8_t g, uint8_t b, uint8_t speed) {
  uint32_t now = millis();
  // speed=255 → 20 flashes/sec, speed=0 → 0.5/sec
  uint32_t cycleMs = map(255 - speed, 0, 255, 25, 2000);
  if (now - neoState.lastStrobe < cycleMs) return;
  neoState.lastStrobe = now;

  neoState.strobeOn = !neoState.strobeOn;
  neo.fill(neoState.strobeOn ? neo.Color(r, g, b) : 0);
  neo.show();
}

// Fire
void neoFire() {
  uint32_t now = millis();
  if (now - neoState.lastFire < 60) return;
  neoState.lastFire = now;

  for (uint8_t i = 0; i < NEO_COUNT; i++) {
    int16_t delta = random(-30, 45);
    int16_t heat  = (int16_t)neoState.fireHeat[i] + delta;
    neoState.fireHeat[i] = (uint8_t)constrain(heat, 80, 255);

    uint8_t h = neoState.fireHeat[i];
    uint8_t rv, gv, bv;
    if (h < 128) { rv = h * 2;  gv = 0;           bv = 0;   }
    else         { rv = 255;    gv = (h - 128) * 2; bv = 0; }
    neo.setPixelColor(i, neo.Color(rv, gv, bv));
  }
  neo.show();
}

// Theater Chase
void neoTheaterChase(uint8_t r, uint8_t g, uint8_t b, uint8_t speed) {
  uint32_t now = millis();
  uint32_t interval = map(255 - speed, 0, 255, 20, 400);
  if (now - neoState.lastTheater < interval) return;
  neoState.lastTheater = now;

  neo.clear();
  for (uint8_t i = neoState.theaterStep; i < NEO_COUNT; i += 3)
    neo.setPixelColor(i, neo.Color(r, g, b));
  neo.show();
  neoState.theaterStep = (neoState.theaterStep + 1) % 3;
}

// Sparkle 
void neoSparkle(uint8_t r, uint8_t g, uint8_t b, uint8_t speed) {
  uint32_t now = millis();
  uint32_t interval = map(255 - speed, 0, 255, 20, 300);
  if (now - neoState.lastSparkle < interval) return;
  neoState.lastSparkle = now;

  neo.clear();
  uint8_t count = random(1, 5);
  for (uint8_t i = 0; i < count; i++)
    neo.setPixelColor(random(NEO_COUNT), neo.Color(r, g, b));
  neo.show();
}

// Color Cycle
void neoColorCycle(uint8_t speed) {
  uint32_t now = millis();
  uint32_t interval = map(255 - speed, 0, 255, 30, 600);
  if (now - neoState.lastRainbow < interval) return;
  neoState.lastRainbow = now;

  neo.fill(neo.gamma32(neo.ColorHSV(neoState.rainbowHue)));
  neo.show();
  neoState.rainbowHue += 256;
}


void updateNeo() {
  bool    en, man;
  uint8_t fx, r, g, b, spd;

  xSemaphoreTake(cfgMutex, portMAX_DELAY);
  en  = cfg.neo_enabled;
  man = cfg.neo_manual;
  fx  = cfg.neo_effect;
  r   = cfg.neo_r;
  g   = cfg.neo_g;
  b   = cfg.neo_b;
  spd = cfg.neo_speed;
  xSemaphoreGive(cfgMutex);

  if (!en) { neo.clear(); neo.show(); return; }

  //Manual override
  if (man) {
    float breathSpd = 0.005f + (spd / 255.0f) * 0.055f;
    switch (fx) {
      case FX_STATIC:
        if (millis() - neoState.lastStatic > 100) {
          neoState.lastStatic = millis();
          neo.fill(neo.Color(r, g, b));
          neo.show();
        }
        break;
      case FX_BREATH:     neoBreath(r, g, b, breathSpd);         break;
      case FX_SPIN:       neoSpinManual(r, g, b, spd);           break;
      case FX_RAINBOW:    neoRainbow(spd);                        break;
      case FX_STROBE:     neoStrobe(r, g, b, spd);               break;
      case FX_FIRE:       neoFire();                              break;
      case FX_THEATER:    neoTheaterChase(r, g, b, spd);         break;
      case FX_SPARKLE:    neoSparkle(r, g, b, spd);              break;
      case FX_COLORCYCLE: neoColorCycle(spd);                    break;
    }
    return;
  }

  //Sensor-controlled

  if (!wifiState.connected)              { neoSpinner(neo.Color(255, 0, 0));    return; }
  if (!sensors.ahtOk || !sensors.ensOk) { neoSpinner(neo.Color(255, 200, 0));  return; }
  if (!sensors.ready)                    { neoSpinner(neo.Color(255, 255, 255)); return; }
  if (sensors.pm2_5 > 55)               { neoFlash(255, 30, 0);                return; }
  switch (sensors.aqi) {
    case 1: neoBreath(0,   255, 0,   0.015f); break;
    case 2: neoBreath(0,   255, 200, 0.020f); break;
    case 3: neoBreath(255, 200, 0,   0.030f); break;
    case 4: neoBreath(255, 80,  0,   0.045f); break;
    case 5: neoFlash (255, 0,   0);           break;
    default:neoBreath(100, 100, 100, 0.010f); break;
  }
}

