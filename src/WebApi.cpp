// WebApi.cpp — HTTP handlers, bearer auth and the dashboard page.
#include "WebApi.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>

#include "Config.h"
#include "Constants.h"
#include "Display.h"
#include "Globals.h"
#include "Network.h"

//HTTP handlers

void handleAirData() {
  static uint32_t lastRequest = 0;
  uint32_t now = millis();
  if (now - lastRequest < 1000) {
    server->send(429, "text/plain", "Too Many Requests");
    return;
  }
  lastRequest = now;

  JsonDocument doc;
  doc["temperature"]   = sensors.temperature;
  doc["humidity"]      = sensors.humidity;
  doc["eco2"]          = sensors.eco2;
  doc["tvoc"]          = sensors.tvoc;
  doc["aqi"]           = sensors.aqi;
  doc["aqi_label"]     = aqiLabel(sensors.aqi);
  doc["pressure_hpa"]  = sensors.pressure_hPa;
  doc["altitude_m"]    = sensors.altitude_m;
  doc["bmp_temp"]      = sensors.bmp_temp;
  doc["pm1_0"]         = sensors.pm1_0;
  doc["pm2_5"]         = sensors.pm2_5;
  doc["pm10"]          = sensors.pm10;
  doc["chip_temp"]     = sensors.chipTemp;
  doc["ready"]         = sensors.ready;
  doc["uptime_s"]      = millis() / 1000;
  doc["fast_wifi"]     = rtcValid;

  String json;
  serializeJson(doc, json);
  server->sendHeader("Access-Control-Allow-Origin", "*");
  server->sendHeader("Cache-Control", "no-cache");
  server->send(200, "application/json", json);
}


const char* API_TOKEN = SECRET_API_TOKEN;

bool checkAuth() {
  if (!server->hasHeader("Authorization")) {
    server->sendHeader("WWW-Authenticate", "Bearer realm=\"ESP32\"");
    server->send(401, "text/plain", "Unauthorized");
    return false;
  }
  String auth = server->header("Authorization");
  String expected = String("Bearer ") + API_TOKEN;
  if (auth != expected) {
    server->send(403, "text/plain", "Forbidden");
    return false;
  }
  return true;
}

void handleConfigGet() {
  if (!checkAuth()) return;
  JsonDocument doc;
  doc["wifi_ssid"]       = cfg.wifi_ssid;
  doc["wifi_password"]   = "********";
  doc["server_port"]     = cfg.server_port;
  doc["temp_offset"]     = cfg.temp_offset;
  doc["hum_offset"]      = cfg.hum_offset;
  doc["warmup_ms"]       = cfg.warmup_ms;
  doc["sensor_interval"] = cfg.sensor_interval;
  doc["neo_brightness"]  = cfg.neo_brightness;
  doc["neo_enabled"]     = cfg.neo_enabled;
  doc["rtc_valid"]       = rtcValid;
  doc["rtc_channel"]     = rtcChannel;

  String json;
  serializeJson(doc, json);
  server->sendHeader("Access-Control-Allow-Origin", "*");
  server->send(200, "application/json", json);
}

// /neo GET returns all NeoPixel state
void handleNeoGet() {
  JsonDocument doc;
  xSemaphoreTake(cfgMutex, portMAX_DELAY);
  doc["enabled"]    = cfg.neo_enabled;
  doc["brightness"] = cfg.neo_brightness;
  doc["manual"]     = cfg.neo_manual;
  doc["effect"]     = cfg.neo_effect;
  doc["r"]          = cfg.neo_r;
  doc["g"]          = cfg.neo_g;
  doc["b"]          = cfg.neo_b;
  doc["speed"]      = cfg.neo_speed;
  xSemaphoreGive(cfgMutex);

  String json;
  serializeJson(doc, json);
  server->sendHeader("Access-Control-Allow-Origin", "*");
  server->send(200, "application/json", json);
}

// ── /neo POST — partial-update any NeoPixel field ───────────
void handleNeoSet() {
  if (!checkAuth()) return;
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server->arg("plain"))) {
    server->send(400, "text/plain", "Invalid JSON");
    return;
  }

  xSemaphoreTake(cfgMutex, portMAX_DELAY);
  if (doc.containsKey("enabled"))    cfg.neo_enabled    = doc["enabled"];
  if (doc.containsKey("brightness")) {
    cfg.neo_brightness = doc["brightness"].as<uint8_t>();
    neo.setBrightness(cfg.neo_brightness);
  }
  if (doc.containsKey("manual"))     cfg.neo_manual     = doc["manual"];
  if (doc.containsKey("effect")) {
    uint8_t newFx = doc["effect"].as<uint8_t>();
    if (newFx != cfg.neo_effect) {
      cfg.neo_effect       = newFx;
      neoState.spinPos     = 0;
      neoState.rainbowHue  = 0;
      neoState.theaterStep = 0;
      neoState.strobeOn    = false;
      neoState.breathVal   = 0.0f;
      neoState.breathDir   = 1.0f;
    }
  }
  if (doc.containsKey("r"))     cfg.neo_r     = doc["r"].as<uint8_t>();
  if (doc.containsKey("g"))     cfg.neo_g     = doc["g"].as<uint8_t>();
  if (doc.containsKey("b"))     cfg.neo_b     = doc["b"].as<uint8_t>();
  if (doc.containsKey("speed")) cfg.neo_speed = doc["speed"].as<uint8_t>();
  xSemaphoreGive(cfgMutex);

  saveConfig();
  server->sendHeader("Access-Control-Allow-Origin", "*");
  server->send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleConfigSet() {
  if (!checkAuth()) return;
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server->arg("plain"))) {
    server->send(400, "text/plain", "Invalid JSON");
    return;
  }

  bool wifiChanged = false;
  if (doc["wifi_ssid"].is<const char*>()) {
    strlcpy(cfg.wifi_ssid, doc["wifi_ssid"], sizeof(cfg.wifi_ssid));
    wifiChanged = true;
  }
  if (doc["wifi_password"].is<const char*>()) {
    strlcpy(cfg.wifi_password, doc["wifi_password"], sizeof(cfg.wifi_password));
    wifiChanged = true;
  }
  if (doc["server_port"].is<uint16_t>())      cfg.server_port     = doc["server_port"];
  if (doc["temp_offset"].is<float>())         cfg.temp_offset     = doc["temp_offset"];
  if (doc["hum_offset"].is<float>())          cfg.hum_offset      = doc["hum_offset"];
  if (doc["warmup_ms"].is<uint32_t>())        cfg.warmup_ms       = doc["warmup_ms"];
  if (doc["sensor_interval"].is<uint32_t>())  cfg.sensor_interval = doc["sensor_interval"];
  if (doc["neo_brightness"].is<uint8_t>()) {
    xSemaphoreTake(cfgMutex, portMAX_DELAY);
    cfg.neo_brightness = doc["neo_brightness"];
    neo.setBrightness(cfg.neo_brightness);
    xSemaphoreGive(cfgMutex);
  }
  if (doc.containsKey("neo_enabled")) cfg.neo_enabled = doc["neo_enabled"];

  saveConfig();

  if (wifiChanged) {
    rtcValid = false; rtcIp = 0;
    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->send(200, "application/json",
      "{\"status\":\"saved\",\"note\":\"WiFi changed — reconnecting\"}");
    delay(500);
    WiFi.disconnect();
    wifiConnect();
  } else {
    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->send(200, "application/json", "{\"status\":\"saved\"}");
  }
}

void handleConfigReset() {
  if (!checkAuth()) return;
  applyDefaults();
  saveConfig();
  rtcValid = false; rtcIp = 0;
  server->sendHeader("Access-Control-Allow-Origin", "*");
  server->send(200, "application/json", "{\"status\":\"reset to defaults\"}");
}

void handleClearRtc() {
  if (!checkAuth()) return;
  rtcValid = false; rtcIp = 0;
  server->sendHeader("Access-Control-Allow-Origin", "*");
  server->send(200, "application/json", "{\"status\":\"RTC cleared\"}");
}

// ── Root page ──
void handleRoot() {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
 <meta charset='UTF-8'>
 <meta name='viewport' content='width=device-width, initial-scale=1'>
 <title>Homelab Air Monitor</title>
 <style>
  *{margin:0;padding:0;box-sizing:border-box;}
  body{background:#000;color:#fff;font-family:'Courier New',monospace;min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:30px 20px;}
  h1{color:#00ffff;font-size:1.4em;letter-spacing:3px;text-transform:uppercase;border-bottom:1px solid #00ffff;padding-bottom:10px;margin-bottom:25px;width:100%;max-width:500px;text-align:center;}
  #livestate{font-size:0.5em;vertical-align:middle;margin-left:10px;transition:color .3s;}
  .grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;width:100%;max-width:500px;margin-bottom:25px;}
  .card{background:#0a0a0a;border:1px solid #1a1a1a;border-radius:8px;padding:14px;}
  .card .label{font-size:0.65em;color:#555;text-transform:uppercase;letter-spacing:2px;margin-bottom:5px;}
  .card .value{font-size:1.5em;font-weight:bold;}
  .temp .value{color:#fd8000;} .humid .value{color:#00ffff;}
  .eco2 .value{color:#00ff00;} .tvoc  .value{color:#ff00ff;}
  .aqi  .value{color:#00ff00;} .press .value{color:#ff00ff;}
  .pm25 .value{color:#00ff00;} .pm10  .value{color:#00e5ff;}
  .endpoints{width:100%;max-width:500px;background:#0a0a0a;border:1px solid #1a1a1a;border-radius:8px;padding:16px;margin-bottom:20px;}
  .endpoints .title{color:#555;font-size:0.65em;letter-spacing:2px;text-transform:uppercase;margin-bottom:12px;}
  .endpoint{display:flex;align-items:center;gap:10px;margin-bottom:8px;font-size:0.85em;}
  .method{background:#001a1a;color:#00ffff;padding:2px 8px;border-radius:4px;font-size:0.75em;min-width:40px;text-align:center;}
  .method.post{background:#1a0a00;color:#fd8000;}
  .path{color:#fff;} .desc{color:#444;font-size:0.8em;margin-left:auto;}
  .footer{color:#222;font-size:0.7em;text-align:center;}
  .footer span{color:#333;}
  /* ── NeoPixel control styles ─────────────── */
  .neo-card{width:100%;max-width:500px;margin-bottom:20px;}
  .neo-title{color:#00ffff;font-size:0.65em;letter-spacing:2px;text-transform:uppercase;margin-bottom:2px;}
  .neo-sub{color:#888;font-size:0.7em;margin-bottom:14px;}
  .neo-row{display:flex;align-items:center;justify-content:space-between;margin-bottom:14px;}
  .neo-section{color:#444;font-size:0.6em;letter-spacing:2px;text-transform:uppercase;margin:12px 0 6px;}
  .mode-btn{flex:1;padding:7px 0;background:#0a0a0a;border:1px solid #1a1a1a;border-radius:4px;color:#555;font-family:'Courier New',monospace;font-size:0.8em;cursor:pointer;transition:all .2s;margin:0 3px;}
  .mode-btn.active{background:#001a1a;border-color:#00ffff;color:#00ffff;}
  .mode-btn:hover:not(.active){border-color:#333;color:#aaa;}
  .slider-row{display:flex;align-items:center;gap:10px;margin-bottom:10px;}
  .slider-row input[type=range]{flex:1;accent-color:#00ffff;}
  .slider-val{color:#fff;font-weight:bold;min-width:30px;text-align:right;font-size:0.9em;}
  .color-row{display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-bottom:10px;}
  .color-swatch{width:44px;height:34px;border:2px solid #1a3a3a;border-radius:6px;cursor:pointer;padding:0;background:none;}
  .preset-btn{background:#0a0a0a;border:1px solid #1a1a1a;border-radius:4px;padding:4px 9px;cursor:pointer;font-family:'Courier New',monospace;font-size:0.72em;color:#777;transition:all .15s;}
  .preset-btn:hover{border-color:#00ffff;color:#00ffff;}
  .preset-btn.selected{border-color:#00ffff;color:#00ffff;background:#001a1a;}
  .fx-select{width:100%;background:#0a0a0a;color:#00ffff;border:1px solid #1a3a3a;border-radius:6px;padding:7px 10px;font-family:'Courier New',monospace;font-size:0.88em;cursor:pointer;margin-bottom:10px;}
  .scene-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;margin-bottom:4px;}
  .scene-btn{background:#0a0a0a;border:1px solid #1a1a1a;border-radius:5px;padding:7px 4px;cursor:pointer;font-family:'Courier New',monospace;font-size:0.72em;color:#666;transition:all .15s;text-align:center;}
  .scene-btn:hover{border-color:#fd8000;color:#fd8000;}
  .toggle-wrap{display:flex;align-items:center;gap:8px;}
  .toggle-wrap input[type=checkbox]{accent-color:#00ffff;width:15px;height:15px;}
  .manual-section{border-top:1px solid #1a1a1a;padding-top:14px;margin-top:4px;}
 </style>
</head>
<body>
 <h1>&#127756; Homelab Air Monitor <span id='livestate' style='color:#1a3a1a;'>&#9679;</span></h1>
 <div class='grid'>
  <div class='card temp'><div class='label'>Temperature</div><div class='value' id='val-temp'>)";
  html += String(sensors.temperature, 1);
  html += R"(&#8451;</div></div>
  <div class='card humid'><div class='label'>Humidity</div><div class='value' id='val-hum'>)";
  html += String(sensors.humidity, 1);
  html += R"(%</div></div>
  <div class='card eco2'><div class='label'>eCO2</div><div class='value' id='val-eco2'>)";
  html += String(sensors.eco2);
  html += R"( ppm</div></div>
  <div class='card tvoc'><div class='label'>TVOC</div><div class='value' id='val-tvoc'>)";
  html += String(sensors.tvoc);
  html += R"( ppb</div></div>
  <div class='card aqi'><div class='label'>AQI</div><div class='value' id='val-aqi'>)";
  html += String(sensors.aqi);
  html += R"( &mdash; )";
  html += String(aqiLabel(sensors.aqi));
  html += R"(</div></div>
  <div class='card press'><div class='label'>Pressure</div><div class='value' id='val-press'>)";
  html += String(sensors.pressure_hPa, 1);
  html += R"( hPa</div></div>
  <div class='card pm25'><div class='label'>PM2.5</div><div class='value' id='val-pm25'>)";
  html += String(sensors.pm2_5);
  html += R"( &mu;g</div></div>
  <div class='card pm10'><div class='label'>PM10</div><div class='value' id='val-pm10'>)";
  html += String(sensors.pm10);
  html += R"( &mu;g</div></div>
 </div>

 <!-- ── NeoPixel Control ───────────────────────────────── -->
 <div class='neo-card'>
  <div class='card' style='padding:16px;'>
   <div class='neo-row' style='margin-bottom:10px;'>
    <div>
     <div class='neo-title'>NeoPixel Ring</div>
     <div class='neo-sub'>16-LED WS2812B control</div>
    </div>
    <div class='toggle-wrap'>
     <input type='checkbox' id='neoToggle'>
     <span style='color:#888;font-size:0.85em;'>Enabled</span>
    </div>
   </div>

   <!-- Mode selector -->
   <div style='display:flex;margin-bottom:14px;'>
    <button class='mode-btn' id='btnSensor' onclick='setMode(false)'>&#127912; SENSOR</button>
    <button class='mode-btn' id='btnManual' onclick='setMode(true)'>&#127899; MANUAL</button>
   </div>

   <!-- Brightness — always visible -->
   <div class='neo-section'>Brightness</div>
   <div class='slider-row'>
    <input type='range' id='neoBrightness' min='0' max='255' value='175'
     oninput='document.getElementById("neoBrightVal").textContent=this.value'
     onchange='sendNeo({brightness:parseInt(this.value)})'>
    <span class='slider-val' id='neoBrightVal'>175</span>
   </div>

   <!-- Manual controls -->
   <div class='manual-section' id='manualControls'>

    <!-- Scene presets -->
    <div class='neo-section'>Quick Scenes</div>
    <div class='scene-grid'>
     <button class='scene-btn' onclick='applyScene("relax")'>&#127772; RELAX</button>
     <button class='scene-btn' onclick='applyScene("focus")'>&#128161; FOCUS</button>
     <button class='scene-btn' onclick='applyScene("party")'>&#127881; PARTY</button>
     <button class='scene-btn' onclick='applyScene("alert")'>&#9888; ALERT</button>
     <button class='scene-btn' onclick='applyScene("chill")'>&#10052; CHILL</button>
     <button class='scene-btn' onclick='applyScene("fire")'>&#128293; FIRE</button>
    </div>

    <!-- Color picker + presets -->
    <div class='neo-section'>Color</div>
    <div class='color-row'>
     <input type='color' id='neoColor' class='color-swatch' value='#00c864'
      oninput='onColorPick(this.value)'>
     <button class='preset-btn' data-hex='#ff2200' onclick='pickPreset(this)'>RED</button>
     <button class='preset-btn' data-hex='#00ff44' onclick='pickPreset(this)'>GREEN</button>
     <button class='preset-btn' data-hex='#0088ff' onclick='pickPreset(this)'>BLUE</button>
     <button class='preset-btn' data-hex='#ff8800' onclick='pickPreset(this)'>AMBER</button>
     <button class='preset-btn' data-hex='#cc00ff' onclick='pickPreset(this)'>PURPLE</button>
     <button class='preset-btn' data-hex='#ffffff' onclick='pickPreset(this)'>WHITE</button>
    </div>

    <!-- Effect selector -->
    <div class='neo-section'>Effect</div>
    <select class='fx-select' id='neoEffect' onchange='onEffectChange(parseInt(this.value))'>
     <option value='0'>-- Static</option>
     <option value='1' selected>~~ Breathing</option>
     <option value='2'>-&gt; Chase / Spin</option>
     <option value='3'>** Rainbow Cycle</option>
     <option value='4'>!! Strobe</option>
     <option value='5'>^^ Fire</option>
     <option value='6'>.&gt; Theater Chase</option>
     <option value='7'>** Sparkle</option>
     <option value='8'>@@ Color Cycle</option>
    </select>

    <!-- Speed — hidden for static -->
    <div id='speedSection'>
     <div class='neo-section'>Speed</div>
     <div class='slider-row'>
      <input type='range' id='neoSpeed' min='0' max='255' value='128'
       oninput='document.getElementById("neoSpeedVal").textContent=this.value'
       onchange='sendNeo({speed:parseInt(this.value)})'>
      <span class='slider-val' id='neoSpeedVal'>128</span>
     </div>
    </div>

   </div><!-- /manualControls -->
  </div>
 </div><!-- /neo-card -->

 <!-- API endpoints -->
 <div class='endpoints'>
  <div class='title'>API Endpoints</div>
  <div class='endpoint'><span class='method'>GET</span><span class='path'>/air</span><span class='desc'>sensor JSON</span></div>
  <div class='endpoint'><span class='method'>GET</span><span class='path'>/neo</span><span class='desc'>neopixel state</span></div>
  <div class='endpoint'><span class='method post'>POST</span><span class='path'>/neo</span><span class='desc'>update any neo field</span></div>
  <div class='endpoint'><span class='method'>GET</span><span class='path'>/config</span><span class='desc'>get config</span></div>
  <div class='endpoint'><span class='method post'>POST</span><span class='path'>/config</span><span class='desc'>update config</span></div>
  <div class='endpoint'><span class='method post'>POST</span><span class='path'>/config/reset</span><span class='desc'>reset defaults</span></div>
  <div class='endpoint'><span class='method post'>POST</span><span class='path'>/rtc/clear</span><span class='desc'>clear WiFi cache</span></div>
 </div>

 <div class='footer'>
  uptime: <span id='ft-uptime'>)";
  html += String(millis() / 1000);
  html += R"(s</span> &nbsp;|&nbsp;
  cpu: <span id='ft-cpu'>)";
  html += String((int)sensors.chipTemp);
  html += R"(&#8451;</span> &nbsp;|&nbsp;
  ip: <span id='ft-ip'>)";
  html += WiFi.localIP().toString();
  html += R"(</span>
 </div>

 <div id='token-bar' style='width:100%;max-width:500px;margin-bottom:16px;background:#0a0a0a;border:1px solid #1a1a1a;border-radius:8px;padding:12px;display:flex;align-items:center;gap:8px;'>
  <span style='color:#555;font-size:0.65em;letter-spacing:2px;text-transform:uppercase;white-space:nowrap;'>API Token</span>
  <input id='tokenInput' type='password' placeholder='Enter bearer token...'
   style='flex:1;background:#000;border:1px solid #1a3a3a;border-radius:4px;padding:6px 10px;color:#00ffff;font-family:Courier New,monospace;font-size:0.8em;outline:none;'
   oninput='saveToken(this.value)'>
  <span id='tokenStatus' style='font-size:0.65em;color:#555;white-space:nowrap;'>not set</span>
 </div>

 <script>
  function saveToken(v){localStorage.setItem('aqi_token',v);var s=document.getElementById('tokenStatus');s.textContent=v?'saved ✓':'not set';s.style.color=v?'#00ff00':'#555';}
  function loadToken(){var t=localStorage.getItem('aqi_token')||'';document.getElementById('tokenInput').value=t;var s=document.getElementById('tokenStatus');s.textContent=t?'saved ✓':'not set';s.style.color=t?'#00ff00':'#555';}
  document.addEventListener('DOMContentLoaded',loadToken);
  //Sensor color helpers
  const co2Col = v => v<600?'#00ff00':v<1000?'#ffff00':v<1500?'#fd8000':'#ff0000';
  const pmCol  = v => v<12 ?'#00ff00':v<35  ?'#ffff00':v<55  ?'#fd8000':'#ff0000';
  const aqiCol = {1:'#00ff00',2:'#00ffff',3:'#ffff00',4:'#fd8000',5:'#ff0000'};

  function sv(id, text, color) {
   const el = document.getElementById(id);
   if (!el) return;
   el.textContent = text;
   if (color) el.style.color = color;
  }

  // Live sensor polling every 2s
  let dotTimer = null;
  async function pollSensors() {
   try {
    const d = await fetch('/air').then(r => r.json());
    sv('val-temp',  d.temperature.toFixed(1) + '\u2103');
    sv('val-hum',   d.humidity.toFixed(1) + '%');
    sv('val-eco2',  d.eco2 + ' ppm',  co2Col(d.eco2));
    sv('val-tvoc',  d.tvoc + ' ppb');
    const lbl = (d.aqi_label || '').trim() || 'Warmup';
    sv('val-aqi',   d.aqi + ' \u2014 ' + lbl, aqiCol[d.aqi] || '#7bef7b');
    sv('val-press', d.pressure_hpa.toFixed(1) + ' hPa');
    sv('val-pm25',  d.pm2_5 + ' \u00b5g', pmCol(d.pm2_5));
    sv('val-pm10',  d.pm10  + ' \u00b5g', pmCol(d.pm10));
    sv('ft-uptime', d.uptime_s + 's');
    sv('ft-cpu',    Math.round(d.chip_temp) + '\u2103');
    const dot = document.getElementById('livestate');
    if (dot) {
     dot.style.color = '#00ff00';
     clearTimeout(dotTimer);
     dotTimer = setTimeout(() => { dot.style.color = '#1a3a1a'; }, 400);
    }
   } catch(e) {
    const dot = document.getElementById('livestate');
    if (dot) dot.style.color = '#ff0000';
   }
  }
  setInterval(pollSensors, 2000);
  pollSensors();

  // NeoPixel controls
  const SCENES = {
   relax: {effect:1, r:255, g:140, b:0,   speed:55,  brightness:120},
   focus: {effect:0, r:180, g:210, b:255,  speed:128, brightness:210},
   party: {effect:3, r:0,   g:0,   b:0,    speed:220, brightness:255},
   alert: {effect:4, r:255, g:0,   b:0,    speed:210, brightness:255},
   chill: {effect:1, r:0,   g:80,  b:255,  speed:35,  brightness:100},
   fire:  {effect:5, r:0,   g:0,   b:0,    speed:160, brightness:200}
  };
  let debounce = null;
  function sendNeo(patch, immediate) {
   if (!immediate) {
    clearTimeout(debounce);
    debounce = setTimeout(() => _doSend(patch), 120);
   } else { _doSend(patch); }
  }
  function _doSend(patch) {
   const token = localStorage.getItem('aqi_token') || '';
   fetch('/neo', {method:'POST', headers:{'Content-Type':'application/json','Authorization':'Bearer '+token}, body:JSON.stringify(patch)}).catch(() => {});
  }
  function hexToRgb(hex) {
   return {r:parseInt(hex.slice(1,3),16), g:parseInt(hex.slice(3,5),16), b:parseInt(hex.slice(5,7),16)};
  }
  function rgbToHex(r,g,b) {
   return '#'+[r,g,b].map(x=>x.toString(16).padStart(2,'0')).join('');
  }
  function setMode(manual) {
   document.getElementById('btnSensor').classList.toggle('active', !manual);
   document.getElementById('btnManual').classList.toggle('active', manual);
   document.getElementById('manualControls').style.display = manual ? 'block' : 'none';
   sendNeo({manual: manual}, true);
  }
  function onColorPick(hex) {
   const {r,g,b} = hexToRgb(hex);
   document.querySelectorAll('.preset-btn').forEach(b => b.classList.remove('selected'));
   sendNeo({r,g,b});
  }
  function pickPreset(btn) {
   const hex = btn.dataset.hex;
   document.getElementById('neoColor').value = hex;
   document.querySelectorAll('.preset-btn').forEach(b => b.classList.remove('selected'));
   btn.classList.add('selected');
   const {r,g,b} = hexToRgb(hex);
   sendNeo({r,g,b}, true);
  }
  function onEffectChange(fx) {
   document.getElementById('speedSection').style.display = (fx === 0) ? 'none' : 'block';
   sendNeo({effect: fx}, true);
  }
  function applyScene(name) {
   const s = SCENES[name];
   if (!s) return;
   setMode(true);
   document.getElementById('neoEffect').value = s.effect;
   document.getElementById('speedSection').style.display = (s.effect === 0) ? 'none' : 'block';
   document.getElementById('neoSpeed').value = s.speed;
   document.getElementById('neoSpeedVal').textContent = s.speed;
   document.getElementById('neoBrightness').value = s.brightness;
   document.getElementById('neoBrightVal').textContent = s.brightness;
   if (s.r || s.g || s.b) document.getElementById('neoColor').value = rgbToHex(s.r, s.g, s.b);
   document.querySelectorAll('.preset-btn').forEach(b => b.classList.remove('selected'));
   _doSend({manual:true, effect:s.effect, r:s.r, g:s.g, b:s.b, speed:s.speed, brightness:s.brightness});
  }
  function loadNeoState() {
   fetch('/neo').then(r => r.json()).then(j => {
    document.getElementById('neoToggle').checked = !!j.enabled;
    document.getElementById('neoToggle').onchange = e => sendNeo({enabled: e.target.checked}, true);
    document.getElementById('btnSensor').classList.toggle('active', !j.manual);
    document.getElementById('btnManual').classList.toggle('active', !!j.manual);
    document.getElementById('manualControls').style.display = j.manual ? 'block' : 'none';
    document.getElementById('neoBrightness').value = j.brightness || 175;
    document.getElementById('neoBrightVal').textContent = j.brightness || 175;
    document.getElementById('neoColor').value = rgbToHex(j.r||0, j.g||200, j.b||100);
    document.getElementById('neoEffect').value = j.effect || 0;
    document.getElementById('speedSection').style.display = (j.effect === 0) ? 'none' : 'block';
    document.getElementById('neoSpeed').value = j.speed || 128;
    document.getElementById('neoSpeedVal').textContent = j.speed || 128;
   }).catch(() => {});
  }
  document.addEventListener('DOMContentLoaded', loadNeoState);
 </script>
</body>
</html>
)";

  server->sendHeader("Access-Control-Allow-Origin", "*");
  server->send(200, "text/html", html);
}

void handleNotFound() {
  server->send(404, "text/plain", "Not found");
}


void webServerBegin() {
  server = new WebServer(cfg.server_port);
  server->on("/",             HTTP_GET,  handleRoot);
  server->on("/air",          HTTP_GET,  handleAirData);
  server->on("/config",       HTTP_GET,  handleConfigGet);
  server->on("/config",       HTTP_POST, handleConfigSet);
  server->on("/config/reset", HTTP_POST, handleConfigReset);
  server->on("/rtc/clear",    HTTP_POST, handleClearRtc);
  server->on("/neo",          HTTP_GET,  handleNeoGet);
  server->on("/neo",          HTTP_POST, handleNeoSet);
  const char* headerKeys[] = {"Authorization"};
  server->collectHeaders(headerKeys, 1);
  server->onNotFound(handleNotFound);
  server->begin();
  Serial.printf("HTTP server on port %d\n", cfg.server_port);
}
