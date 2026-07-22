#pragma once

#include <cstddef>
#include <ctime>

#include "domain/eham_state.h"

namespace services::metar {

enum class ParseResult {
  Ok,
  NoEhamReport,
  MalformedReport,
  Stale,
};

/** Reject a report whose observation time is more than this far in the past. */
constexpr long kMaxReportAgeSec = 90 * 60;

/**
 * Extracts the first "<!-- METAR ... -->" / "<!-- SPECI ... -->" TAC comment
 * from a raw KNMI IWXXM file body, collapsing internal whitespace/newlines to
 * single spaces. Writes a null-terminated string into `out`. Returns false if
 * no such comment is present (e.g. truncated/malformed file) -- this is
 * distinct from "comment present but not an EHAM report", which is handled by
 * parseTacLine() returning ParseResult::NoEhamReport.
 */
bool extractTacComment(const char* body, size_t len, char* out, size_t out_len);

/**
 * Parses a bare TAC line, e.g. "METAR EHAM 212155Z 34004KT 9999 FEW015
 * SCT026 BKN037 17/13 Q1025 NOSIG=". Requires station EHAM; any other
 * station yields NoEhamReport. Infers the observation month from `now_utc`
 * (correctly across month boundaries) and rejects reports older than
 * kMaxReportAgeSec via ParseResult::Stale.
 */
ParseResult parseTacLine(const char* tac, std::time_t now_utc, EhamWeather* out);

/** Convenience: extractTacComment() + parseTacLine() over a raw KNMI file body. */
ParseResult parseKnmiMetarFile(const char* body, size_t len, std::time_t now_utc, EhamWeather* out);

#if defined(EHAM_ENABLE_FIXTURES)
/** Runs the parser fixtures below and prints PASS/FAIL to Serial. */
void runMetarParserSelfTest();
#endif

}  // namespace services::metar
