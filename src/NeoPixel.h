// NeoPixel.h — LED ring animations.
#pragma once

#include <Arduino.h>

// Renders one animation frame from the current config. Called in a
// tight loop by the NeoPixel task; the individual effects are internal.
void updateNeo();

// Rotating comet used as a status animation during boot and WiFi connect.
void neoSpinner(uint32_t color, uint8_t tailLen = 4);
