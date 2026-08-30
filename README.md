# Hexair

ESP32 air quality monitor: local LVGL display, addressable status ring, a web
dashboard and REST API, plus an optional history service that records readings
to MariaDB and publishes the device to Home Assistant.

## What it measures

Temperature, humidity, eCO2, TVOC, an air-quality index (1–5), atmospheric
pressure and altitude, and PM1.0 / PM2.5 / PM10 particulate counts.

## Hardware

| Part | Role |
|---|---|
| ESP32-WROOM-32D | Microcontroller |
| AHT21 | Temperature / humidity |
| ENS160 | eCO2, TVOC, AQI |
| BMP580 | Pressure / altitude |
| PMS5003 | Particulate matter |
| ST7789 240x320 TFT | Local display |
| WS2812B ring, 16 LED | Status lighting |

Pin assignments live in [`src/Constants.h`](src/Constants.h). The display is
configured through build flags in `platformio.ini` rather than a TFT_eSPI
`User_Setup.h`.

## Firmware

PlatformIO, Arduino framework. Dependencies resolve automatically from
`platformio.ini`.

```bash
pio run                                        # build
pio run -t upload --upload-port /dev/ttyUSB0   # flash
pio device monitor                             # 115200 baud
```

### Credentials

Copy the template and fill it in — `src/secrets.h` is gitignored:

```c
#define SECRET_WIFI_SSID     "your-ssid"
#define SECRET_WIFI_PASSWORD "your-password"
#define SECRET_API_TOKEN     "generate-a-long-random-token"
```

Generate a token with `python3 -c "import secrets; print(secrets.token_hex(24))"`.
WiFi credentials here are only the *defaults*; once `/config.json` exists in
LittleFS, the stored values win.

### Source layout

Split into modules rather than one sketch:

| File | Responsibility |
|---|---|
| `main.cpp` | Boot, task creation, sampling loop |
| `Constants.h` | Pin map, palette, timings, effect IDs |
| `Config.*` | Runtime config struct, LittleFS persistence |
| `Globals.*` | Shared hardware objects and state |
| `Sensors.*` | Reading all four sensors |
| `Ui.*` | LVGL interface |
| `Display.*` | AQI text labels, shared by the UI and the API |
| `NeoPixel.*` | Ring animations |
| `StatusLed.*` | Onboard LED heartbeat and error codes |
| `Network.*` | WiFi with an RTC-cached fast path |
| `WebApi.*` | HTTP handlers and the dashboard page |

Two FreeRTOS tasks run alongside `loop()`: the ring animation is pinned to
core 1, the web server to core 0. `loop()` itself samples the sensors, drives
LVGL and blinks the status LED.

## Display

An LVGL 9 interface with a persistent header and footer around three pages that
cycle every 8 seconds — air quality (arc gauge, eCO2, TVOC), climate
(temperature, humidity, pressure) and particulate (PM values plus a live PM2.5
trend chart). There is no touch controller and no free GPIO for a button, so the
pages advance on a timer and anything that must stay visible — WiFi state, IP,
CPU temperature, ring status — lives in the header and footer.

`lv_conf.h` is in [`include/`](include/lv_conf.h), and `platformio.ini` adds
that directory with `-I`. This matters: without it the LVGL *library* sources
cannot resolve `#include "lv_conf.h"` and silently fall back to
`lv_conf_internal.h` defaults. It still builds and runs, but you get a 64KB
static memory pool you did not ask for and every font except montserrat_14
goes missing.

## Status ring

Sensor mode maps AQI to colour and animation, and signals faults — red spinner
for no WiFi, amber for a failed sensor, white while warming up. Manual mode
exposes nine effects (static, breathing, chase, rainbow, strobe, fire, theater
chase, sparkle, colour cycle) with colour, brightness and speed.

On boot the ring flashes red, green and blue for about a second. That runs
directly in `setup()` before any task exists, so a dark ring during it points at
wiring or power rather than software.

## Web API

Served on port 9091. A dashboard lives at `http://<device-ip>/`.

| Endpoint | Auth | Purpose |
|---|---|---|
| `GET /air` | none | Current readings as JSON |
| `GET /neo` | none | Ring state |
| `POST /neo` | Bearer | Set ring state |
| `GET /config` | Bearer | Current configuration |
| `POST /config` | Bearer | Update configuration |
| `POST /config/reset` | Bearer | Restore defaults |
| `POST /rtc/clear` | Bearer | Clear the cached WiFi fast-path |

Authentication is a bearer token (`Authorization: Bearer <token>`), not Basic
auth. Note that `/air` and `GET /neo` are deliberately unauthenticated —
`/air` is rate-limited to one request per second; anything that changes state
requires the token.

There is **no WebSocket server**, despite the `WebSockets` entry in
`lib_deps`. The companion app attempts `ws://<ip>:9092` first and falls back to
polling `/air` after a timeout.

## History service

[`server/`](server/README.md) holds an optional service that runs on a separate
always-on machine. It polls `/air`, records to MariaDB, serves a bucketed
history API to the app, and publishes the device to Home Assistant over MQTT
discovery. It requires no firmware changes — see its README for the design
rationale and deployment.

## Companion app

The Flutter app lives at https://github.com/VLDG2712/AQI-Monitor.

## License

See [LICENSE](LICENSE).
