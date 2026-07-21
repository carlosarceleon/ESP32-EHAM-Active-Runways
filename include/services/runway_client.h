#pragma once

#include "domain/eham_state.h"

namespace services::runway {

/**
 * Call every loop() iteration once Wi-Fi is connected. No-op until the clock
 * is valid. Fetches immediately the first time it is due, then on the
 * five-minute/backoff schedule described in the implementation plan. May
 * block for up to the configured HTTP timeout while a fetch is in flight.
 */
void runwayLoop();

/** Requests an out-of-schedule fetch on the next runwayLoop() call (BOOT short tap). */
void forceRefresh();

/** The latest state: last successful fetch, or a cleared/stale state past the freshness limit. */
const EhamOperationalState& currentState();

/** True if currentState() changed since the last call to this function (consumes the flag). */
bool consumeStateChanged();

}  // namespace services::runway
