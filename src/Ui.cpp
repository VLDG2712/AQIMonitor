// Ui.cpp — LVGL screen construction and updates.
//
// Layout: a persistent header and footer bracket a content area that cycles
// through three pages. There is no touch controller and no free GPIO for a
// button, so pages advance on a timer; status that must always be visible
// (WiFi, IP, LED state) lives in the header/footer rather than on a page.
#include "Ui.h"

#include <lvgl.h>
#include <WiFi.h>

#include "Config.h"
#include "Constants.h"
#include "Display.h"
#include "Globals.h"

// ---------------------------------------------------------------------------
// LVGL <-> TFT_eSPI plumbing
// ---------------------------------------------------------------------------

static constexpr int SCR_W = 240;
static constexpr int SCR_H = 320;

// Partial render buffer: 40 rows is large enough that most widget redraws
// finish in one flush. Allocated on the heap rather than statically — the
// ESP32's dram0_0_seg cannot take 19KB of extra .bss on top of everything
// else, but there is ample heap.
static constexpr int BUF_ROWS = 40;
static constexpr size_t BUF_BYTES = SCR_W * BUF_ROWS * sizeof(lv_color16_t);
static uint8_t* drawBuf = nullptr;

static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  // No byte swap here. Verified on the panel: passing true rendered cyan
  // (0x07FF) as yellow (0xFF07), the signature of swapping twice.
  tft.pushColors((uint16_t*)px_map, w * h, false);
  tft.endWrite();

  lv_display_flush_ready(disp);
}

// LVGL 9 pulls the tick from a callback rather than needing lv_tick_inc().
static uint32_t tickCb() { return millis(); }

// ---------------------------------------------------------------------------
// Palette — 24-bit equivalents of the RGB565 constants in Constants.h, so the
// redesign keeps the same colour language as the old TFT layout.
// ---------------------------------------------------------------------------

static constexpr uint32_t COL_BG      = 0x000000;
static constexpr uint32_t COL_SURFACE = 0x08080C;
static constexpr uint32_t COL_ACCENT  = 0x00FFFF;  // C_HEADER
static constexpr uint32_t COL_LABEL   = 0x7B7D7B;  // C_LABEL
static constexpr uint32_t COL_DIM     = 0x39393B;  // C_DIM
static constexpr uint32_t COL_TEMP    = 0xFFA400;  // C_TEMP
static constexpr uint32_t COL_HUMID   = 0x00FFFF;  // C_HUMID
static constexpr uint32_t COL_TVOC    = 0xFF00FF;  // C_TVOC
static constexpr uint32_t COL_PRESS   = 0xFF00FF;  // C_PRESSURE
static constexpr uint32_t COL_GOOD    = 0x00FF00;
static constexpr uint32_t COL_MED     = 0xFFFF00;
static constexpr uint32_t COL_BAD     = 0xFFA400;
static constexpr uint32_t COL_UGLY    = 0xFF0000;

static uint32_t aqiColour(uint8_t aqi) {
  switch (aqi) {
    case 1:  return COL_GOOD;
    case 2:  return COL_ACCENT;
    case 3:  return COL_MED;
    case 4:  return COL_BAD;
    case 5:  return COL_UGLY;
    default: return COL_LABEL;
  }
}

static uint32_t co2Colour(uint16_t co2) {
  if (co2 < 600)  return COL_GOOD;
  if (co2 < 1000) return COL_MED;
  if (co2 < 1500) return COL_BAD;
  return COL_UGLY;
}

static uint32_t pmColour(uint16_t pm25) {
  if (pm25 < 12) return COL_GOOD;
  if (pm25 < 35) return COL_MED;
  if (pm25 < 55) return COL_BAD;
  return COL_UGLY;
}

// ---------------------------------------------------------------------------
// Widget handles
// ---------------------------------------------------------------------------

static constexpr int PAGE_COUNT = 3;
// Long enough to read a page without feeling stuck on it.
static constexpr uint32_t PAGE_MS = 8000;

static lv_obj_t* pages[PAGE_COUNT];
static lv_obj_t* dots[PAGE_COUNT];
static int activePage = 0;

// Header / footer
static lv_obj_t* lblNet;
static lv_obj_t* lblCpu;
static lv_obj_t* barLed;
static lv_obj_t* lblLed;

// Page 1 — air quality
static lv_obj_t* arcAqi;
static lv_obj_t* lblAqiValue;
static lv_obj_t* lblAqiText;
static lv_obj_t* lblCo2;
static lv_obj_t* lblTvoc;

// Page 2 — climate
static lv_obj_t* lblTemp;
static lv_obj_t* lblHum;
static lv_obj_t* lblPress;
static lv_obj_t* lblAlt;

// Page 3 — particulate
static lv_obj_t* lblPm1;
static lv_obj_t* lblPm25;
static lv_obj_t* lblPm10;
static lv_obj_t* chartPm;
static lv_chart_series_t* seriesPm;

// ---------------------------------------------------------------------------
// Construction helpers
// ---------------------------------------------------------------------------

/// A borderless, padding-free container — LVGL's defaults add both.
static lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h,
                       uint32_t bg = COL_BG) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(bg), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(o, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static lv_obj_t* label(lv_obj_t* parent, int x, int y, const char* text,
                       uint32_t colour, const lv_font_t* font) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_color(l, lv_color_hex(colour), LV_PART_MAIN);
  lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
  return l;
}

static void buildHeader(lv_obj_t* scr) {
  lv_obj_t* bar = panel(scr, 0, 0, SCR_W, 26, COL_SURFACE);
  label(bar, 6, 5, "HEXAIR", COL_ACCENT, &lv_font_montserrat_16);
  lblNet = label(bar, 0, 7, "connecting", COL_LABEL, &lv_font_montserrat_12);
  lv_obj_align(lblNet, LV_ALIGN_RIGHT_MID, -6, 0);

  panel(scr, 0, 26, SCR_W, 1, COL_ACCENT);
}

static void buildFooter(lv_obj_t* scr) {
  panel(scr, 0, 286, SCR_W, 1, COL_DIM);

  lblCpu = label(scr, 6, 292, "CPU --C", COL_DIM, &lv_font_montserrat_12);

  // LED brightness bar, mirroring the strip the old layout drew by hand.
  barLed = lv_bar_create(scr);
  lv_obj_set_pos(barLed, 6, 310);
  lv_obj_set_size(barLed, 110, 5);
  lv_bar_set_range(barLed, 0, 255);
  lv_obj_set_style_bg_color(barLed, lv_color_hex(COL_DIM), LV_PART_MAIN);
  lv_obj_set_style_bg_color(barLed, lv_color_hex(COL_GOOD), LV_PART_INDICATOR);
  lv_obj_set_style_radius(barLed, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(barLed, 0, LV_PART_INDICATOR);

  lblLed = label(scr, 122, 305, "LED", COL_DIM, &lv_font_montserrat_12);

  // Page indicator dots, so it is obvious the view rotates.
  for (int i = 0; i < PAGE_COUNT; i++) {
    dots[i] = panel(scr, SCR_W - 10 - (PAGE_COUNT - 1 - i) * 12, 296, 6, 6,
                    COL_DIM);
    lv_obj_set_style_radius(dots[i], 3, LV_PART_MAIN);
  }
}

static void buildPageAir(lv_obj_t* parent) {
  label(parent, 6, 2, "AIR QUALITY", COL_LABEL, &lv_font_montserrat_12);

  arcAqi = lv_arc_create(parent);
  lv_obj_set_size(arcAqi, 150, 150);
  lv_obj_align(arcAqi, LV_ALIGN_TOP_MID, 0, 18);
  lv_arc_set_rotation(arcAqi, 135);
  lv_arc_set_bg_angles(arcAqi, 0, 270);
  lv_arc_set_range(arcAqi, 0, 5);
  lv_arc_set_value(arcAqi, 0);
  lv_obj_remove_style(arcAqi, nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(arcAqi, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(arcAqi, 12, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arcAqi, 12, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arcAqi, lv_color_hex(COL_DIM), LV_PART_MAIN);

  lblAqiValue = label(parent, 0, 0, "-", COL_LABEL, &lv_font_montserrat_36);
  lv_obj_align(lblAqiValue, LV_ALIGN_TOP_MID, 0, 62);
  lblAqiText = label(parent, 0, 0, "Warmup", COL_LABEL, &lv_font_montserrat_14);
  lv_obj_align(lblAqiText, LV_ALIGN_TOP_MID, 0, 112);

  label(parent, 12, 182, "eCO2", COL_LABEL, &lv_font_montserrat_12);
  lblCo2 = label(parent, 12, 196, "---", COL_GOOD, &lv_font_montserrat_28);
  label(parent, 12, 232, "ppm", COL_DIM, &lv_font_montserrat_12);

  label(parent, 132, 182, "TVOC", COL_LABEL, &lv_font_montserrat_12);
  lblTvoc = label(parent, 132, 196, "---", COL_TVOC, &lv_font_montserrat_28);
  label(parent, 132, 232, "ppb", COL_DIM, &lv_font_montserrat_12);
}

static void buildPageClimate(lv_obj_t* parent) {
  label(parent, 6, 2, "CLIMATE", COL_LABEL, &lv_font_montserrat_12);

  label(parent, 12, 28, "TEMPERATURE", COL_LABEL, &lv_font_montserrat_12);
  lblTemp = label(parent, 12, 44, "--.-", COL_TEMP, &lv_font_montserrat_36);

  label(parent, 12, 104, "HUMIDITY", COL_LABEL, &lv_font_montserrat_12);
  lblHum = label(parent, 12, 120, "--.-", COL_HUMID, &lv_font_montserrat_36);

  label(parent, 12, 180, "PRESSURE", COL_LABEL, &lv_font_montserrat_12);
  lblPress = label(parent, 12, 196, "----.-", COL_PRESS, &lv_font_montserrat_28);
  lblAlt = label(parent, 12, 232, "-- m", COL_DIM, &lv_font_montserrat_14);
}

static void buildPageParticulate(lv_obj_t* parent) {
  label(parent, 6, 2, "PARTICULATE", COL_LABEL, &lv_font_montserrat_12);

  label(parent, 12, 26, "PM1.0", COL_LABEL, &lv_font_montserrat_12);
  lblPm1 = label(parent, 12, 40, "--", COL_GOOD, &lv_font_montserrat_28);

  label(parent, 92, 26, "PM2.5", COL_LABEL, &lv_font_montserrat_12);
  lblPm25 = label(parent, 92, 40, "--", COL_GOOD, &lv_font_montserrat_28);

  label(parent, 168, 26, "PM10", COL_LABEL, &lv_font_montserrat_12);
  lblPm10 = label(parent, 168, 40, "--", COL_GOOD, &lv_font_montserrat_28);

  label(parent, 12, 86, "PM2.5 TREND", COL_LABEL, &lv_font_montserrat_12);

  // The sparkline fills the strip the old layout left empty below y=245.
  chartPm = lv_chart_create(parent);
  lv_obj_set_pos(chartPm, 12, 104);
  lv_obj_set_size(chartPm, 216, 140);
  lv_chart_set_type(chartPm, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(chartPm, 60);
  lv_chart_set_update_mode(chartPm, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_range(chartPm, LV_CHART_AXIS_PRIMARY_Y, 0, 60);
  lv_chart_set_div_line_count(chartPm, 3, 0);
  lv_obj_set_style_bg_color(chartPm, lv_color_hex(COL_BG), LV_PART_MAIN);
  lv_obj_set_style_border_color(chartPm, lv_color_hex(COL_DIM), LV_PART_MAIN);
  lv_obj_set_style_border_width(chartPm, 1, LV_PART_MAIN);
  lv_obj_set_style_line_color(chartPm, lv_color_hex(COL_DIM), LV_PART_ITEMS);
  // Line only, no per-point markers — at 60 points they merge into a band.
  lv_obj_set_style_width(chartPm, 0, LV_PART_INDICATOR);
  lv_obj_set_style_height(chartPm, 0, LV_PART_INDICATOR);
  seriesPm = lv_chart_add_series(chartPm, lv_color_hex(COL_ACCENT),
                                 LV_CHART_AXIS_PRIMARY_Y);
}

static void showPage(int idx) {
  for (int i = 0; i < PAGE_COUNT; i++) {
    if (i == idx) lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    else          lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(dots[i],
                              lv_color_hex(i == idx ? COL_ACCENT : COL_DIM),
                              LV_PART_MAIN);
  }
  activePage = idx;
}

static void pageTimerCb(lv_timer_t*) { showPage((activePage + 1) % PAGE_COUNT); }

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void uiInit() {
  lv_init();
  lv_tick_set_cb(tickCb);

  drawBuf = (uint8_t*)heap_caps_malloc(BUF_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  if (drawBuf == nullptr) {
    Serial.println("FATAL: LVGL draw buffer allocation failed");
    return;
  }
  Serial.printf("LVGL draw buffer: %u bytes, free heap now %u\n",
                (unsigned)BUF_BYTES, ESP.getFreeHeap());

  lv_display_t* disp = lv_display_create(SCR_W, SCR_H);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_set_buffers(disp, drawBuf, nullptr, BUF_BYTES,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  buildHeader(scr);
  buildFooter(scr);

  // Content area sits between the header rule and the footer rule.
  for (int i = 0; i < PAGE_COUNT; i++) pages[i] = panel(scr, 0, 28, SCR_W, 256);
  buildPageAir(pages[0]);
  buildPageClimate(pages[1]);
  buildPageParticulate(pages[2]);
  showPage(0);

  lv_timer_create(pageTimerCb, PAGE_MS, nullptr);

  // Confirms LVGL is blending in the same pixel format we push to the panel;
  // a mismatch shows up as correct solid colours but wrong anti-aliased edges.
  Serial.printf("LVGL color format: %d (RGB565=%d, RGB565_SWAPPED=%d)\n",
                (int)lv_display_get_color_format(disp),
                (int)LV_COLOR_FORMAT_RGB565,
                (int)LV_COLOR_FORMAT_RGB565_SWAPPED);
  Serial.printf("LVGL UI built, free heap now %u\n", ESP.getFreeHeap());
}

void uiTick() { lv_timer_handler(); }

void uiUpdate() {
  char buf[48];

  // --- header ---
  if (wifiState.connected) {
    snprintf(buf, sizeof(buf), "%s%s", WiFi.localIP().toString().c_str(),
             rtcValid ? " F" : "");
    lv_label_set_text(lblNet, buf);
    lv_obj_set_style_text_color(lblNet, lv_color_hex(COL_GOOD), LV_PART_MAIN);
  } else {
    lv_label_set_text(lblNet, "reconnecting");
    lv_obj_set_style_text_color(lblNet, lv_color_hex(COL_UGLY), LV_PART_MAIN);
  }
  lv_obj_align(lblNet, LV_ALIGN_RIGHT_MID, -6, 0);

  // --- page 1: air ---
  if (sensors.ready) {
    lv_arc_set_value(arcAqi, sensors.aqi);
    lv_obj_set_style_arc_color(arcAqi, lv_color_hex(aqiColour(sensors.aqi)),
                               LV_PART_INDICATOR);
    snprintf(buf, sizeof(buf), "%u", sensors.aqi);
    lv_label_set_text(lblAqiValue, buf);
    lv_obj_set_style_text_color(lblAqiValue,
                                lv_color_hex(aqiColour(sensors.aqi)),
                                LV_PART_MAIN);
    lv_label_set_text(lblAqiText, aqiLabel(sensors.aqi));
  } else {
    // Mirrors the old warm-up countdown rather than showing a misleading 0.
    uint32_t elapsed = millis();
    uint32_t secsLeft =
        elapsed < cfg.warmup_ms ? (cfg.warmup_ms - elapsed) / 1000 : 0;
    lv_arc_set_value(arcAqi, 0);
    lv_label_set_text(lblAqiValue, "--");
    snprintf(buf, sizeof(buf), "Warmup %lus", (unsigned long)secsLeft);
    lv_label_set_text(lblAqiText, buf);
  }
  lv_obj_align(lblAqiValue, LV_ALIGN_TOP_MID, 0, 62);
  lv_obj_align(lblAqiText, LV_ALIGN_TOP_MID, 0, 112);

  snprintf(buf, sizeof(buf), "%u", sensors.eco2);
  lv_label_set_text(lblCo2, buf);
  lv_obj_set_style_text_color(lblCo2, lv_color_hex(co2Colour(sensors.eco2)),
                              LV_PART_MAIN);
  snprintf(buf, sizeof(buf), "%u", sensors.tvoc);
  lv_label_set_text(lblTvoc, buf);

  // --- page 2: climate ---
  snprintf(buf, sizeof(buf), "%.1f C", sensors.temperature);
  lv_label_set_text(lblTemp, buf);
  snprintf(buf, sizeof(buf), "%.1f %%", sensors.humidity);
  lv_label_set_text(lblHum, buf);
  snprintf(buf, sizeof(buf), "%.1f", sensors.pressure_hPa);
  lv_label_set_text(lblPress, buf);
  snprintf(buf, sizeof(buf), "hPa  /  %.0f m", sensors.altitude_m);
  lv_label_set_text(lblAlt, buf);

  // --- page 3: particulate ---
  snprintf(buf, sizeof(buf), "%u", sensors.pm1_0);
  lv_label_set_text(lblPm1, buf);
  lv_obj_set_style_text_color(lblPm1, lv_color_hex(pmColour(sensors.pm1_0)),
                              LV_PART_MAIN);
  snprintf(buf, sizeof(buf), "%u", sensors.pm2_5);
  lv_label_set_text(lblPm25, buf);
  lv_obj_set_style_text_color(lblPm25, lv_color_hex(pmColour(sensors.pm2_5)),
                              LV_PART_MAIN);
  snprintf(buf, sizeof(buf), "%u", sensors.pm10);
  lv_label_set_text(lblPm10, buf);
  lv_obj_set_style_text_color(lblPm10, lv_color_hex(pmColour(sensors.pm10)),
                              LV_PART_MAIN);

  // Rescale the sparkline so spikes stay on-chart instead of clipping flat.
  int32_t top = sensors.pm2_5 > 55 ? ((sensors.pm2_5 / 50) + 1) * 50 : 60;
  lv_chart_set_range(chartPm, LV_CHART_AXIS_PRIMARY_Y, 0, top);
  lv_chart_set_next_value(chartPm, seriesPm, sensors.pm2_5);

  // --- footer ---
  snprintf(buf, sizeof(buf), "CPU %.0fC", sensors.chipTemp);
  lv_label_set_text(lblCpu, buf);

  lv_bar_set_value(barLed, cfg.neo_enabled ? cfg.neo_brightness : 0, LV_ANIM_OFF);
  uint32_t ledCol = !cfg.neo_enabled ? COL_UGLY
                                     : (cfg.neo_manual ? COL_ACCENT : COL_GOOD);
  lv_obj_set_style_bg_color(barLed, lv_color_hex(ledCol), LV_PART_INDICATOR);
  if (!cfg.neo_enabled)    lv_label_set_text(lblLed, "OFF");
  else if (cfg.neo_manual) lv_label_set_text(lblLed, "MAN");
  else                     lv_label_set_text(lblLed, "AUTO");
  lv_obj_set_style_text_color(lblLed, lv_color_hex(ledCol), LV_PART_MAIN);
}
