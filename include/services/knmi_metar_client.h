#pragma once

#include "domain/eham_state.h"

namespace services::metar_client {

/**
 * Call every loop() iteration once Wi-Fi is connected. No-op until the clock
 * is valid and a KNMI token is available. Refreshes on a 30-minute schedule
 * (plus a fixed per-device jitter). May block for up to a few HTTP timeouts
 * while a fetch is in flight; never affects runway fetch scheduling.
 */
void metarLoop();

/** The latest weather: available only once a full fetch+parse cycle has succeeded. */
const EhamWeather& currentWeather();

/** True if currentWeather() changed since the last call to this function (consumes the flag). */
bool consumeWeatherChanged();

}  // namespace services::metar_client
