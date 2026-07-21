#include "services/runway_parser.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include "data/eham_runways.h"
#include "services/clock_service.h"

namespace services::runway {

namespace {

bool parseTimestamp(JsonObject slot, const char* key, std::time_t* out) {
  if (!slot[key].is<const char*>()) {
    return false;
  }
  return services::clock::parseIso8601ToUtc(slot[key].as<const char*>(), out);
}

void applyHeadingArray(JsonArray headings, EhamOperationalState& state, bool is_landing) {
  for (JsonVariant v : headings) {
    if (!v.is<const char*>()) {
      continue;
    }
    const char* heading = v.as<const char*>();
    const bool applied = is_landing ? data::eham_runways::applyLanding(state, heading)
                                     : data::eham_runways::applyDeparture(state, heading);
    if (!applied) {
      Serial.printf("runway_parser: unknown %s heading '%s'\n",
                    is_landing ? "landing" : "departing", heading);
    }
  }
}

}  // namespace

ParseResult parseRunwayResponse(const char* json, size_t len, std::time_t now_utc,
                                 EhamOperationalState* out, std::time_t* slot_from_utc,
                                 std::time_t* slot_until_utc) {
  if (json == nullptr || out == nullptr) {
    return ParseResult::MalformedJson;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json, len);
  if (err) {
    Serial.printf("runway_parser: JSON error: %s\n", err.c_str());
    return ParseResult::MalformedJson;
  }

  JsonArray times = doc["times"].as<JsonArray>();
  if (times.isNull()) {
    Serial.println("runway_parser: response missing times[]");
    return ParseResult::MalformedJson;
  }

  bool have_exact = false;
  JsonObject exact_slot;
  std::time_t exact_from = 0;
  std::time_t exact_until = 0;

  bool have_past = false;
  JsonObject past_slot;
  std::time_t past_from = 0;
  std::time_t past_until = 0;

  for (JsonObject slot : times) {
    std::time_t from_t = 0;
    std::time_t until_t = 0;
    if (!parseTimestamp(slot, "from", &from_t) || !parseTimestamp(slot, "until", &until_t)) {
      continue;
    }

    if (from_t <= now_utc && now_utc <= until_t) {
      have_exact = true;
      exact_slot = slot;
      exact_from = from_t;
      exact_until = until_t;
    } else if (until_t <= now_utc && (!have_past || until_t > past_until)) {
      have_past = true;
      past_slot = slot;
      past_from = from_t;
      past_until = until_t;
    }
  }

  JsonObject chosen;
  std::time_t chosen_from = 0;
  std::time_t chosen_until = 0;
  if (have_exact) {
    chosen = exact_slot;
    chosen_from = exact_from;
    chosen_until = exact_until;
  } else if (have_past && (now_utc - past_until) <= kBoundedFallbackSec) {
    chosen = past_slot;
    chosen_from = past_from;
    chosen_until = past_until;
  } else {
    return ParseResult::NoAcceptableSlot;
  }

  EhamOperationalState state{};
  applyHeadingArray(chosen["landingRunways"].as<JsonArray>(), state, /*is_landing=*/true);
  applyHeadingArray(chosen["departingRunways"].as<JsonArray>(), state, /*is_landing=*/false);

  *out = state;
  if (slot_from_utc != nullptr) *slot_from_utc = chosen_from;
  if (slot_until_utc != nullptr) *slot_until_utc = chosen_until;
  return ParseResult::Ok;
}

#if defined(EHAM_ENABLE_FIXTURES)

namespace {

void checkTrue(const char* label, bool condition, bool* all_passed) {
  Serial.printf("[runway_parser self-test] %s: %s\n", label, condition ? "PASS" : "FAIL");
  if (!condition) {
    *all_passed = false;
  }
}

// Mirrors fixtures/runway_no_exact_slot.json -- kept in sync by hand since
// there's no on-device file reading (see clock_service.cpp's equivalent note).
constexpr char kNoExactSlotJson[] =
    R"({"times":[)"
    R"({"from":"2026-07-21T00:00:00+00:00","until":"2026-07-21T04:40:00+00:00",)"
    R"("landingRunways":["06"],"departingRunways":["36L"]},)"
    R"({"from":"2026-07-21T05:00:00+00:00","until":"2026-07-21T05:30:00+00:00",)"
    R"("landingRunways":["06","36R"],"departingRunways":["36L"]})"
    R"(]})";

// Mirrors fixtures/runway_current.json's first slot.
constexpr char kExactSlotJson[] =
    R"({"times":[)"
    R"({"from":"2026-07-21T00:00:02+00:00","until":"2026-07-21T04:40:00+00:00",)"
    R"("landingRunways":["06"],"departingRunways":["36L"]})"
    R"(]})";

constexpr char kUnknownHeadingJson[] =
    R"({"times":[)"
    R"({"from":"2026-07-21T00:00:00+00:00","until":"2026-07-21T04:40:00+00:00",)"
    R"("landingRunways":["06","99Z"],"departingRunways":["36L"]})"
    R"(]})";

// Mirrors fixtures/runway_malformed.json (truncated JSON).
constexpr char kMalformedJson[] =
    R"({"times":[{"from":"2026-07-21T00:00:00+00:00","until":"2026-07-21T04:40:00+00:00",)"
    R"("landingRunways":["06"],"departingRunways":["36L")";

std::time_t isoToEpoch(const char* iso) {
  std::time_t t = 0;
  services::clock::parseIso8601ToUtc(iso, &t);
  return t;
}

}  // namespace

void runParserSelfTest() {
  bool all_passed = true;
  EhamOperationalState state{};
  std::time_t from_t = 0;
  std::time_t until_t = 0;

  // Exact slot: now falls inside [from, until].
  const std::time_t inside_now = isoToEpoch("2026-07-21T02:00:00+00:00");
  ParseResult result = parseRunwayResponse(kExactSlotJson, sizeof(kExactSlotJson) - 1, inside_now,
                                            &state, &from_t, &until_t);
  checkTrue("exact slot parses Ok", result == ParseResult::Ok, &all_passed);
  checkTrue("exact slot applies 06 landing / 36L departure",
            data::eham_runways::findRunwayEnd("06", nullptr, nullptr) &&
                state.runways[4].end_a.landing && state.runways[0].end_b.departure,
            &all_passed);

  // Bounded fallback: 5 minutes after the last slot's `until` (05:30) -> accepted.
  const std::time_t just_after = isoToEpoch("2026-07-21T05:35:00+00:00");
  result = parseRunwayResponse(kNoExactSlotJson, sizeof(kNoExactSlotJson) - 1, just_after, &state,
                                &from_t, &until_t);
  checkTrue("bounded fallback (5 min past) accepted", result == ParseResult::Ok, &all_passed);
  checkTrue("bounded fallback picks the later slot",
            until_t == isoToEpoch("2026-07-21T05:30:00+00:00"), &all_passed);

  // Too far past: 15 minutes after `until` -> rejected.
  const std::time_t too_late = isoToEpoch("2026-07-21T05:45:00+00:00");
  result = parseRunwayResponse(kNoExactSlotJson, sizeof(kNoExactSlotJson) - 1, too_late, &state,
                                &from_t, &until_t);
  checkTrue("stale-by-15-minutes slot rejected", result == ParseResult::NoAcceptableSlot,
            &all_passed);

  // Unknown heading is skipped, not fatal.
  result = parseRunwayResponse(kUnknownHeadingJson, sizeof(kUnknownHeadingJson) - 1, inside_now,
                                &state, &from_t, &until_t);
  checkTrue("unknown heading does not fail the parse", result == ParseResult::Ok, &all_passed);
  checkTrue("unknown heading still applies the known one alongside it",
            state.runways[4].end_a.landing, &all_passed);

  // Malformed JSON.
  result = parseRunwayResponse(kMalformedJson, sizeof(kMalformedJson) - 1, inside_now, &state,
                                &from_t, &until_t);
  checkTrue("malformed JSON is rejected", result == ParseResult::MalformedJson, &all_passed);

  Serial.printf("[runway_parser self-test] %s\n", all_passed ? "ALL PASS" : "SOME FAILED");
}

#endif  // EHAM_ENABLE_FIXTURES

}  // namespace services::runway
