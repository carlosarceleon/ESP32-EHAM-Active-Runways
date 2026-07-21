/**
 * EHAM Active Runways — WiFi setup, then Schiphol runway status UI on the
 * round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "domain/eham_state.h"
#include "hardware/display.h"
#include "services/clock_service.h"
#include "services/wifi_setup.h"
#include "ui/eham_display.h"
#include "ui/schiphol_theme.h"
#include "ui/status_screens.h"

#if defined(EHAM_ENABLE_FIXTURES)
#include "data/eham_runways.h"
#endif

namespace {

#if defined(EHAM_ENABLE_FIXTURES)

using FixtureFn = EhamOperationalState (*)();

constexpr FixtureFn kFixtures[] = {
    data::eham_runways::fixtureAllInactive,
    data::eham_runways::fixtureLanding18R,
    data::eham_runways::fixtureDeparture24,
    data::eham_runways::fixtureLanding18RDeparture36L,
    data::eham_runways::fixtureLanding18CDeparture18C,
    data::eham_runways::fixtureMultipleActive,
};
constexpr size_t kFixtureCount = sizeof(kFixtures) / sizeof(kFixtures[0]);

size_t g_fixture_index = 0;

void drawCurrentFixture() {
  Serial.printf("EHAM fixture %u/%u\n", static_cast<unsigned>(g_fixture_index + 1),
                static_cast<unsigned>(kFixtureCount));
  ui::ehamDisplayDraw(kFixtures[g_fixture_index]());
}

void onBootTap() {
  g_fixture_index = (g_fixture_index + 1) % kFixtureCount;
  drawCurrentFixture();
}

#else  // !defined(EHAM_ENABLE_FIXTURES)

bool g_screen_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;

void showEhamScreenIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_screen_visible = false;
    return;
  }
  // Live runway data lands in a later milestone; render the deterministic
  // all-inactive state until then.
  ui::ehamDisplayDraw(EhamOperationalState{});
  g_screen_visible = true;
}

#endif  // defined(EHAM_ENABLE_FIXTURES)

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
#if defined(EHAM_ENABLE_FIXTURES)
    onBootTap();
#else
    Serial.println("BOOT tap — force refresh requested");
#endif
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Schiphol Runway Display");

  bootButtonInit();
  displayInit();
  ui::schiphol::initPalette();

#if defined(EHAM_ENABLE_FIXTURES)
  data::eham_runways::runSelfTest();
  services::clock::runClockSelfTest();
  drawCurrentFixture();
#else
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }

  if (wifiSetupConnect()) {
    services::clock::beginClockSync();
    showEhamScreenIfConnected();
  }
#endif
}

void loop() {
  handleBootButton();

#if !defined(EHAM_ENABLE_FIXTURES)
  wifiLoop();
  services::clock::clockLoop();

  if (WiFi.status() != WL_CONNECTED) {
    if (g_screen_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_screen_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showEhamScreenIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (!g_screen_visible) {
      showEhamScreenIfConnected();
    }
  }
#endif

  delay(10);
}
