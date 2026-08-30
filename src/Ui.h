// Ui.h — LVGL user interface for the 240x320 panel.
//
// Replaces the direct TFT_eSPI drawing that used to live in Display.cpp.
// Everything here must be called from a single task (the Arduino loop);
// LVGL is not thread-safe and LV_USE_OS is LV_OS_NONE.
#pragma once

#include <Arduino.h>

// Brings up LVGL, binds it to the existing TFT_eSPI instance and builds the
// screen. Call after tft.init(), and after loadConfig() so the LED bar can
// show real values immediately.
void uiInit();

// Drives LVGL's timers and redraws. Call every loop iteration; it is cheap
// when there is nothing to redraw.
void uiTick();

// Pushes the current sensor readings and config into the widgets. Call after
// each sensor read rather than every loop.
void uiUpdate();
