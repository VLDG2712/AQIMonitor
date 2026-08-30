// WebApi.h — HTTP API and the built-in dashboard.
#pragma once

#include <Arduino.h>

// Creates the WebServer on the configured port, registers every route
// and starts listening. Handlers themselves stay internal.
void webServerBegin();
