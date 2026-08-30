// StatusLed.cpp — onboard LED status signalling.
#include "StatusLed.h"

#include "Config.h"
#include "Constants.h"
#include "Globals.h"

//LED

uint8_t collectErrors(uint8_t* errors) {
  uint8_t count = 0;
  if (!wifiState.connected) errors[count++] = 2;
  if (!sensors.ahtOk)       errors[count++] = 3;
  if (!sensors.ensOk)       errors[count++] = 4;
  if (!sensors.bmpOk)       errors[count++] = 5;
  if (!sensors.pmsOk)       errors[count++] = 6;
  return count;
}

void updateLed() {
  uint32_t now = millis();
  uint8_t errors[5];
  uint8_t errorCount = collectErrors(errors);

  if (errorCount == 0) {
    if (!cfg.neo_enabled) {
      digitalWrite(PIN_LED, LOW);
      return;
    }
    uint32_t cycle = now % LED_HEARTBEAT_MS;
    bool on = (cycle < 80) || (cycle > 150 && cycle < 230);
    digitalWrite(PIN_LED, on ? HIGH : LOW);
    return;
  }

  if (led.currentErrorIdx >= errorCount) led.currentErrorIdx = 0;
  uint8_t err         = errors[led.currentErrorIdx];
  uint32_t cycleLen   = (err * (LED_FLASH_ON_MS + LED_FLASH_OFF_MS)) + LED_ERROR_GAP_MS;
  uint32_t cyclePos   = now - led.lastCycleStart;

  if (cyclePos >= cycleLen) {
    led.lastCycleStart  = now;
    led.currentErrorIdx = (led.currentErrorIdx + 1) % errorCount;
    digitalWrite(PIN_LED, LOW);
    return;
  }

  uint32_t flashSection = err * (LED_FLASH_ON_MS + LED_FLASH_OFF_MS);
  if (cyclePos < flashSection) {
    uint32_t flashPos = cyclePos % (LED_FLASH_ON_MS + LED_FLASH_OFF_MS);
    digitalWrite(PIN_LED, flashPos < LED_FLASH_ON_MS ? HIGH : LOW);
  } else {
    digitalWrite(PIN_LED, LOW);
  }
}
