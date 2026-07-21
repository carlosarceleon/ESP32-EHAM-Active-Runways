#include "data/eham_runways.h"

#include <cctype>
#include <cstring>

#if defined(EHAM_ENABLE_FIXTURES)
#include <Arduino.h>
#endif

namespace data::eham_runways {

namespace {

constexpr EhamRunwayDefinition kRunways[] = {
    {"18R/36L", "Polderbaan", "18R", "36L", 23.0f, 13.0f, 21.1f, 46.7f},
    {"18C/36C", "Zwanenburgbaan", "18C", "36C", 40.0f, 44.0f, 38.3f, 73.2f},
    {"09/27", "Oostbaan", "09", "27", 43.7f, 58.6f, 74.4f, 56.8f},
    {"18L/36R", "Aalsmeerbaan", "18L", "36R", 64.2f, 54.0f, 62.5f, 84.1f},
    {"06/24", "Kaagbaan", "06", "24", 36.4f, 87.0f, 62.6f, 70.5f},
    {"04/22", "Buitenveldertbaan", "04", "22", 66.2f, 74.6f, 78.9f, 60.2f},
};

constexpr size_t kRunwayCount = sizeof(kRunways) / sizeof(kRunways[0]);

}  // namespace

size_t runwayCount() { return kRunwayCount; }

const EhamRunwayDefinition& runwayDefinition(size_t index) {
  return kRunways[index];
}

bool normalizeHeading(const char* raw, char* out, size_t out_len) {
  if (raw == nullptr || out == nullptr || out_len == 0) {
    return false;
  }

  // Trim leading/trailing whitespace.
  while (*raw != '\0' && std::isspace(static_cast<unsigned char>(*raw))) {
    ++raw;
  }
  const char* end = raw + std::strlen(raw);
  while (end > raw && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
    --end;
  }

  const size_t trimmed_len = static_cast<size_t>(end - raw);
  if (trimmed_len == 0 || trimmed_len >= out_len) {
    return false;
  }

  for (size_t i = 0; i < trimmed_len; ++i) {
    out[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(raw[i])));
  }
  out[trimmed_len] = '\0';

  // Bare "18"/"36" alias to Zwanenburgbaan, per the reference integration.
  if (std::strcmp(out, "18") == 0) {
    if (out_len < 4) return false;
    std::strcpy(out, "18C");
  } else if (std::strcmp(out, "36") == 0) {
    if (out_len < 4) return false;
    std::strcpy(out, "36C");
  }

  return true;
}

bool findRunwayEnd(const char* heading, size_t* runway_index, bool* is_end_a) {
  if (heading == nullptr) {
    return false;
  }

  for (size_t i = 0; i < kRunwayCount; ++i) {
    if (std::strcmp(kRunways[i].end_a, heading) == 0) {
      if (runway_index != nullptr) *runway_index = i;
      if (is_end_a != nullptr) *is_end_a = true;
      return true;
    }
    if (std::strcmp(kRunways[i].end_b, heading) == 0) {
      if (runway_index != nullptr) *runway_index = i;
      if (is_end_a != nullptr) *is_end_a = false;
      return true;
    }
  }
  return false;
}

namespace {

bool applyToEnd(EhamOperationalState& state, const char* heading, bool RunwayEndUse::*flag) {
  char normalized[8];
  if (!normalizeHeading(heading, normalized, sizeof(normalized))) {
    return false;
  }

  size_t runway_index = 0;
  bool is_end_a = false;
  if (!findRunwayEnd(normalized, &runway_index, &is_end_a)) {
    return false;
  }

  RunwayEndUse& use = is_end_a ? state.runways[runway_index].end_a
                                : state.runways[runway_index].end_b;
  use.*flag = true;
  return true;
}

}  // namespace

bool applyLanding(EhamOperationalState& state, const char* heading) {
  return applyToEnd(state, heading, &RunwayEndUse::landing);
}

bool applyDeparture(EhamOperationalState& state, const char* heading) {
  return applyToEnd(state, heading, &RunwayEndUse::departure);
}

#if defined(EHAM_ENABLE_FIXTURES)

EhamOperationalState fixtureAllInactive() { return EhamOperationalState{}; }

EhamOperationalState fixtureLanding18R() {
  EhamOperationalState state;
  applyLanding(state, "18R");
  return state;
}

EhamOperationalState fixtureDeparture24() {
  EhamOperationalState state;
  applyDeparture(state, "24");
  return state;
}

EhamOperationalState fixtureLanding18RDeparture36L() {
  EhamOperationalState state;
  applyLanding(state, "18R");
  applyDeparture(state, "36L");
  return state;
}

EhamOperationalState fixtureLanding18CDeparture18C() {
  EhamOperationalState state;
  applyLanding(state, "18C");
  applyDeparture(state, "18C");
  return state;
}

EhamOperationalState fixtureMultipleActive() {
  EhamOperationalState state;
  applyLanding(state, "27");
  applyLanding(state, "36C");
  applyDeparture(state, "36L");
  applyDeparture(state, "06");
  return state;
}

namespace {

void checkTrue(const char* label, bool condition, bool* all_passed) {
  Serial.printf("[eham_runways self-test] %s: %s\n", label, condition ? "PASS" : "FAIL");
  if (!condition) {
    *all_passed = false;
  }
}

}  // namespace

void runSelfTest() {
  bool all_passed = true;

  size_t idx = 0;
  bool is_end_a = false;

  checkTrue("18R maps to Polderbaan end A", findRunwayEnd("18R", &idx, &is_end_a) &&
                                                 idx == 0 && is_end_a,
            &all_passed);
  checkTrue("36L maps to Polderbaan end B", findRunwayEnd("36L", &idx, &is_end_a) &&
                                                 idx == 0 && !is_end_a,
            &all_passed);
  checkTrue("09 maps to Oostbaan end A",
            findRunwayEnd("09", &idx, &is_end_a) && idx == 2 && is_end_a, &all_passed);
  checkTrue("04 maps to Buitenveldertbaan end A",
            findRunwayEnd("04", &idx, &is_end_a) && idx == 5 && is_end_a, &all_passed);

  char normalized[8] = {0};
  checkTrue("bare 18 normalizes to 18C",
            normalizeHeading("18", normalized, sizeof(normalized)) &&
                std::strcmp(normalized, "18C") == 0,
            &all_passed);
  checkTrue("bare 36 normalizes to 36C",
            normalizeHeading("36", normalized, sizeof(normalized)) &&
                std::strcmp(normalized, "36C") == 0,
            &all_passed);
  checkTrue("lowercase/whitespace normalizes",
            normalizeHeading("  18r  ", normalized, sizeof(normalized)) &&
                std::strcmp(normalized, "18R") == 0,
            &all_passed);

  checkTrue("unknown identifier is rejected", !findRunwayEnd("99Z", &idx, &is_end_a),
            &all_passed);

  EhamOperationalState both = fixtureLanding18CDeparture18C();
  checkTrue("18C landing+departure both set independently",
            both.runways[1].end_a.landing && both.runways[1].end_a.departure,
            &all_passed);

  EhamOperationalState opposite = fixtureLanding18RDeparture36L();
  checkTrue("18R landing / 36L departure land on opposite ends of runway 0",
            opposite.runways[0].end_a.landing && !opposite.runways[0].end_a.departure &&
                opposite.runways[0].end_b.departure && !opposite.runways[0].end_b.landing,
            &all_passed);

  EhamOperationalState state = EhamOperationalState{};
  checkTrue("bogus heading does not mutate state",
            !applyLanding(state, "99Z") && !state.runways[0].end_a.landing, &all_passed);

  Serial.printf("[eham_runways self-test] %s\n", all_passed ? "ALL PASS" : "SOME FAILED");
}

#endif  // EHAM_ENABLE_FIXTURES

}  // namespace data::eham_runways
