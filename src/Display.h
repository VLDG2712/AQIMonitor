// Display.h — AQI text labels.
//
// The TFT drawing that used to live here was replaced by the LVGL interface in
// Ui.cpp. Only the label mapping survives, because it is shared: both the UI
// and the /air JSON handler report the same wording.
#pragma once

#include <Arduino.h>

/// Human-readable band for an ENS160 AQI value (1-5); "Warmup" while unready.
///
/// Padded to a fixed width — the /air response and the old fixed-cell TFT
/// layout both relied on that, and the JSON field is trimmed by clients.
const char* aqiLabel(uint8_t aqi);
