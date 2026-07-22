#include "ui/schiphol_theme.h"

#include "hardware/display.h"

#if defined(EHAM_ENABLE_FIXTURES)
#include "hardware/display_font.h"
#endif

namespace ui::schiphol {

uint16_t kColorBackground = 0x0000;
uint16_t kColorRunwayInactive = 0x0000;
uint16_t kColorRunwayInactiveLabel = 0x0000;
uint16_t kColorTitle = 0xFFFF;
uint16_t kColorLanding = 0x0000;
uint16_t kColorLandingLabel = 0xFFFF;
uint16_t kColorDeparture = 0x0000;
uint16_t kColorDepartureLabel = 0x0000;
uint16_t kColorWarning = 0x0000;
uint16_t kColorWeather = 0xFFFF;

uint16_t toPanelColor(uint8_t r, uint8_t g, uint8_t b) {
  return tft.color565(b, g, r);
}

void initPalette() {
  kColorBackground = toPanelColor(kNorthSeaDarkR, kNorthSeaDarkG, kNorthSeaDarkB);
  kColorRunwayInactive =
      toPanelColor(kNorthSeaSubtleR, kNorthSeaSubtleG, kNorthSeaSubtleB);
  kColorRunwayInactiveLabel =
      toPanelColor(kGoldenDuneLightR, kGoldenDuneLightG, kGoldenDuneLightB);
  kColorTitle =
      toPanelColor(kGoldenDuneLightestR, kGoldenDuneLightestG, kGoldenDuneLightestB);
  kColorLanding = toPanelColor(kSkyBlissStrongR, kSkyBlissStrongG, kSkyBlissStrongB);
  kColorLandingLabel =
      toPanelColor(kSkyBlissLightestR, kSkyBlissLightestG, kSkyBlissLightestB);
  kColorDeparture = toPanelColor(kOrangeStrandR, kOrangeStrandG, kOrangeStrandB);
  kColorDepartureLabel = kColorDeparture;
  kColorWarning = toPanelColor(kGoldenDuneR, kGoldenDuneG, kGoldenDuneB);
  kColorWeather = toPanelColor(kSkyBlissLighterR, kSkyBlissLighterG, kSkyBlissLighterB);
}

#if defined(EHAM_ENABLE_FIXTURES)

namespace {

struct Swatch {
  const char* label;
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

constexpr Swatch kSwatches[] = {
    {"North Sea", kNorthSeaR, kNorthSeaG, kNorthSeaB},
    {"North Sea Subtle", kNorthSeaSubtleR, kNorthSeaSubtleG, kNorthSeaSubtleB},
    {"North Sea Dark", kNorthSeaDarkR, kNorthSeaDarkG, kNorthSeaDarkB},
    {"Sky Bliss", kSkyBlissR, kSkyBlissG, kSkyBlissB},
    {"Sky Bliss Lightest", kSkyBlissLightestR, kSkyBlissLightestG, kSkyBlissLightestB},
    {"Sky Bliss Lighter", kSkyBlissLighterR, kSkyBlissLighterG, kSkyBlissLighterB},
    {"Sky Bliss Strong", kSkyBlissStrongR, kSkyBlissStrongG, kSkyBlissStrongB},
    {"Golden Dune", kGoldenDuneR, kGoldenDuneG, kGoldenDuneB},
    {"Golden Dune Lightest", kGoldenDuneLightestR, kGoldenDuneLightestG,
     kGoldenDuneLightestB},
    {"Golden Dune Light", kGoldenDuneLightR, kGoldenDuneLightG, kGoldenDuneLightB},
    {"Golden Dune Dark", kGoldenDuneDarkR, kGoldenDuneDarkG, kGoldenDuneDarkB},
    {"Orange Strand", kOrangeStrandR, kOrangeStrandG, kOrangeStrandB},
};
constexpr size_t kSwatchCount = sizeof(kSwatches) / sizeof(kSwatches[0]);

}  // namespace

void paletteCalibrationDraw() {
  displayFontEnsureLoaded(tft);
  displayFontSetSmoothSize(tft, 0.4f);

  tft.fillScreen(toPanelColor(kNorthSeaDarkR, kNorthSeaDarkG, kNorthSeaDarkB));

  constexpr int kRowH = 240 / kSwatchCount;
  constexpr int kSwatchW = 60;
  for (size_t i = 0; i < kSwatchCount; ++i) {
    const int y = static_cast<int>(i) * kRowH;
    const Swatch& sw = kSwatches[i];
    tft.fillRect(0, y, kSwatchW, kRowH, toPanelColor(sw.r, sw.g, sw.b));
    tft.setTextDatum(textdatum_t::middle_left);
    tft.setTextColor(toPanelColor(0xFF, 0xFF, 0xFF), toPanelColor(kNorthSeaDarkR,
                                                                   kNorthSeaDarkG,
                                                                   kNorthSeaDarkB));
    tft.drawString(sw.label, kSwatchW + 4, y + kRowH / 2);
  }
}

#endif  // defined(EHAM_ENABLE_FIXTURES)

}  // namespace ui::schiphol
