// Network.cpp — WiFi bring-up and reconnection.
#include "Network.h"

#include "Config.h"
#include "Constants.h"
#include "Globals.h"
#include "Display.h"

//WiFi

void wifiConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  if (rtcValid && rtcIp != 0) {
    Serial.println("WiFi fast connect using RTC cache...");
    IPAddress ip(rtcIp), gateway(rtcGateway), subnet(rtcSubnet), dns(rtcGateway);
    WiFi.config(ip, gateway, subnet, dns);
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_password, rtcChannel, rtcBssid, true);
  } else {
    Serial.println("WiFi full connect...");
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_password);
  }
  wifiState.lastAttemptMs = millis();
}

void wifiCheck() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiState.connected) {
      wifiState.connected = true;
      Serial.println("WiFi connected: " + WiFi.localIP().toString());
      if (!rtcValid) {
        memcpy(rtcBssid, WiFi.BSSID(), 6);
        rtcChannel = WiFi.channel();
        rtcIp      = (uint32_t)WiFi.localIP();
        rtcGateway = (uint32_t)WiFi.gatewayIP();
        rtcSubnet  = (uint32_t)WiFi.subnetMask();
        rtcValid   = true;
        Serial.printf("RTC cached — IP:%s Ch:%d\n",
                      WiFi.localIP().toString().c_str(), rtcChannel);
      }
    }
    return;
  }
  wifiState.connected = false;
  if (rtcValid && millis() - wifiState.lastAttemptMs > 5000) {
    Serial.println("Fast connect failed — clearing RTC");
    rtcValid = false; rtcIp = 0;
  }
  if (millis() - wifiState.lastAttemptMs > 30000) {
    Serial.println("Reconnecting WiFi...");
    WiFi.disconnect();
    wifiConnect();
  }
}

