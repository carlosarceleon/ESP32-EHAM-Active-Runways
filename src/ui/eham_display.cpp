#include "ui/eham_display.h"

#include <lgfx/v1/lgfx_fonts.hpp>

#include <cmath>
#include <cstdio>

#include "data/eham_runways.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "ui/schiphol_theme.h"

namespace ui {
namespace {

// Normalized-geometry viewport. Source: archofthings/ha-schiphol-runway-card
// (MIT) — see docs/agent-instructions/ESP32_Schiphol_Runway_Display_Implementation_Plan.md.
constexpr int kMapLeft = 18;
constexpr int kMapTop = 20;
constexpr int kMapRight = 222;
constexpr int kMapBottom = 204;

constexpr int kFrameSize = 240;

constexpr float kBaseLineHalfWidth = 1.75f;      // ~3.5 px total base line
constexpr float kActivityLineHalfWidth = 1.5f;   // ~3 px total activity overlay
constexpr float kParallelOffsetPx = 4.0f;        // dual landing+departure offset
constexpr float kChevronHalfLenPx = 4.0f;
constexpr float kChevronHalfWidthPx = 3.0f;
constexpr float kLandingChevronT = 0.28f;         // first half of travel
constexpr float kDepartureChevronT = 0.72f;       // second half of travel
constexpr int kEndLabelGapPx = 9;
constexpr int kEndLabelHeightPx = 11;
constexpr int kTitleHeightPx = 20;
constexpr int kTitleTopY = 8;
constexpr int kWindArrowCenterX = kFrameSize / 2;
constexpr int kWindArrowCenterY = 47;
constexpr float kWindArrowMinLengthPx = 12.0f;
constexpr float kWindArrowMaxLengthPx = 30.0f;
constexpr uint16_t kBeaufort1MinKt = 1;
constexpr uint16_t kBeaufort5MaxKt = 21;

struct Vec2 {
  float x;
  float y;
};

Vec2 sub(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
Vec2 add(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
Vec2 scale(Vec2 v, float s) { return {v.x * s, v.y * s}; }
Vec2 lerp(Vec2 a, Vec2 b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }
float length(Vec2 v) { return sqrtf(v.x * v.x + v.y * v.y); }

Vec2 normalize(Vec2 v) {
  const float len = length(v);
  if (len < 0.0001f) {
    return {0.0f, 0.0f};
  }
  return {v.x / len, v.y / len};
}

/** 90-degree rotation; handedness is unused (offset sign only has to be consistent). */
Vec2 perpOf(Vec2 dir) { return {-dir.y, dir.x}; }

Vec2 pctToScreen(float x_pct, float y_pct) {
  return {kMapLeft + (x_pct * 0.01f) * static_cast<float>(kMapRight - kMapLeft),
          kMapTop + (y_pct * 0.01f) * static_cast<float>(kMapBottom - kMapTop)};
}

constexpr float windArrowLength(uint16_t speed_kt) {
  return speed_kt <= kBeaufort1MinKt
             ? kWindArrowMinLengthPx
             : speed_kt >= kBeaufort5MaxKt
                   ? kWindArrowMaxLengthPx
                   : kWindArrowMinLengthPx +
                         (kWindArrowMaxLengthPx - kWindArrowMinLengthPx) *
                             static_cast<float>(speed_kt - kBeaufort1MinKt) /
                             static_cast<float>(kBeaufort5MaxKt - kBeaufort1MinKt);
}

static_assert(windArrowLength(0) == kWindArrowMinLengthPx);
static_assert(windArrowLength(21) == kWindArrowMaxLengthPx);
static_assert(windArrowLength(40) == kWindArrowMaxLengthPx);

LGFX_Sprite s_frame(&tft);
bool s_frame_ready = false;

bool ensureFrameSprite() {
  if (s_frame_ready) {
    return true;
  }
  s_frame.setColorDepth(16);
  if (!s_frame.createSprite(kFrameSize, kFrameSize)) {
    Serial.println("eham_display: sprite alloc failed, drawing directly to panel");
    return false;
  }
  s_frame_ready = true;
  return true;
}

struct LabelStyle {
  bool ready = false;
  bool use_vlw = false;
  float vlw_size = 0.5f;
  const lgfx::GFXfont* gfx_font = &fonts::FreeSansBold9pt7b;
};

LabelStyle s_title_style;
LabelStyle s_end_label_style;

int measureVlwHeight(lgfx::LGFXBase& gfx, float size) {
  gfx.setTextSize(size);
  return gfx.fontHeight();
}

float findVlwSizeForHeight(lgfx::LGFXBase& gfx, int target_px) {
  float lo = 0.2f;
  float hi = 1.6f;
  for (int i = 0; i < 14; ++i) {
    const float mid = (lo + hi) * 0.5f;
    if (measureVlwHeight(gfx, mid) < target_px) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return hi;
}

void ensureLabelStyle(lgfx::LGFXBase& gfx, LabelStyle& style, int target_px,
                       const lgfx::GFXfont* bitmap_font) {
  if (style.ready) {
    return;
  }
  if (displayFontIsSmooth()) {
    style.use_vlw = true;
    style.vlw_size = findVlwSizeForHeight(gfx, target_px);
  } else {
    style.use_vlw = false;
    style.gfx_font = bitmap_font;
  }
  style.ready = true;
}

void applyLabelStyle(lgfx::LGFXBase& gfx, const LabelStyle& style) {
  if (style.use_vlw) {
    displayFontSetSmoothSize(gfx, style.vlw_size);
  } else {
    displayFontSetBitmap(gfx, style.gfx_font);
  }
}

void drawTitle(lgfx::LGFXBase& gfx) {
  displayFontEnsureLoaded(gfx);
  ensureLabelStyle(gfx, s_title_style, kTitleHeightPx, &fonts::FreeSansBold18pt7b);
  applyLabelStyle(gfx, s_title_style);
  gfx.setTextDatum(textdatum_t::top_center);
  gfx.setTextColor(schiphol::kColorTitle, schiphol::kColorBackground);
  gfx.drawString("EHAM", kFrameSize / 2, kTitleTopY);
}

void drawWindArrow(lgfx::LGFXBase& gfx, const EhamWeather& weather) {
  if (!weather.available || weather.calm_wind ||
      (weather.variable_wind && weather.wind_direction_deg == 0)) {
    return;
  }

  constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;
  const float angle = weather.wind_direction_deg * kDegreesToRadians;
  const Vec2 dir = {sinf(angle), -cosf(angle)};
  const float arrow_length = windArrowLength(weather.wind_speed_kt);
  const Vec2 center = {kWindArrowCenterX, kWindArrowCenterY};
  const Vec2 tail = sub(center, scale(dir, arrow_length * 0.5f));
  const Vec2 tip = add(center, scale(dir, arrow_length * 0.5f));
  constexpr float kHeadLengthPx = 6.0f;
  constexpr float kHeadHalfWidthPx = 4.0f;
  const Vec2 head_base = sub(tip, scale(dir, kHeadLengthPx));
  const Vec2 head_perp = scale(perpOf(dir), kHeadHalfWidthPx);

  gfx.drawWideLine(lroundf(tail.x), lroundf(tail.y), lroundf(head_base.x),
                   lroundf(head_base.y), 1.5f, schiphol::kColorWeather);
  gfx.fillTriangle(lroundf(tip.x), lroundf(tip.y),
                   lroundf(head_base.x + head_perp.x), lroundf(head_base.y + head_perp.y),
                   lroundf(head_base.x - head_perp.x), lroundf(head_base.y - head_perp.y),
                   schiphol::kColorWeather);
}

void drawChevron(lgfx::LGFXBase& gfx, Vec2 center, Vec2 dir, uint16_t color) {
  const Vec2 tip = add(center, scale(dir, kChevronHalfLenPx));
  const Vec2 base = sub(center, scale(dir, kChevronHalfLenPx));
  const Vec2 perp = perpOf(dir);
  const Vec2 left = add(base, scale(perp, kChevronHalfWidthPx));
  const Vec2 right = sub(base, scale(perp, kChevronHalfWidthPx));
  gfx.fillTriangle(lroundf(tip.x), lroundf(tip.y), lroundf(left.x), lroundf(left.y),
                    lroundf(right.x), lroundf(right.y), color);
}

void drawActivityLine(lgfx::LGFXBase& gfx, Vec2 active_pt, Vec2 opposite_pt, Vec2 offset,
                       uint16_t color, float chevron_t) {
  const Vec2 a = add(active_pt, offset);
  const Vec2 b = add(opposite_pt, offset);
  gfx.drawWideLine(lroundf(a.x), lroundf(a.y), lroundf(b.x), lroundf(b.y),
                    kActivityLineHalfWidth, color);

  const Vec2 dir = normalize(sub(opposite_pt, active_pt));
  const Vec2 chevron_center = lerp(a, b, chevron_t);
  drawChevron(gfx, chevron_center, dir, color);
}

void drawEndLabel(lgfx::LGFXBase& gfx, Vec2 point, Vec2 outward_dir, const char* text,
                   uint16_t color) {
  const Vec2 pos = add(point, scale(outward_dir, static_cast<float>(kEndLabelGapPx)));
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.setTextColor(color, schiphol::kColorBackground);
  gfx.drawString(text, lroundf(pos.x), lroundf(pos.y));
}

/** Departure takes priority over landing when a single end carries both roles. */
uint16_t endLabelColor(const RunwayEndUse& end) {
  if (end.departure) {
    return schiphol::kColorDepartureLabel;
  }
  if (end.landing) {
    return schiphol::kColorLandingLabel;
  }
  return schiphol::kColorRunwayInactiveLabel;
}

void drawRunway(lgfx::LGFXBase& gfx, size_t index, const PhysicalRunwayState& rw) {
  const auto& def = data::eham_runways::runwayDefinition(index);
  const Vec2 pa = pctToScreen(def.x1_pct, def.y1_pct);
  const Vec2 pb = pctToScreen(def.x2_pct, def.y2_pct);
  const Vec2 dir_ab = normalize(sub(pb, pa));
  const Vec2 perp = perpOf(dir_ab);

  gfx.drawWideLine(lroundf(pa.x), lroundf(pa.y), lroundf(pb.x), lroundf(pb.y),
                    kBaseLineHalfWidth, schiphol::kColorRunwayInactive);

  const bool has_landing = rw.end_a.landing || rw.end_b.landing;
  const bool has_departure = rw.end_a.departure || rw.end_b.departure;
  const bool dual = has_landing && has_departure;
  const Vec2 offset_unit = dual ? scale(perp, kParallelOffsetPx) : Vec2{0.0f, 0.0f};

  // Critical movement-direction rule: activity on an end travels from that
  // end toward the opposite end, for both landing and departure.
  if (has_landing) {
    const bool a_active = rw.end_a.landing;
    drawActivityLine(gfx, a_active ? pa : pb, a_active ? pb : pa, scale(offset_unit, -1.0f),
                      schiphol::kColorLanding, kLandingChevronT);
  }
  if (has_departure) {
    const bool a_active = rw.end_a.departure;
    drawActivityLine(gfx, a_active ? pa : pb, a_active ? pb : pa, offset_unit,
                      schiphol::kColorDeparture, kDepartureChevronT);
  }

  ensureLabelStyle(gfx, s_end_label_style, kEndLabelHeightPx, &fonts::FreeSansBold9pt7b);
  applyLabelStyle(gfx, s_end_label_style);
  drawEndLabel(gfx, pa, scale(dir_ab, -1.0f), def.end_a, endLabelColor(rw.end_a));
  drawEndLabel(gfx, pb, dir_ab, def.end_b, endLabelColor(rw.end_b));
}

void drawUnavailableBanner(lgfx::LGFXBase& gfx) {
  ensureLabelStyle(gfx, s_end_label_style, kEndLabelHeightPx, &fonts::FreeSansBold9pt7b);
  applyLabelStyle(gfx, s_end_label_style);
  gfx.setTextDatum(textdatum_t::bottom_center);
  gfx.setTextColor(schiphol::kColorWarning, schiphol::kColorBackground);
  gfx.drawString("LIVE DATA UNAVAILABLE", kFrameSize / 2, kFrameSize - 10);
}

void formatWeatherLine(const EhamWeather& weather, char* out, size_t out_len) {
  char wind[32];
  if (weather.calm_wind) {
    snprintf(wind, sizeof(wind), "CALM");
  } else if (weather.variable_wind && weather.wind_direction_deg == 0) {
    snprintf(wind, sizeof(wind), "VRB %uKT", weather.wind_speed_kt);
  } else {
    char gust[8] = "";
    if (weather.has_gust) {
      snprintf(gust, sizeof(gust), "G%u", weather.gust_speed_kt);
    }
    snprintf(wind, sizeof(wind), "%03u%s %u%sKT", weather.wind_direction_deg,
             weather.variable_wind ? "V" : "", weather.wind_speed_kt, gust);
  }
  snprintf(out, out_len, "%s  %dC", wind, weather.temperature_c);
}

/** Compact official EHAM weather row -- only ever called when weather.available. */
void drawWeatherRow(lgfx::LGFXBase& gfx, const EhamWeather& weather) {
  char line[48];
  formatWeatherLine(weather, line, sizeof(line));

  ensureLabelStyle(gfx, s_end_label_style, kEndLabelHeightPx, &fonts::FreeSansBold9pt7b);
  applyLabelStyle(gfx, s_end_label_style);
  gfx.setTextDatum(textdatum_t::bottom_center);
  gfx.setTextColor(schiphol::kColorWeather, schiphol::kColorBackground);
  gfx.drawString(line, kFrameSize / 2, kFrameSize - 10);
}

void renderFrame(lgfx::LGFXBase& gfx, const EhamOperationalState& state) {
  gfx.fillScreen(schiphol::kColorBackground);
  drawTitle(gfx);
  for (size_t i = 0; i < data::eham_runways::runwayCount(); ++i) {
    drawRunway(gfx, i, state.runways[i]);
  }
  drawWindArrow(gfx, state.weather);
  if (!state.runway_data_available) {
    drawUnavailableBanner(gfx);
  } else if (state.weather.available) {
    drawWeatherRow(gfx, state.weather);
  }
}

}  // namespace

void ehamDisplayDraw(const EhamOperationalState& state) {
  schiphol::initPalette();

  if (ensureFrameSprite()) {
    renderFrame(s_frame, state);
    s_frame.pushSprite(0, 0);
    return;
  }

  renderFrame(tft, state);
}

}  // namespace ui
