#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <Adafruit_AHTX0.h>
#include <ScioSense_ENS160.h>
#include <Adafruit_BMP5xx.h>
#include <Adafruit_NeoPixel.h>
#include <PMS.h>
#include <esp_task_wdt.h>
#include <LittleFS.h>

const char* DEFAULT_SSID            = "Homelab_Setup"; // Wifi SSID here
const char* DEFAULT_PASSWORD        = "changeme123"; // Wifi password here
const uint16_t DEFAULT_SERVER_PORT     = 9091; // Port here default: 9091
const float    DEFAULT_TEMP_OFFSET     = -5.7f; // Change These to match with a thermometer, for eg. when the sensor heats the temp shows higher than usual hence the subtraction to match the thermometer.
const float    DEFAULT_HUM_OFFSET      = +8.0f; // Change These to match with a hygrometer, for eg. when the sensor heats the humidity lowers and hence the adition of humidity to match a hygrometer.
const uint32_t DEFAULT_WARMUP_MS       = 120000; // Warmup in ms
const uint32_t DEFAULT_SENSOR_INTERVAL = 2000; // Sensor readings interval
const uint8_t  DEFAULT_NEO_BRIGHTNESS  = 175; // Neopixel brightness

const uint8_t PIN_NEO    = 32;
const uint8_t PIN_PMS_RX = 16;
const uint8_t PIN_PMS_TX = 17;
const uint8_t PIN_LED    = 2;
const uint8_t PIN_TFT_BL = 13;
const uint8_t NEO_COUNT  = 16;
const uint8_t ADDR_ENS160 = 0x53;
const uint8_t ADDR_BMP580  = 0x47;

const uint16_t C_BG       = 0x0000;
const uint16_t C_SURFACE  = 0x0841;
const uint16_t C_HEADER   = 0x07FF;
const uint16_t C_DIVIDER  = 0x18C3;
const uint16_t C_LABEL    = 0x7BEF;
const uint16_t C_DIM      = 0x39E7;
const uint16_t C_TEMP     = 0xFD20;
const uint16_t C_HUMID    = 0x07FF;
const uint16_t C_TVOC     = 0xF81F;
const uint16_t C_PRESSURE = 0xF81F;
const uint16_t C_CO2_GOOD = 0x07E0;
const uint16_t C_CO2_MED  = 0xFFE0;
const uint16_t C_CO2_BAD  = 0xFD20;
const uint16_t C_CO2_UGLY = 0xF800;
const uint16_t C_AQI_1    = 0x07E0;
const uint16_t C_AQI_2    = 0x07FF;
const uint16_t C_AQI_3    = 0xFFE0;
const uint16_t C_AQI_4    = 0xFD20;
const uint16_t C_AQI_5    = 0xF800;
const uint16_t C_WARN     = 0xFFE0;
const uint16_t C_PM_GOOD  = 0x07E0;
const uint16_t C_PM_MED   = 0xFFE0;
const uint16_t C_PM_BAD   = 0xFD20;
const uint16_t C_PM_UGLY  = 0xF800;

const uint32_t LED_HEARTBEAT_MS = 2000;
const uint16_t LED_FLASH_ON_MS  = 150;
const uint16_t LED_FLASH_OFF_MS = 250;
const uint16_t LED_ERROR_GAP_MS = 2000;
const uint32_t WDT_TIMEOUT_S   = 10;

const uint8_t FX_STATIC    = 0;
const uint8_t FX_BREATH    = 1;
const uint8_t FX_SPIN      = 2;
const uint8_t FX_RAINBOW   = 3;
const uint8_t FX_STROBE    = 4;
const uint8_t FX_FIRE      = 5;
const uint8_t FX_THEATER   = 6;
const uint8_t FX_SPARKLE   = 7;
const uint8_t FX_COLORCYCLE= 8;

RTC_DATA_ATTR uint8_t  rtcBssid[6] = {0};
RTC_DATA_ATTR uint8_t  rtcChannel  = 0;
RTC_DATA_ATTR uint32_t rtcIp       = 0;
RTC_DATA_ATTR uint32_t rtcGateway  = 0;
RTC_DATA_ATTR uint32_t rtcSubnet   = 0;
RTC_DATA_ATTR bool     rtcValid    = false;

struct Config {
  char     wifi_ssid[64];
  char     wifi_password[64];
  uint16_t server_port;
  float    temp_offset;
  float    hum_offset;
  uint32_t warmup_ms;
  uint32_t sensor_interval;
  uint8_t  neo_brightness;
  bool     neo_enabled;
  bool     neo_manual;
  uint8_t  neo_effect;
  uint8_t  neo_r;
  uint8_t  neo_g;
  uint8_t  neo_b;
  uint8_t  neo_speed;
} cfg;

TFT_eSPI         tft = TFT_eSPI();
Adafruit_AHTX0   aht;
ScioSense_ENS160 ens160(ADDR_ENS160);
Adafruit_BMP5xx  bmp;
Adafruit_NeoPixel neo(NEO_COUNT, PIN_NEO, NEO_GRB + NEO_KHZ800);
HardwareSerial   pmsSerial(2);
PMS              pms(pmsSerial);
PMS::DATA        pmsData;
WebServer* server = nullptr;
WebSocketsServer wsServer(9092);

struct SensorData {
  float    temperature  = 0.0f;
  float    humidity     = 0.0f;
  uint16_t eco2         = 0;
  uint16_t tvoc         = 0;
  uint8_t  aqi          = 0;
  float    pressure_hPa = 0.0f;
  float    altitude_m   = 0.0f;
  float    bmp_temp     = 0.0f;
  uint16_t pm1_0        = 0;
  uint16_t pm2_5        = 0;
  uint16_t pm10         = 0;
  float    chipTemp     = 0.0f;
  bool     ready        = false;
  bool     ahtOk        = false;
  bool     ensOk        = false;
  bool     bmpOk        = false;
  bool     pmsOk        = false;
} sensors;

struct WiFiState {
  bool     connected     = false;
  uint32_t lastAttemptMs = 0;
} wifiState;

struct LedState {
  uint8_t  currentErrorIdx = 0;
  uint32_t lastCycleStart  = 0;
} led;

struct NeoState {
  uint32_t lastUpdate  = 0;
  float    breathVal   = 0.0f;
  float    breathDir   = 1.0f;
  uint8_t  spinPos     = 0;
  uint32_t lastSpin    = 0;
  uint16_t rainbowHue  = 0;
  uint8_t  theaterStep = 0;
  uint32_t lastTheater = 0;
  uint32_t lastRainbow = 0;
  uint32_t lastStrobe  = 0;
  bool     strobeOn    = false;
  uint32_t lastFire    = 0;
  uint32_t lastSparkle = 0;
  uint32_t lastStatic  = 0;
  uint8_t  fireHeat[16] = {0};
} neoState;

struct DisplayCache {
  float    temperature  = -999.0f;
  float    humidity     = -999.0f;
  uint16_t eco2         = 0xFFFF;
  uint16_t tvoc         = 0xFFFF;
  uint8_t  aqi          = 0xFF;
  float    pressure_hPa = -999.0f;
  float    altitude_m   = -999.0f;
  uint16_t pm1_0        = 0xFFFF;
  uint16_t pm2_5        = 0xFFFF;
  uint16_t pm10         = 0xFFFF;
  bool     ready        = false;
  bool     connected    = false;
} dispCache;

SemaphoreHandle_t cfgMutex      = nullptr;
TaskHandle_t      neoTaskHandle = nullptr;

const char INDEX_HTML[] PROGMEM = R"=====(
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
  .eco2 .value{color:#00ff00;} .tvoc .value{color:#ff00ff;}
  .aqi .value{color:#00ff00;}  .press .value{color:#ff00ff;}
  .pm25 .value{color:#00ff00;} .pm10 .value{color:#00e5ff;}
  .endpoints{width:100%;max-width:500px;background:#0a0a0a;border:1px solid #1a1a1a;border-radius:8px;padding:16px;margin-bottom:20px;}
  .endpoints .title{color:#555;font-size:0.65em;letter-spacing:2px;text-transform:uppercase;margin-bottom:12px;}
  .endpoint{display:flex;align-items:center;gap:10px;margin-bottom:8px;font-size:0.85em;}
  .method{background:#001a1a;color:#00ffff;padding:2px 8px;border-radius:4px;font-size:0.75em;min-width:40px;text-align:center;}
  .method.post{background:#1a0a00;color:#fd8000;}
  .path{color:#fff;} .desc{color:#444;font-size:0.8em;margin-left:auto;}
  .footer{color:#222;font-size:0.7em;text-align:center;}
  .footer span{color:#333;}
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
  <div class='card temp'><div class='label'>Temperature</div><div class='value' id='val-temp'>-- &#8451;</div></div>
  <div class='card humid'><div class='label'>Humidity</div><div class='value' id='val-hum'>-- %</div></div>
  <div class='card eco2'><div class='label'>eCO2</div><div class='value' id='val-eco2'>-- ppm</div></div>
  <div class='card tvoc'><div class='label'>TVOC</div><div class='value' id='val-tvoc'>-- ppb</div></div>
  <div class='card aqi'><div class='label'>AQI</div><div class='value' id='val-aqi'>--</div></div>
  <div class='card press'><div class='label'>Pressure</div><div class='value' id='val-press'>-- hPa</div></div>
  <div class='card pm25'><div class='label'>PM2.5</div><div class='value' id='val-pm25'>-- &mu;g</div></div>
  <div class='card pm10'><div class='label'>PM10</div><div class='value' id='val-pm10'>-- &mu;g</div></div>
 </div>

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
   <div style='display:flex;margin-bottom:14px;'>
    <button class='mode-btn' id='btnSensor' onclick='setMode(false)'>&#127912; SENSOR</button>
    <button class='mode-btn' id='btnManual' onclick='setMode(true)'>&#127899; MANUAL</button>
   </div>
   <div class='neo-section'>Brightness</div>
   <div class='slider-row'>
    <input type='range' id='neoBrightness' min='0' max='255' value='175'
     oninput='document.getElementById("neoBrightVal").textContent=this.value'
     onchange='sendNeo({brightness:parseInt(this.value)})'>
    <span class='slider-val' id='neoBrightVal'>175</span>
   </div>
   <div class='manual-section' id='manualControls'>
    <div class='neo-section'>Quick Scenes</div>
    <div class='scene-grid'>
     <button class='scene-btn' onclick='applyScene("relax")'>&#127772; RELAX</button>
     <button class='scene-btn' onclick='applyScene("focus")'>&#128161; FOCUS</button>
     <button class='scene-btn' onclick='applyScene("party")'>&#127881; PARTY</button>
     <button class='scene-btn' onclick='applyScene("alert")'>&#9888; ALERT</button>
     <button class='scene-btn' onclick='applyScene("chill")'>&#10052; CHILL</button>
     <button class='scene-btn' onclick='applyScene("fire")'>&#128293; FIRE</button>
    </div>
    <div class='neo-section'>Color</div>
    <div class='color-row'>
     <input type='color' id='neoColor' class='color-swatch' value='#00c864' oninput='onColorPick(this.value)'>
     <button class='preset-btn' data-hex='#ff2200' onclick='pickPreset(this)'>RED</button>
     <button class='preset-btn' data-hex='#00ff44' onclick='pickPreset(this)'>GREEN</button>
     <button class='preset-btn' data-hex='#0088ff' onclick='pickPreset(this)'>BLUE</button>
     <button class='preset-btn' data-hex='#ff8800' onclick='pickPreset(this)'>AMBER</button>
     <button class='preset-btn' data-hex='#cc00ff' onclick='pickPreset(this)'>PURPLE</button>
     <button class='preset-btn' data-hex='#ffffff' onclick='pickPreset(this)'>WHITE</button>
    </div>
    <div class='neo-section'>Effect</div>
    <select class='fx-select' id='neoEffect' onchange='onEffectChange(parseInt(this.value))'>
     <option value='0'>-- Static</option>
     <option value='1' selected>~~ Breathing</option>
     <option value='2'>-> Chase / Spin</option>
     <option value='3'>** Rainbow Cycle</option>
     <option value='4'>!! Strobe</option>
     <option value='5'>^^ Fire</option>
     <option value='6'>.> Theater Chase</option>
     <option value='7'>** Sparkle</option>
     <option value='8'>@@ Color Cycle</option>
    </select>
    <div id='speedSection'>
     <div class='neo-section'>Speed</div>
     <div class='slider-row'>
      <input type='range' id='neoSpeed' min='0' max='255' value='128'
       oninput='document.getElementById("neoSpeedVal").textContent=this.value'
       onchange='sendNeo({speed:parseInt(this.value)})'>
      <span class='slider-val' id='neoSpeedVal'>128</span>
     </div>
    </div>
   </div>
  </div>
 </div>

 <div class='endpoints'>
  <div class='title'>API Endpoints</div>
  <div class='endpoint'><span class='method'>GET</span><span class='path'>/air</span><span class='desc'>sensor JSON</span></div>
  <div class='endpoint'><span class='method'>WS</span><span class='path'>:9092</span><span class='desc'>live sensor push</span></div>
  <div class='endpoint'><span class='method'>GET</span><span class='path'>/neo</span><span class='desc'>neopixel state</span></div>
  <div class='endpoint'><span class='method post'>POST</span><span class='path'>/neo</span><span class='desc'>update neo field</span></div>
  <div class='endpoint'><span class='method'>GET</span><span class='path'>/config</span><span class='desc'>get config</span></div>
  <div class='endpoint'><span class='method post'>POST</span><span class='path'>/config</span><span class='desc'>update config</span></div>
  <div class='endpoint'><span class='method post'>POST</span><span class='path'>/config/reset</span><span class='desc'>reset defaults</span></div>
  <div class='endpoint'><span class='method post'>POST</span><span class='path'>/rtc/clear</span><span class='desc'>clear WiFi cache</span></div>
 </div>

 <div class='footer'>
  uptime: <span id='ft-uptime'>--s</span> &nbsp;|&nbsp; cpu: <span id='ft-cpu'>--&#8451;</span>
 </div>

 <script>
  const co2Col = v => v<600?'#00ff00':v<1000?'#ffff00':v<1500?'#fd8000':'#ff0000';
  const pmCol  = v => v<12 ?'#00ff00':v<35  ?'#ffff00':v<55  ?'#fd8000':'#ff0000';
  const aqiCol = {1:'#00ff00',2:'#00ffff',3:'#ffff00',4:'#fd8000',5:'#ff0000'};
  function sv(id, text, color) {
   const el = document.getElementById(id);
   if (!el) return;
   el.textContent = text;
   if (color) el.style.color = color;
  }
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
    if (dot) { dot.style.color = '#00ff00'; clearTimeout(dotTimer);
    dotTimer = setTimeout(() => { dot.style.color = '#1a3a1a'; }, 400);
    }
   } catch(e) {
    const dot = document.getElementById('livestate');
    if (dot) dot.style.color = '#ff0000';
   }
  }
  setInterval(pollSensors, 2000);
  pollSensors();
  const SCENES = {
   relax: {effect:1, r:255, g:140, b:0,   speed:55,  brightness:120},
   focus: {effect:0, r:180, g:210, b:255, speed:128, brightness:210},
   party: {effect:3, r:0,   g:0,   b:0,   speed:220, brightness:255},
   alert: {effect:4, r:255, g:0,   b:0,   speed:210, brightness:255},
   chill: {effect:1, r:0,   g:80,  b:255, speed:35,  brightness:100},
   fire:  {effect:5, r:0,   g:0,   b:0,   speed:160, brightness:200}
  };
  let debounce = null;
  function sendNeo(patch, immediate) {
   if (!immediate) { clearTimeout(debounce); debounce = setTimeout(() => _doSend(patch), 120);
   }
   else { _doSend(patch); }
  }
  function _doSend(patch) {
   fetch('/neo', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(patch)}).catch(() => {});
  }
  function hexToRgb(hex) { return {r:parseInt(hex.slice(1,3),16), g:parseInt(hex.slice(3,5),16), b:parseInt(hex.slice(5,7),16)}; }
  function rgbToHex(r,g,b) { return '#'+[r,g,b].map(x=>x.toString(16).padStart(2,'0')).join('');
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
   document.getElementById('speedSection').style.display = (fx === 0) ?
   'none' : 'block';
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
)=====";

void applyDefaults() {
  strlcpy(cfg.wifi_ssid,     DEFAULT_SSID,     sizeof(cfg.wifi_ssid));
  strlcpy(cfg.wifi_password, DEFAULT_PASSWORD,  sizeof(cfg.wifi_password));
  cfg.server_port     = DEFAULT_SERVER_PORT;
  cfg.temp_offset     = DEFAULT_TEMP_OFFSET;
  cfg.hum_offset      = DEFAULT_HUM_OFFSET;
  cfg.warmup_ms       = DEFAULT_WARMUP_MS;
  cfg.sensor_interval = DEFAULT_SENSOR_INTERVAL;
  cfg.neo_brightness  = DEFAULT_NEO_BRIGHTNESS;
  cfg.neo_enabled     = true;
  cfg.neo_manual      = false;
  cfg.neo_effect      = FX_BREATH;
  cfg.neo_r           = 0;
  cfg.neo_g           = 200;
  cfg.neo_b           = 100;
  cfg.neo_speed       = 128;
}

void loadConfig() {
  applyDefaults();
  if (!LittleFS.begin(true)) return;
  if (!LittleFS.exists("/config.json")) return;
  fs::File f = LittleFS.open("/config.json", "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f)) { f.close(); return; }
  f.close();
  if (doc["wifi_ssid"].is<const char*>())
    strlcpy(cfg.wifi_ssid, doc["wifi_ssid"], sizeof(cfg.wifi_ssid));
  if (doc["wifi_password"].is<const char*>())
    strlcpy(cfg.wifi_password, doc["wifi_password"], sizeof(cfg.wifi_password));
  cfg.server_port     = doc["server_port"]     | DEFAULT_SERVER_PORT;
  cfg.temp_offset     = doc["temp_offset"]     | DEFAULT_TEMP_OFFSET;
  cfg.hum_offset      = doc["hum_offset"]      | DEFAULT_HUM_OFFSET;
  cfg.warmup_ms       = doc["warmup_ms"]       | DEFAULT_WARMUP_MS;
  cfg.sensor_interval = doc["sensor_interval"] | DEFAULT_SENSOR_INTERVAL;
  cfg.neo_brightness  = doc["neo_brightness"]  | DEFAULT_NEO_BRIGHTNESS;
  cfg.neo_enabled     = doc["neo_enabled"]     | true;
  cfg.neo_manual      = doc["neo_manual"]      | false;
  cfg.neo_effect      = doc["neo_effect"]      | (uint8_t)FX_BREATH;
  cfg.neo_r           = doc["neo_r"]           | (uint8_t)0;
  cfg.neo_g           = doc["neo_g"]           | (uint8_t)200;
  cfg.neo_b           = doc["neo_b"]           | (uint8_t)100;
  cfg.neo_speed       = doc["neo_speed"]       | (uint8_t)128;
}

void saveConfig() {
  fs::File f = LittleFS.open("/config.json", "w");
  if (!f) return;
  JsonDocument doc;
  doc["wifi_ssid"]       = cfg.wifi_ssid;
  doc["wifi_password"]   = cfg.wifi_password;
  doc["server_port"]     = cfg.server_port;
  doc["temp_offset"]     = cfg.temp_offset;
  doc["hum_offset"]      = cfg.hum_offset;
  doc["warmup_ms"]       = cfg.warmup_ms;
  doc["sensor_interval"] = cfg.sensor_interval;
  doc["neo_brightness"]  = cfg.neo_brightness;
  doc["neo_enabled"]     = cfg.neo_enabled;
  doc["neo_manual"]      = cfg.neo_manual;
  doc["neo_effect"]      = cfg.neo_effect;
  doc["neo_r"]           = cfg.neo_r;
  doc["neo_g"]           = cfg.neo_g;
  doc["neo_b"]           = cfg.neo_b;
  doc["neo_speed"]       = cfg.neo_speed;
  serializeJson(doc, f);
  f.close();
}

uint16_t aqiColor(uint8_t aqi) {
  switch (aqi) {
    case 1: return C_AQI_1; case 2: return C_AQI_2;
    case 3: return C_AQI_3; case 4: return C_AQI_4;
    case 5: return C_AQI_5; default: return C_LABEL;
  }
}

const char* aqiLabel(uint8_t aqi) {
  switch (aqi) {
    case 1: return "Excellent"; case 2: return "Good     ";
    case 3: return "Moderate "; case 4: return "Poor     ";
    case 5: return "Unhealthy"; default: return "Warmup   ";
  }
}

uint16_t co2Color(uint16_t co2) {
  if (co2 < 600)  return C_CO2_GOOD;
  if (co2 < 1000) return C_CO2_MED;
  if (co2 < 1500) return C_CO2_BAD;
  return C_CO2_UGLY;
}

uint16_t pmColor(uint16_t pm25) {
  if (pm25 < 12) return C_PM_GOOD;
  if (pm25 < 35) return C_PM_MED;
  if (pm25 < 55) return C_PM_BAD;
  return C_PM_UGLY;
}

void neoSpinner(uint32_t color, uint8_t tailLen = 4) {
  if (tailLen == 0) tailLen = 1;
  uint32_t now = millis();
  if (now - neoState.lastSpin < 40) return;
  neoState.lastSpin = now;
  neo.clear();
  for (uint8_t i = 0; i < tailLen; i++) {
    uint8_t pos = (neoState.spinPos + i) % NEO_COUNT;
    uint8_t brightness = 255 / (tailLen - i);
    uint8_t r = ((color >> 16) & 0xFF) * brightness / 255;
    uint8_t g = ((color >> 8)  & 0xFF) * brightness / 255;
    uint8_t b = ((color)       & 0xFF) * brightness / 255;
    neo.setPixelColor(pos, neo.Color(r, g, b));
  }
  neo.show();
  neoState.spinPos = (neoState.spinPos + 1) % NEO_COUNT;
}

void neoBreath(uint8_t r, uint8_t g, uint8_t b, float speed = 0.02f) {
  uint32_t now = millis();
  if (now - neoState.lastUpdate < 20) return;
  neoState.lastUpdate = now;
  neoState.breathVal += neoState.breathDir * speed;
  if (neoState.breathVal >= 1.0f) { neoState.breathVal = 1.0f; neoState.breathDir = -1.0f; }
  if (neoState.breathVal <= 0.0f) { neoState.breathVal = 0.0f; neoState.breathDir =  1.0f; }
  uint8_t bright = (uint8_t)(neoState.breathVal * cfg.neo_brightness);
  neo.fill(neo.Color(r * bright / 255, g * bright / 255, b * bright / 255));
  neo.show();
}

void neoFlash(uint8_t r, uint8_t g, uint8_t b) {
  uint32_t now = millis();
  if (now - neoState.lastUpdate < 150) return;
  neoState.lastUpdate = now;
  static bool flashOn = false;
  flashOn = !flashOn;
  neo.fill(flashOn ? neo.Color(r, g, b) : neo.Color(0, 0, 0));
  neo.show();
}

void neoSpinManual(uint8_t r, uint8_t g, uint8_t b, uint8_t speed, uint8_t tailLen = 5) {
  uint32_t now = millis();
  uint32_t interval = map(255 - speed, 0, 255, 10, 200);
  if (now - neoState.lastSpin < interval) return;
  neoState.lastSpin = now;
  neo.clear();
  for (uint8_t i = 0; i < tailLen; i++) {
    uint8_t pos = (neoState.spinPos + i) % NEO_COUNT;
    uint8_t brt = 255 / (tailLen - i);
    neo.setPixelColor(pos, neo.Color(r * brt / 255, g * brt / 255, b * brt / 255));
  }
  neo.show();
  neoState.spinPos = (neoState.spinPos + 1) % NEO_COUNT;
}

void neoRainbow(uint8_t speed) {
  uint32_t now = millis();
  if (now - neoState.lastRainbow < map(255 - speed, 0, 255, 10, 200)) return;
  neoState.lastRainbow = now;
  for (uint8_t i = 0; i < NEO_COUNT; i++) {
    uint16_t hue = neoState.rainbowHue + (i * 65536UL / NEO_COUNT);
    neo.setPixelColor(i, neo.gamma32(neo.ColorHSV(hue)));
  }
  neo.show();
  neoState.rainbowHue += 512;
}

void neoStrobe(uint8_t r, uint8_t g, uint8_t b, uint8_t speed) {
  uint32_t now = millis();
  if (now - neoState.lastStrobe < map(255 - speed, 0, 255, 25, 2000)) return;
  neoState.lastStrobe = now;
  neoState.strobeOn = !neoState.strobeOn;
  neo.fill(neoState.strobeOn ? neo.Color(r, g, b) : 0);
  neo.show();
}

void neoFire() {
  uint32_t now = millis();
  if (now - neoState.lastFire < 60) return;
  neoState.lastFire = now;
  for (uint8_t i = 0; i < NEO_COUNT; i++) {
    int16_t heat = (int16_t)neoState.fireHeat[i] + random(-30, 45);
    neoState.fireHeat[i] = (uint8_t)constrain(heat, 80, 255);
    uint8_t h = neoState.fireHeat[i];
    uint8_t rv, gv;
    if (h < 128) { rv = h * 2; gv = 0; }
    else         { rv = 255;   gv = (h - 128) * 2; }
    neo.setPixelColor(i, neo.Color(rv, gv, 0));
  }
  neo.show();
}

void neoTheaterChase(uint8_t r, uint8_t g, uint8_t b, uint8_t speed) {
  uint32_t now = millis();
  if (now - neoState.lastTheater < map(255 - speed, 0, 255, 20, 400)) return;
  neoState.lastTheater = now;
  neo.clear();
  for (uint8_t i = neoState.theaterStep; i < NEO_COUNT; i += 3)
    neo.setPixelColor(i, neo.Color(r, g, b));
  neo.show();
  neoState.theaterStep = (neoState.theaterStep + 1) % 3;
}

void neoSparkle(uint8_t r, uint8_t g, uint8_t b, uint8_t speed) {
  uint32_t now = millis();
  if (now - neoState.lastSparkle < map(255 - speed, 0, 255, 20, 300)) return;
  neoState.lastSparkle = now;
  neo.clear();
  for (uint8_t i = 0; i < random(1, 5); i++)
    neo.setPixelColor(random(NEO_COUNT), neo.Color(r, g, b));
  neo.show();
}

void neoColorCycle(uint8_t speed) {
  uint32_t now = millis();
  if (now - neoState.lastRainbow < map(255 - speed, 0, 255, 30, 600)) return;
  neoState.lastRainbow = now;
  neo.fill(neo.gamma32(neo.ColorHSV(neoState.rainbowHue)));
  neo.show();
  neoState.rainbowHue += 256;
}

void updateNeo() {
  if (!cfgMutex) return;
  bool    en, man;
  uint8_t fx, r, g, b, spd;
  xSemaphoreTake(cfgMutex, portMAX_DELAY);
  en  = cfg.neo_enabled;
  man = cfg.neo_manual;
  fx  = cfg.neo_effect;
  r   = cfg.neo_r;
  g   = cfg.neo_g;
  b   = cfg.neo_b;
  spd = cfg.neo_speed;
  xSemaphoreGive(cfgMutex);

  if (!en) { neo.clear(); neo.show(); return; }

  if (man) {
    float breathSpd = 0.005f + (spd / 255.0f) * 0.055f;
    switch (fx) {
      case FX_STATIC:
        if (millis() - neoState.lastStatic > 100) {
          neoState.lastStatic = millis();
          neo.fill(neo.Color(r, g, b)); neo.show();
        }
        break;
      case FX_BREATH:     neoBreath(r, g, b, breathSpd);     break;
      case FX_SPIN:       neoSpinManual(r, g, b, spd);       break;
      case FX_RAINBOW:    neoRainbow(spd);                    break;
      case FX_STROBE:     neoStrobe(r, g, b, spd);           break;
      case FX_FIRE:       neoFire();                          break;
      case FX_THEATER:    neoTheaterChase(r, g, b, spd);     break;
      case FX_SPARKLE:    neoSparkle(r, g, b, spd);           break;
      case FX_COLORCYCLE: neoColorCycle(spd);                 break;
      default: break;
    }
    return;
  }

  if (!wifiState.connected)              { neoSpinner(neo.Color(255, 0, 0));  return; }
  if (!sensors.ahtOk || !sensors.ensOk) { neoSpinner(neo.Color(255, 200, 0));  return; }
  if (!sensors.ready)                    { neoSpinner(neo.Color(255, 255, 255)); return; }
  if (sensors.pm2_5 > 55)               { neoFlash(255, 30, 0); return; }
  switch (sensors.aqi) {
    case 1: neoBreath(0,   255, 0,   0.015f); break;
    case 2: neoBreath(0,   255, 200, 0.020f); break;
    case 3: neoBreath(255, 200, 0,   0.030f); break;
    case 4: neoBreath(255, 80,  0,   0.045f); break;
    case 5: neoFlash(255, 0, 0);              break;
    default:neoBreath(100, 100, 100, 0.010f); break;
  }
}

void neoTaskFn(void* param) {
  for (;;) {
    updateNeo();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

uint8_t collectErrors(uint8_t* errors) {
  uint8_t count = 0;
  if (!wifiState.connected) errors[count++] = 2;
  if (!sensors.ahtOk)       errors[count++] = 3;
  if (!sensors.ensOk)       errors[count++] = 4;
  if (!sensors.bmpOk)       errors[count++] = 5;
  if (!sensors.pmsOk)       errors[count++] = 6;
  return count;
}

void updateLed() {
  uint32_t now = millis();
  uint8_t errors[5];
  uint8_t errorCount = collectErrors(errors);

  if (errorCount == 0) {
    uint32_t cycle = now % LED_HEARTBEAT_MS;
    bool on = (cycle < 80) || (cycle > 150 && cycle < 230);
    digitalWrite(PIN_LED, on ? HIGH : LOW);
    return;
  }

  if (led.currentErrorIdx >= errorCount) led.currentErrorIdx = 0;
  uint8_t  err       = errors[led.currentErrorIdx];
  uint32_t cycleLen  = (err * (LED_FLASH_ON_MS + LED_FLASH_OFF_MS)) + LED_ERROR_GAP_MS;
  uint32_t cyclePos  = now - led.lastCycleStart;

  if (cyclePos >= cycleLen) {
    led.lastCycleStart  = now;
    led.currentErrorIdx = (led.currentErrorIdx + 1) % errorCount;
    digitalWrite(PIN_LED, LOW);
    return;
  }

  uint32_t flashSection = err * (LED_FLASH_ON_MS + LED_FLASH_OFF_MS);
  if (cyclePos < flashSection) {
    uint32_t flashPos = cyclePos % (LED_FLASH_ON_MS + LED_FLASH_OFF_MS);
    digitalWrite(PIN_LED, flashPos < LED_FLASH_ON_MS ? HIGH : LOW);
  } else {
    digitalWrite(PIN_LED, LOW);
  }
}

void wifiConnect() {
  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  if (rtcValid && rtcIp != 0) {
    IPAddress ip(rtcIp), gateway(rtcGateway), subnet(rtcSubnet), dns(rtcGateway);
    WiFi.config(ip, gateway, subnet, dns);
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_password, rtcChannel, rtcBssid, true);
  } else {
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
      }
    }
    return;
  }
  wifiState.connected = false;
  if (rtcValid && millis() - wifiState.lastAttemptMs > 5000) {
    rtcValid = false; rtcIp = 0;
  }
  if (millis() - wifiState.lastAttemptMs > 30000) {
    WiFi.disconnect();
    wifiConnect();
  }
}

void drawStaticLayout() {
  dispCache = {};
  tft.fillScreen(C_BG);
  tft.fillRect(0, 0, 240, 18, C_SURFACE);
  tft.setTextColor(C_HEADER, C_SURFACE);
  tft.setTextSize(1);
  tft.setCursor(5, 5);
  tft.print("HOMELAB AIR MONITOR");
  tft.drawFastHLine(0, 18, 240, C_HEADER);
  tft.setTextColor(C_LABEL, C_BG);
  tft.setCursor(5,   25); tft.print("TEMP");
  tft.setCursor(130, 25); tft.print("HUMIDITY");
  tft.drawFastHLine(0, 75, 240, C_DIVIDER);
  tft.setCursor(5,   82); tft.print("eCO2 (ppm)");
  tft.setCursor(130, 82); tft.print("TVOC (ppb)");
  tft.drawFastHLine(0, 135, 240, C_DIVIDER);
  tft.setCursor(5,   142); tft.print("AQI");
  tft.setCursor(80,  142); tft.print("PRESSURE");
  tft.drawFastHLine(0, 195, 240, C_DIVIDER);
  tft.setCursor(5,   200); tft.print("PM1.0");
  tft.setCursor(85,  200); tft.print("PM2.5");
  tft.setCursor(165, 200); tft.print("PM10");
  tft.drawFastHLine(0, 225, 240, C_DIVIDER);
}

void updateDisplay() {
  // Temperature
  if (sensors.temperature != dispCache.temperature) {
    dispCache.temperature = sensors.temperature;
    tft.fillRect(5, 35, 120, 35, C_BG);
    tft.setTextColor(C_TEMP, C_BG); tft.setTextSize(3);
    tft.setCursor(5, 35);
    tft.print(sensors.temperature, 1); tft.print("C");
  }

  // Humidity
  if (sensors.humidity != dispCache.humidity) {
    dispCache.humidity = sensors.humidity;
    tft.fillRect(130, 35, 110, 35, C_BG);
    tft.setTextColor(C_HUMID, C_BG); tft.setTextSize(3);
    tft.setCursor(130, 35);
    tft.print(sensors.humidity, 1); tft.print("%");
  }

  // eCO2
  if (sensors.eco2 != dispCache.eco2) {
    dispCache.eco2 = sensors.eco2;
    tft.fillRect(5, 92, 120, 35, C_BG);
    tft.setTextColor(co2Color(sensors.eco2), C_BG); tft.setTextSize(3);
    tft.setCursor(5, 92);
    tft.print(sensors.eco2);
  }

  // TVOC
  if (sensors.tvoc != dispCache.tvoc) {
    dispCache.tvoc = sensors.tvoc;
    tft.fillRect(130, 92, 110, 35, C_BG);
    tft.setTextColor(C_TVOC, C_BG); tft.setTextSize(3);
    tft.setCursor(130, 92);
    tft.print(sensors.tvoc);
  }

  // AQI
  if (sensors.aqi != dispCache.aqi || sensors.ready != dispCache.ready) {
    dispCache.aqi   = sensors.aqi;
    dispCache.ready = sensors.ready;
    tft.fillRect(5, 152, 70, 40, C_BG);
    if (sensors.ready) {
      tft.setTextColor(aqiColor(sensors.aqi), C_BG); tft.setTextSize(3);
      tft.setCursor(5, 152); tft.print(sensors.aqi);
      tft.setTextSize(1); tft.setCursor(5, 178);
      tft.print(aqiLabel(sensors.aqi));
    } else {
      tft.setTextColor(C_WARN, C_BG); tft.setTextSize(1);
      tft.setCursor(5, 152);
      uint32_t elapsed  = millis();
      uint32_t secsLeft = elapsed < cfg.warmup_ms ? (cfg.warmup_ms - elapsed) / 1000 : 0;
      tft.printf("Warmup\n%lus", secsLeft);
    }
  }

  // Pressure + altitude
  if (sensors.pressure_hPa != dispCache.pressure_hPa || sensors.altitude_m != dispCache.altitude_m) {
    dispCache.pressure_hPa = sensors.pressure_hPa;
    dispCache.altitude_m   = sensors.altitude_m;
    tft.fillRect(80, 152, 160, 40, C_BG);
    tft.setTextColor(C_PRESSURE, C_BG); tft.setTextSize(2);
    tft.setCursor(80, 152);
    tft.print(sensors.pressure_hPa, 1); tft.print("hPa");
    tft.setTextColor(C_DIM, C_BG); tft.setTextSize(1);
    tft.setCursor(80, 178);
    tft.printf("%.0fm alt", sensors.altitude_m);
  }

  // PM values
  if (sensors.pm1_0 != dispCache.pm1_0 || sensors.pm2_5 != dispCache.pm2_5 || sensors.pm10 != dispCache.pm10) {
    dispCache.pm1_0 = sensors.pm1_0;
    dispCache.pm2_5 = sensors.pm2_5;
    dispCache.pm10  = sensors.pm10;
    tft.fillRect(5, 210, 235, 14, C_BG);
    tft.setTextSize(2);
    tft.setTextColor(pmColor(sensors.pm1_0), C_BG); tft.setCursor(5,   210); tft.print(sensors.pm1_0);
    tft.setTextColor(pmColor(sensors.pm2_5), C_BG); tft.setCursor(85,  210); tft.print(sensors.pm2_5);
    tft.setTextColor(pmColor(sensors.pm10),  C_BG); tft.setCursor(165, 210); tft.print(sensors.pm10);
  }

  tft.fillRect(5, 228, 235, 12, C_BG);
  tft.setTextColor(C_DIM, C_BG); tft.setTextSize(1);
  tft.setCursor(5, 229);
  tft.printf("CPU:%.0fC  ", sensors.chipTemp);
  if (wifiState.connected) {
    tft.print(WiFi.localIP());
    if (rtcValid) { tft.setTextColor(C_AQI_1, C_BG); tft.print(" [F]"); }
  } else {
    tft.print("WiFi reconnecting...");
  }

  // LED bar
  int bx = 5, by = 239, bw = 120, bh = 6;
  tft.fillRect(bx, by, bw, bh, C_BG);
  tft.drawRect(bx, by, bw, bh, C_DIM);
  int fillW = (cfg.neo_brightness * (bw - 2)) / 255;
  uint16_t fillCol = cfg.neo_enabled ? (cfg.neo_manual ? C_AQI_2 : C_AQI_1) : C_CO2_UGLY;
  if (fillW > 0) tft.fillRect(bx + 1, by + 1, fillW, bh - 2, fillCol);
  tft.setTextColor(C_DIM, C_BG);
  tft.setCursor(bx + bw + 8, by);
  if (!cfg.neo_enabled)    tft.print("LED:off");
  else if (cfg.neo_manual) tft.printf("LED:M%d", cfg.neo_brightness);
  else                     tft.printf("LED:%d",  cfg.neo_brightness);
}

void wsBroadcast() {
  if (!wifiState.connected) return;
  if (wsServer.connectedClients() == 0) return;
  
  JsonDocument doc; 
  doc["timestamp"] = millis();
  
  JsonObject ens = doc["ens160"].to<JsonObject>();
  ens["aqi"]  = sensors.aqi;
  ens["eco2"] = sensors.eco2;
  ens["tvoc"] = sensors.tvoc;
  
  JsonObject aht = doc["aht21"].to<JsonObject>();
  aht["temperature"] = sensors.temperature;
  aht["humidity"]    = sensors.humidity;
  
  JsonObject pms_obj = doc["pms5003"].to<JsonObject>();
  pms_obj["pm1_0"] = sensors.pm1_0;
  pms_obj["pm2_5"] = sensors.pm2_5;
  pms_obj["pm10"]  = sensors.pm10;
  
  JsonObject bmp_obj = doc["bmp580"].to<JsonObject>();
  bmp_obj["pressure"] = sensors.pressure_hPa;
  bmp_obj["altitude"] = sensors.altitude_m;
  
  doc["ready"]    = sensors.ready;
  doc["uptime_s"] = millis() / 1000;
  
  String output;
  serializeJson(doc, output);
  wsServer.broadcastTXT(output);
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.printf("[WS] Client #%u connected\n", num);
    wsBroadcast();
  } else if (type == WStype_DISCONNECTED) {
    Serial.printf("[WS] Client #%u disconnected\n", num);
  }
}

void handleAirData() {
  static uint32_t lastRequest = 0;
  uint32_t now = millis();
  if (now - lastRequest < 1000) { server->send(429, "text/plain", "Too Many Requests"); return; }
  lastRequest = now;


  JsonDocument doc;
  char buf[1024];
  
  doc["temperature"]  = sensors.temperature;
  doc["humidity"]     = sensors.humidity;
  doc["eco2"]         = sensors.eco2;
  doc["tvoc"]         = sensors.tvoc;
  doc["aqi"]          = sensors.aqi;
  doc["aqi_label"]    = aqiLabel(sensors.aqi);
  doc["pressure_hpa"] = sensors.pressure_hPa;
  doc["altitude_m"]   = sensors.altitude_m;
  doc["bmp_temp"]     = sensors.bmp_temp;
  doc["pm1_0"]        = sensors.pm1_0;
  doc["pm2_5"]        = sensors.pm2_5;
  doc["pm10"]         = sensors.pm10;
  doc["chip_temp"]    = sensors.chipTemp;
  doc["ready"]        = sensors.ready;
  doc["uptime_s"]     = millis() / 1000;
  doc["fast_wifi"]    = rtcValid;

  serializeJson(doc, buf, sizeof(buf));
  server->sendHeader("Access-Control-Allow-Origin", "*");
  server->sendHeader("Cache-Control", "no-cache");
  server->send(200, "application/json", buf);
}

const char* API_TOKEN = "MySuperSecretToken123!"; // Here generate your own bearer-token for api security!

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

void handleNeoSet() {
  if (!checkAuth()) return;
  if (!server->hasArg("plain")) { server->send(400, "text/plain", "Missing JSON body"); return; }
  JsonDocument doc;
  if (deserializeJson(doc, server->arg("plain"))) { server->send(400, "text/plain", "Invalid JSON"); return; }

  xSemaphoreTake(cfgMutex, portMAX_DELAY);
  if (doc["enabled"].is<bool>())    cfg.neo_enabled = doc["enabled"];
  if (doc["brightness"].is<int>()) { cfg.neo_brightness = doc["brightness"].as<uint8_t>(); neo.setBrightness(cfg.neo_brightness); }
  if (doc["manual"].is<bool>())     cfg.neo_manual  = doc["manual"];
  if (doc["effect"].is<int>()) {
    uint8_t newFx = min(doc["effect"].as<uint8_t>(), FX_COLORCYCLE);
    if (newFx != cfg.neo_effect) {
      cfg.neo_effect = newFx;
      neoState.spinPos = 0; neoState.rainbowHue = 0;
      neoState.theaterStep = 0; neoState.strobeOn = false;
      neoState.breathVal = 0.0f; neoState.breathDir = 1.0f;
    }
  }
  if (doc["r"].is<int>())     cfg.neo_r     = doc["r"].as<uint8_t>();
  if (doc["g"].is<int>())     cfg.neo_g     = doc["g"].as<uint8_t>();
  if (doc["b"].is<int>())     cfg.neo_b     = doc["b"].as<uint8_t>();
  if (doc["speed"].is<int>()) cfg.neo_speed = doc["speed"].as<uint8_t>();
  xSemaphoreGive(cfgMutex); 

  saveConfig();
  server->sendHeader("Access-Control-Allow-Origin", "*");
  server->send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleConfigSet() {
  if (!checkAuth()) return;
  if (!server->hasArg("plain")) { server->send(400, "text/plain", "Missing JSON body"); return; }
  JsonDocument doc;
  if (deserializeJson(doc, server->arg("plain"))) { server->send(400, "text/plain", "Invalid JSON"); return; }

  bool wifiChanged = false;
  if (doc["wifi_ssid"].is<const char*>())    { strlcpy(cfg.wifi_ssid,     doc["wifi_ssid"],     sizeof(cfg.wifi_ssid));     wifiChanged = true; }
  if (doc["wifi_password"].is<const char*>()){ strlcpy(cfg.wifi_password, doc["wifi_password"],  sizeof(cfg.wifi_password)); wifiChanged = true; }
  if (doc["server_port"].is<uint16_t>()) {
    uint16_t p = doc["server_port"].as<uint16_t>();
    if (p >= 1024) cfg.server_port = p;
  }
  if (doc["temp_offset"].is<float>())        cfg.temp_offset     = doc["temp_offset"];
  if (doc["hum_offset"].is<float>())         cfg.hum_offset      = doc["hum_offset"];
  if (doc["warmup_ms"].is<uint32_t>())       cfg.warmup_ms       = doc["warmup_ms"];
  if (doc["sensor_interval"].is<uint32_t>()) cfg.sensor_interval = max((uint32_t)500, doc["sensor_interval"].as<uint32_t>());
  if (doc["neo_brightness"].is<int>()) {
    xSemaphoreTake(cfgMutex, portMAX_DELAY);
    cfg.neo_brightness = doc["neo_brightness"];
    neo.setBrightness(cfg.neo_brightness);
    xSemaphoreGive(cfgMutex);
  }
  if (doc["neo_enabled"].is<bool>()) cfg.neo_enabled = doc["neo_enabled"];

  saveConfig();
  if (wifiChanged) {
    rtcValid = false; rtcIp = 0;
    server->sendHeader("Access-Control-Allow-Origin", "*");
    server->send(200, "application/json", "{\"status\":\"saved\",\"note\":\"WiFi changed — reconnecting\"}");
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

void handleRoot() {
  server->sendHeader("Access-Control-Allow-Origin", "*");
  server->sendHeader("Cache-Control", "no-cache");
  server->send(200, "text/html", INDEX_HTML);
}

void handleNotFound() {
  server->send(404, "text/plain", "Not found");
}

void readSensors() {
  if (sensors.ahtOk) {
    sensors_event_t hum, temp;
    aht.getEvent(&hum, &temp);
    sensors.temperature = temp.temperature + cfg.temp_offset;
    sensors.humidity    = constrain(hum.relative_humidity + cfg.hum_offset, 0.0f, 100.0f);
  }
  if (sensors.ensOk) {
    ens160.set_envdata(sensors.temperature, sensors.humidity);
    if (ens160.available()) {
      ens160.measure(true);
      sensors.eco2 = ens160.geteCO2();
      sensors.tvoc = ens160.getTVOC();
      sensors.aqi  = ens160.getAQI();
    }
  }
  if (sensors.bmpOk) {
    sensors.pressure_hPa = bmp.readPressure() / 100.0f;
    sensors.bmp_temp     = bmp.readTemperature();
    sensors.altitude_m   = bmp.readAltitude(1013.25f);
  }
  if (sensors.pmsOk && pms.read(pmsData)) {
    sensors.pm1_0 = pmsData.PM_AE_UG_1_0;
    sensors.pm2_5 = pmsData.PM_AE_UG_2_5;
    sensors.pm10  = pmsData.PM_AE_UG_10_0;
  }
  sensors.chipTemp = (float)temperatureRead();
  sensors.ready    = (millis() >= cfg.warmup_ms);
}

void setup() {
  Serial.begin(115200);
  
  cfgMutex = xSemaphoreCreateMutex();
  if (cfgMutex == NULL) {
    Serial.println("CRITICAL ERROR: Mutex creation failed!");
    while(1); // Halt everything if this fails
  }
  
  esp_task_wdt_init(WDT_TIMEOUT_S, true);
  esp_task_wdt_add(NULL);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  loadConfig();

  neo.begin();
  neo.setBrightness(cfg.neo_brightness);
  neo.clear();
  neo.show();

  tft.init();
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, HIGH);
  tft.invertDisplay(true);
  tft.setRotation(0);
  tft.fillScreen(C_BG);
  tft.setTextColor(C_HEADER, C_BG);
  tft.setTextSize(2);
  tft.setCursor(10, 40); tft.println("Homelab Air");
  tft.setCursor(10, 65); tft.println("Monitor v1");
  tft.setTextSize(1);

  tft.setTextColor(C_LABEL, C_BG);
  tft.setCursor(10, 100);
  tft.println(rtcValid ? "WiFi fast connect..." : "Connecting WiFi...");
  wifiConnect();
  uint32_t wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
    delay(100);
    esp_task_wdt_reset();
    updateLed();
    neoSpinner(neo.Color(0, 255, 255));
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiState.connected = true;
    if (!rtcValid) {
      memcpy(rtcBssid, WiFi.BSSID(), 6);
      rtcChannel = WiFi.channel();
      rtcIp      = (uint32_t)WiFi.localIP();
      rtcGateway = (uint32_t)WiFi.gatewayIP();
      rtcSubnet  = (uint32_t)WiFi.subnetMask();
      rtcValid   = true;
    }
    tft.setTextColor(C_AQI_1, C_BG);
    tft.setCursor(10, 115);
    tft.println(rtcValid ? "WiFi OK! [fast]" : "WiFi OK!");
    tft.setTextColor(C_DIM, C_BG);
    tft.setCursor(10, 128);
    tft.print(WiFi.localIP());
    Serial.println("WiFi: " + WiFi.localIP().toString());
  } else {
    tft.setTextColor(C_CO2_UGLY, C_BG);
    tft.setCursor(10, 115);
    tft.println("WiFi failed - will retry");
    rtcValid = false; rtcIp = 0;
  }

  tft.setCursor(10, 148);
  if (aht.begin()) { sensors.ahtOk = true; tft.setTextColor(C_AQI_1, C_BG); tft.println("AHT21 OK"); }
  else             { tft.setTextColor(C_CO2_UGLY, C_BG); tft.println("AHT21 ERROR"); }

  tft.setCursor(10, 163);
  ens160.begin();
  if (ens160.available()) {
    ens160.setMode(ENS160_OPMODE_STD);
    sensors.ensOk = true;
    tft.setTextColor(C_AQI_1, C_BG); tft.println("ENS160 OK");
  } else {
    tft.setTextColor(C_CO2_UGLY, C_BG); tft.println("ENS160 ERROR");
  }

  tft.setCursor(10, 178);
  if (bmp.begin(ADDR_BMP580, &Wire)) { sensors.bmpOk = true; tft.setTextColor(C_AQI_1, C_BG); tft.println("BMP580 OK"); }
  else                               { tft.setTextColor(C_CO2_UGLY, C_BG); tft.println("BMP580 ERROR"); }

  tft.setCursor(10, 193);
  pmsSerial.begin(9600, SERIAL_8N1, PIN_PMS_RX, PIN_PMS_TX);
  pms.passiveMode();
  delay(100);
  pms.wakeUp();
  sensors.pmsOk = true;
  tft.setTextColor(C_AQI_1, C_BG); tft.println("PMS5003 OK");
  
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

  wsServer.begin();
  wsServer.onEvent(onWebSocketEvent);

  tft.setTextColor(C_AQI_1, C_BG);
  tft.setCursor(10, 208);
  tft.println("Ready!");
  delay(1500);

  xTaskCreatePinnedToCore(neoTaskFn, "NeoPixel", 2048, NULL, 2, &neoTaskHandle, 1);
  drawStaticLayout();
}

void loop() {
  esp_task_wdt_reset();
  updateLed();
  if (server) server->handleClient();
  wsServer.loop();
  wifiCheck();

  static uint32_t lastSensorRead  = 0;
  static uint32_t lastDisplayUpd  = 0;
  static uint32_t lastWsBroadcast = 0;
  uint32_t now = millis();
  
  if (now - lastSensorRead >= cfg.sensor_interval) {
    lastSensorRead = now;
    readSensors();
    Serial.printf("[%lus] T:%.1fC H:%.1f%% CO2:%d TVOC:%d AQI:%d P:%.1fhPa PM2.5:%d CPU:%.0fC heap:%u\n",
      now / 1000, sensors.temperature, sensors.humidity,
      sensors.eco2, sensors.tvoc, sensors.aqi,
      sensors.pressure_hPa, sensors.pm2_5, sensors.chipTemp,
      ESP.getFreeHeap());
  }

  
  if (now - lastDisplayUpd >= 500) {
    lastDisplayUpd = now;
    updateDisplay();
  }

  
  if (now - lastWsBroadcast >= cfg.sensor_interval) {
    lastWsBroadcast = now;
    wsBroadcast();
  }
}