// StatusLed.h — onboard LED heartbeat and error blink codes.
#pragma once

#include <Arduino.h>

// Blinks a heartbeat when healthy, or an error code per failed sensor.
void updateLed();
