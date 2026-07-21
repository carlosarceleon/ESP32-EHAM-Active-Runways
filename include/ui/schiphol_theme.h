#pragma once

#include <cstdint>

namespace ui::schiphol {

// --- Schiphol brand palette (RGB888 source values) ---
// Names/hex/roles per docs/agent-instructions/ESP32_Schiphol_Runway_Display_Implementation_Plan.md §8.
constexpr uint8_t kNorthSeaR = 0x2E, kNorthSeaG = 0x3F, kNorthSeaB = 0x42;  // #2E3F42 — secondary dark surfaces
constexpr uint8_t kNorthSeaSubtleR = 0x37, kNorthSeaSubtleG = 0x4B,
                   kNorthSeaSubtleB = 0x4F;  // #374B4F — inactive runway strips, dividers
constexpr uint8_t kNorthSeaDarkR = 0x23, kNorthSeaDarkG = 0x30,
                   kNorthSeaDarkB = 0x32;  // #233032 — main screen background
constexpr uint8_t kSkyBlissR = 0xC8, kSkyBlissG = 0xE2, kSkyBlissB = 0xF0;  // #C8E2F0 — supporting arrival accents
constexpr uint8_t kSkyBlissLightestR = 0xED, kSkyBlissLightestG = 0xF6,
                   kSkyBlissLightestB = 0xFC;  // #EDF6FC — active landing end label
constexpr uint8_t kSkyBlissLighterR = 0xDA, kSkyBlissLighterG = 0xEC,
                   kSkyBlissLighterB = 0xF5;  // #DAECF5 — weather / secondary text
constexpr uint8_t kSkyBlissStrongR = 0x9A, kSkyBlissStrongG = 0xBA,
                   kSkyBlissStrongB = 0xD1;  // #9ABAD1 — landing runway activity
constexpr uint8_t kGoldenDuneR = 0xC4, kGoldenDuneG = 0xB6,
                   kGoldenDuneB = 0x6C;  // #C4B66C — neutral warnings, live-data-unavailable text
constexpr uint8_t kGoldenDuneLightestR = 0xF3, kGoldenDuneLightestG = 0xF0,
                   kGoldenDuneLightestB = 0xE2;  // #F3F0E2 — main labels and title
constexpr uint8_t kGoldenDuneLightR = 0xD7, kGoldenDuneLightG = 0xCD,
                   kGoldenDuneLightB = 0x9B;  // #D7CD9B — inactive runway identifiers
constexpr uint8_t kGoldenDuneDarkR = 0x99, kGoldenDuneDarkG = 0x86,
                   kGoldenDuneDarkB = 0x3B;  // #99863B — low-priority neutral accents
constexpr uint8_t kOrangeStrandR = 0xFF, kOrangeStrandG = 0x77,
                   kOrangeStrandB = 0x00;  // #FF7700 — departure runway activity

// --- Computed panel colors (RGB565), set by initPalette() ---
extern uint16_t kColorBackground;
extern uint16_t kColorRunwayInactive;
extern uint16_t kColorRunwayInactiveLabel;
extern uint16_t kColorTitle;
extern uint16_t kColorLanding;
extern uint16_t kColorLandingLabel;
extern uint16_t kColorDeparture;
extern uint16_t kColorDepartureLabel;
extern uint16_t kColorWarning;

/**
 * Single RGB888 -> panel RGB565 conversion point for the Schiphol theme.
 * Swaps R/B before packing: on this GC9A01 module, sprite draws render true
 * red as blue unless swapped in software (panel-level rgb_order in
 * lgfx_config.hpp does not affect LGFX_Sprite content), confirmed via the
 * radar aircraft marker in ui::radar. Verify with paletteCalibrationDraw()
 * on physical hardware before relying on this for a new panel revision.
 */
uint16_t toPanelColor(uint8_t r, uint8_t g, uint8_t b);

/** Computes all kColor* fields above via toPanelColor(). Call after displayInit(). */
void initPalette();

#if defined(EHAM_ENABLE_FIXTURES)
/** Temporary full-screen swatch + label for every theme color. Test mode only. */
void paletteCalibrationDraw();
#endif

}  // namespace ui::schiphol
