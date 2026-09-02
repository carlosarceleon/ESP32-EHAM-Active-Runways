#pragma once

#include <cstddef>
#include <cstdint>

#include <driver/gpio.h>

namespace config {

// --- Firmware ---
constexpr char kFirmwareVersion[] = "1.0.0";

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

// --- AWC METAR (weather row) ---
/** Public Aviation Weather Center endpoint; no API key is required. */
constexpr char kAwcMetarUrl[] = "https://aviationweather.gov/api/data/metar?ids=EHAM&format=raw";
constexpr size_t kAwcMetarMaxBytes = 4096;
constexpr unsigned long kMetarFetchIntervalMs = 30UL * 60 * 1000;
/** Deterministic 0-120s per-device spread so devices don't all poll at the same instant. */
constexpr unsigned long kMetarJitterRangeMs = 120UL * 1000;

// --- UI colors (RGB565) — status screens ---
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kTextOnYellow = kColorBlack;
constexpr uint16_t kTextOnBlack = 0xFFFF;

}  // namespace config
