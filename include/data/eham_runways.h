#pragma once

#include <cstddef>

#include "domain/eham_state.h"

namespace data::eham_runways {

struct EhamRunwayDefinition {
  const char* designator;
  const char* name;
  const char* end_a;
  const char* end_b;
  float x1_pct;
  float y1_pct;
  float x2_pct;
  float y2_pct;
};

size_t runwayCount();
const EhamRunwayDefinition& runwayDefinition(size_t index);

/**
 * Normalizes a raw runway heading from an external source: trims whitespace,
 * uppercases, and maps the bare "18"/"36" aliases to Zwanenburgbaan ("18C"/
 * "36C") per the reference integration. Returns false if the result would not
 * fit `out_len`.
 */
bool normalizeHeading(const char* raw, char* out, size_t out_len);

/**
 * Looks up a normalized heading (see normalizeHeading) among the six EHAM
 * runway ends. Returns false for unknown identifiers.
 */
bool findRunwayEnd(const char* heading, size_t* runway_index, bool* is_end_a);

/** Normalizes `heading`, looks it up, and sets its landing flag if found. */
bool applyLanding(EhamOperationalState& state, const char* heading);

/** Normalizes `heading`, looks it up, and sets its departure flag if found. */
bool applyDeparture(EhamOperationalState& state, const char* heading);

#if defined(EHAM_ENABLE_FIXTURES)

EhamOperationalState fixtureAllInactive();
EhamOperationalState fixtureLanding18R();
EhamOperationalState fixtureDeparture24();
EhamOperationalState fixtureLanding18RDeparture36L();
EhamOperationalState fixtureLanding18CDeparture18C();
EhamOperationalState fixtureMultipleActive();

/** Runs the mapping/normalization assertions below and prints PASS/FAIL to Serial. */
void runSelfTest();

#endif  // EHAM_ENABLE_FIXTURES

}  // namespace data::eham_runways
