#pragma once

#include <cstddef>
#include <ctime>

#include "domain/eham_state.h"

namespace services::runway {

enum class ParseResult {
  Ok,
  MalformedJson,
  NoAcceptableSlot,
};

/** Accept a past slot's `until` up to this many seconds before `now_utc`. */
constexpr long kBoundedFallbackSec = 10 * 60;

/**
 * Parses a dutchplanespotters `{"times": [...]}` response and selects the
 * slot covering `now_utc` -- or, failing that, the most recent past slot
 * whose `until` is no more than `kBoundedFallbackSec` before `now_utc`.
 *
 * On ParseResult::Ok, `out` is filled from a fully-inactive state (landing
 * and departure flags applied independently) and `slot_from_utc` /
 * `slot_until_utc` hold the selected slot's bounds. On any other result,
 * `out` is left unspecified -- callers must not apply it.
 *
 * Unknown runway headings are logged and skipped; they do not fail the
 * parse. `now_utc` must come from a synchronized clock -- this function is
 * otherwise a pure computation over its inputs.
 */
ParseResult parseRunwayResponse(const char* json, size_t len, std::time_t now_utc,
                                 EhamOperationalState* out, std::time_t* slot_from_utc,
                                 std::time_t* slot_until_utc);

#if defined(EHAM_ENABLE_FIXTURES)
/** Runs the slot-selection/parsing assertions below and prints PASS/FAIL to Serial. */
void runParserSelfTest();
#endif

}  // namespace services::runway
