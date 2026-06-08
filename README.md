# ESP32 Air Quality Monitor

This repository contains the firmware and documentation for a professional-grade DIY Air Quality Monitor based on the ESP32 platform. It tracks crucial environmental metrics and provides a clean, responsive web interface for real-time monitoring.

## Features

- **Comprehensive Monitoring**: Tracks Temperature, Humidity, eCO2, TVOC, Air Quality Index (AQI), Atmospheric Pressure, and PM1.0/PM2.5/PM10 particle counts.
- **Visual Feedback**: Integrated TFT display for immediate local data readout.
- **Ambient Lighting**: 16-LED NeoPixel ring for status visualization (AQI alerts, connectivity, etc.) with customizable effects.
- **Connectivity**: Stable Wi-Fi connectivity with fast-reconnect capabilities using RTC memory.
- **Web Interface**: Responsive, minimalist dashboard for monitoring and configuration.
- **API Support**: Includes REST API endpoints and WebSocket support for live data streaming.
- **Security**: Basic Auth protected configuration endpoints.
- **Flutter Companion App**: Designed to pair with the Flutter app at https://github.com/VLDG2712/AQI-Monitor for mobile monitoring and control.
- **3D Printable Case**: Coming soon! 3D printable almost finished just needs small finish touches.
## Hardware Requirements

- **Microcontroller**: ESP32 Development Board
- **Sensors**:
  - AHT21 (Temp/Humidity)
  - ENS160 (Air Quality/eCO2/TVOC)
  - BMP580 (Pressure/Altitude)
  - PMS5003 (Particulate Matter)
- **Display**: TFT LCD (TFT_eSPI compatible)
- **Lighting**: 16-LED WS2812B NeoPixel Ring

## Firmware Setup

1.  **Clone the Repository**: `git clone https://github.com/VLDG2712/AQIMonitor`
2.  **Install Dependencies**: Use PlatformIO or Arduino IDE to install the following libraries:
    - TFT_eSPI
    - Adafruit_AHTX0
    - ScioSense_ENS160
    - Adafruit_BMP5xx
    - Adafruit_NeoPixel
    - PMS
    - ArduinoJson
    - WebSockets (by Markus Sattler)
3.  **Configure**: 
    - Update `DEFAULT_SSID` and `DEFAULT_PASSWORD` in `AirQualitySensor.ino` with your preferred default credentials.
    - Change the `API_TOKEN` to a secure, unique string.
4.  **Upload**: Compile and upload to your ESP32 board.

## Web Interface & API

The monitor hosts a local web server (default port `9091`).

- **Dashboard**: Access `http://<device-ip>` in your browser.
- **API Endpoints**:
    - `GET /air`: Get current sensor readings in JSON.
    - `WS :9092`: WebSocket for live sensor data streaming.
    - `POST /config`: Update device configuration (Requires Bearer Auth).

## Security

- The firmware implements Basic Authentication for all configuration-changing endpoints.
- **Important**: Always change the default `API_TOKEN` before deploying to a network.

## License

This project is open-source. See the LICENSE file for more details.
