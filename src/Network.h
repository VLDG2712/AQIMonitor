// Network.h — WiFi connection with an RTC-cached fast path.
#pragma once

#include <Arduino.h>

void wifiConnect();
// Reconnects if the link dropped; safe to call every loop.
void wifiCheck();
