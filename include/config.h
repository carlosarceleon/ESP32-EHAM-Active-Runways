#pragma once

#include <cstddef>
#include <cstdint>

#include <driver/gpio.h>

namespace config {

// --- Wi-Fi portal ---
constexpr char kPortalApName[] = "SchipholRunways-Setup";
constexpr char kPortalIp[] = "192.168.4.1";
/** mDNS host (no ".local" suffix); browser: http://schiphol-runways.local */
constexpr char kPortalHostname[] = "schiphol-runways";
constexpr char kPortalHostUrl[] = "schiphol-runways.local";

/** Per-attempt STA connect wait (ms); retried kWifiConnectAttempts times. */
constexpr unsigned long kWifiConnectAttemptMs = 15000;
constexpr uint8_t kWifiConnectAttempts = 3;
constexpr unsigned long kWifiPortalTimeoutSec = 0;  // 0 = no timeout while configuring
constexpr unsigned long kWifiConnectingFrameMs = 50;
/** Wait after disconnect before reconnecting (avoids portal on brief drops). */
constexpr unsigned long kWifiDownGraceMs = 4000;
/** Minimum interval between background reconnect tries. */
constexpr unsigned long kWifiReconnectIntervalMs = 15000;

// --- BOOT button (ESP32-C3 Super Mini, active LOW) ---
constexpr gpio_num_t kBootPin = GPIO_NUM_9;
constexpr unsigned long kBootResetHoldMs = 3000UL;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = 40UL;

// --- Display: GC9A01 1.28" round 240×240 (SPI) ---
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_0;
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_1;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_10;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_3;  // display SDA
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_4;  // display SCL

constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 240;

constexpr uint32_t kDisplaySpiWriteHz = 40000000;
// GC9A01 modules often need invert + BGR for correct black/green output
constexpr bool kDisplayInvert = true;
constexpr bool kDisplayRgbOrder = true;

// --- Radar center defaults (overridden via WiFi setup portal) ---
constexpr double kDefaultRadarLat = 52.3676;
constexpr double kDefaultRadarLon = 4.9041;

/** Poll adsb.fi (API public limit: 1 req/s). */
constexpr unsigned long kAdsbFetchIntervalMs = 3000;
/** Legacy scale unused — fetch uses radar::fetchRadiusKm() to screen edge. */
constexpr float kAdsbFetchRadiusScale = 1.0f;
/** false = hide aircraft with alt_baro "ground"; true = show them too. */
constexpr bool kAdsbShowGroundAircraft = false;

// --- Schiphol runway API ---
constexpr char kRunwayApiUrl[] = "https://www.dutchplanespotters.nl/api/runways/ams";
constexpr char kHttpUserAgent[] = "ESP32-Schiphol-Runway-Display/1.0";
constexpr unsigned long kHttpConnectTimeoutMs = 5000;
constexpr unsigned long kHttpTotalTimeoutMs = 12000;
constexpr size_t kRunwayMaxBodyBytes = 64 * 1024;
constexpr unsigned long kRunwayFetchIntervalMs = 5UL * 60 * 1000;
constexpr unsigned long kRunwayRetryIntervalMs = 60UL * 1000;
constexpr unsigned long kRunwaySlowRetryIntervalMs = 5UL * 60 * 1000;
constexpr uint8_t kRunwayFailuresBeforeSlowBackoff = 3;
/** Keep showing the last successful fetch as "live" for this long before clearing it. */
constexpr long kRunwayFreshnessLimitSec = 15 * 60;

// --- KNMI anonymous token manifest ---
constexpr char kKnmiManifestUrl[] =
    "https://raw.githubusercontent.com/carlosarceleon/schiphol-display-config/main/knmi-token.json";
constexpr char kKnmiApiBase[] = "https://api.dataplatform.knmi.nl/open-data/v1";
/** Cheapest possible KNMI request, used only to confirm a token is accepted. */
constexpr char kKnmiFileListValidationPath[] =
    "/datasets/metar/versions/1.0/files?maxKeys=1&orderBy=created&sorting=desc";
constexpr size_t kKnmiManifestMaxBytes = 2048;
constexpr size_t kKnmiValidationMaxBytes = 4096;
/** Normal refresh cadence once a token has ever been verified. */
constexpr unsigned long kKnmiTokenRefreshIntervalMs = 24UL * 60 * 60 * 1000;
/** Faster retry cadence until the first token is ever verified (or after a forced refresh keeps failing). */
constexpr unsigned long kKnmiTokenRetryIntervalMs = 5UL * 60 * 1000;

// --- KNMI METAR (weather row) ---
/** Lists the most recently created files across all stations; filtered client-side for "EHAM". */
constexpr char kKnmiMetarFileListPath[] =
    "/datasets/metar/versions/1.0/files?maxKeys=25&orderBy=created&sorting=desc";
constexpr char kKnmiMetarFileUrlPathPrefix[] = "/datasets/metar/versions/1.0/files/";
constexpr char kKnmiMetarFileUrlPathSuffix[] = "/url";
constexpr size_t kKnmiMetarListMaxBytes = 16 * 1024;
constexpr size_t kKnmiMetarUrlMaxBytes = 2 * 1024;
/** Observed file size is ~3.0-3.6 KB; leaves headroom without risking the 64 KiB display buffer budget. */
constexpr size_t kKnmiMetarFileMaxBytes = 8 * 1024;
constexpr unsigned long kKnmiMetarFetchIntervalMs = 30UL * 60 * 1000;
/** Deterministic 0-120s per-device spread so devices don't all poll KNMI at the same instant. */
constexpr unsigned long kKnmiMetarJitterRangeMs = 120UL * 1000;

// --- UI colors (RGB565) — status screens ---
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kTextOnYellow = kColorBlack;
constexpr uint16_t kTextOnBlack = 0xFFFF;

}  // namespace config
