#pragma once

#include <cstddef>
#include <ctime>

namespace services::clock {

/** Starts NTP sync (Europe/Amsterdam, CET/CEST) after Wi-Fi is connected. Non-blocking. */
void beginClockSync();

/** Call every loop() iteration; tracks whether NTP has produced a plausible time. */
void clockLoop();

/** True once the system clock holds a plausible (post-2020) time. */
bool clockIsValid();

/** Current UTC time, seconds since epoch. Meaningless if !clockIsValid(). */
std::time_t nowUtc();

/** Writes "YYYY-MM-DD" (local, Europe/Amsterdam) into `out`. False if clock isn't valid yet. */
bool formatLocalDate(char* out, size_t out_len);

/** Writes "HH:MM" (local, Europe/Amsterdam) into `out`. False if clock isn't valid yet. */
bool formatLocalTime(char* out, size_t out_len);

/**
 * Parses an ISO-8601 timestamp ("YYYY-MM-DDTHH:MM:SS[.fff](Z|+HH:MM|-HH:MM)")
 * into UTC seconds-since-epoch. Pure function — no dependency on the system
 * clock or locale. Returns false for malformed or out-of-range input.
 */
bool parseIso8601ToUtc(const char* text, std::time_t* out);

#if defined(EHAM_ENABLE_FIXTURES)
/** Runs the parser fixtures below and prints PASS/FAIL to Serial. */
void runClockSelfTest();
#endif

}  // namespace services::clock
