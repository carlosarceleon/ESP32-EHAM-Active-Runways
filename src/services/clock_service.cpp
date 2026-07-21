#include "services/clock_service.h"

#include <Arduino.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(EHAM_ENABLE_FIXTURES)
#include <cstdlib>
#endif

namespace services::clock {

namespace {

// Europe/Amsterdam: CET in winter, CEST (UTC+2) from last Sunday in March to
// last Sunday in October.
constexpr char kTzAmsterdam[] = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr char kNtpServerPrimary[] = "pool.ntp.org";
constexpr char kNtpServerSecondary[] = "time.nist.gov";

// 2020-01-01T00:00:00Z — anything before this means NTP hasn't landed yet.
constexpr std::time_t kMinPlausibleEpoch = 1577836800;

bool g_clock_valid = false;

/** Days since 1970-01-01 for a proleptic-Gregorian UTC calendar date (Howard Hinnant's algorithm). */
int64_t daysFromCivil(int64_t y, int64_t m, int64_t d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

int daysInMonth(int y, int m) {
  constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && isLeapYear(y)) return 29;
  return kDays[m - 1];
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

}  // namespace

void beginClockSync() {
  configTzTime(kTzAmsterdam, kNtpServerPrimary, kNtpServerSecondary);
}

void clockLoop() {
  if (!g_clock_valid && time(nullptr) >= kMinPlausibleEpoch) {
    g_clock_valid = true;
    Serial.println("Clock synced via NTP");
  }
}

bool clockIsValid() { return g_clock_valid; }

std::time_t nowUtc() { return time(nullptr); }

bool formatLocalDate(char* out, size_t out_len) {
  if (!g_clock_valid || out == nullptr || out_len == 0) return false;
  const std::time_t now = time(nullptr);
  struct tm local_tm {};
  localtime_r(&now, &local_tm);
  return std::strftime(out, out_len, "%Y-%m-%d", &local_tm) != 0;
}

bool formatLocalTime(char* out, size_t out_len) {
  if (!g_clock_valid || out == nullptr || out_len == 0) return false;
  const std::time_t now = time(nullptr);
  struct tm local_tm {};
  localtime_r(&now, &local_tm);
  return std::strftime(out, out_len, "%H:%M", &local_tm) != 0;
}

bool parseIso8601ToUtc(const char* text, std::time_t* out) {
  if (text == nullptr || out == nullptr) return false;

  const size_t len = std::strlen(text);
  // Minimum: "YYYY-MM-DDTHH:MM:SS" (19 chars).
  if (len < 19) return false;

  for (size_t i : {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18}) {
    if (!isDigit(text[i])) return false;
  }
  if (text[4] != '-' || text[7] != '-' || text[13] != ':' || text[16] != ':') return false;
  if (text[10] != 'T' && text[10] != ' ') return false;

  const int year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 + (text[2] - '0') * 10 +
                    (text[3] - '0');
  const int month = (text[5] - '0') * 10 + (text[6] - '0');
  const int day = (text[8] - '0') * 10 + (text[9] - '0');
  const int hour = (text[11] - '0') * 10 + (text[12] - '0');
  const int minute = (text[14] - '0') * 10 + (text[15] - '0');
  const int second = (text[17] - '0') * 10 + (text[18] - '0');

  if (month < 1 || month > 12) return false;
  if (day < 1 || day > daysInMonth(year, month)) return false;
  if (hour > 23) return false;
  if (minute > 59) return false;
  if (second > 59) return false;

  size_t idx = 19;
  if (idx < len && text[idx] == '.') {
    idx++;
    const size_t frac_start = idx;
    while (idx < len && isDigit(text[idx])) idx++;
    if (idx == frac_start) return false;  // "." with no digits
  }

  if (idx >= len) return false;

  int offset_minutes = 0;
  if (text[idx] == 'Z') {
    idx++;
  } else if (text[idx] == '+' || text[idx] == '-') {
    const int sign = text[idx] == '-' ? -1 : 1;
    idx++;
    if (len - idx != 5 || !isDigit(text[idx]) || !isDigit(text[idx + 1]) ||
        text[idx + 2] != ':' || !isDigit(text[idx + 3]) || !isDigit(text[idx + 4])) {
      return false;
    }
    const int offset_hours = (text[idx] - '0') * 10 + (text[idx + 1] - '0');
    const int offset_mins = (text[idx + 3] - '0') * 10 + (text[idx + 4] - '0');
    if (offset_hours > 23 || offset_mins > 59) return false;
    idx += 5;
    offset_minutes = sign * (offset_hours * 60 + offset_mins);
  } else {
    return false;
  }

  if (idx != len) return false;  // trailing garbage

  const int64_t days = daysFromCivil(year, month, day);
  const int64_t utc_seconds =
      days * 86400LL + hour * 3600LL + minute * 60LL + second - offset_minutes * 60LL;
  *out = static_cast<std::time_t>(utc_seconds);
  return true;
}

#if defined(EHAM_ENABLE_FIXTURES)

namespace {

void checkTrue(const char* label, bool condition, bool* all_passed) {
  Serial.printf("[clock_service self-test] %s: %s\n", label, condition ? "PASS" : "FAIL");
  if (!condition) {
    *all_passed = false;
  }
}

/** Keep in sync with fixtures/timestamps.txt (cross-checked by tools/validate_fixtures.py). */
void checkParsesTo(const char* label, const char* text, std::time_t expected, bool* all_passed) {
  std::time_t out = 0;
  const bool ok = parseIso8601ToUtc(text, &out);
  checkTrue(label, ok && out == expected, all_passed);
}

void checkRejected(const char* label, const char* text, bool* all_passed) {
  std::time_t out = 0;
  checkTrue(label, !parseIso8601ToUtc(text, &out), all_passed);
}

}  // namespace

void runClockSelfTest() {
  bool all_passed = true;

  // UTC, "Z" suffix.
  checkParsesTo("UTC Z suffix", "2026-01-15T12:00:00Z", 1768478400, &all_passed);
  // Explicit +00:00 offset, same instant as above.
  checkParsesTo("explicit +00:00 offset", "2026-01-15T12:00:00+00:00", 1768478400, &all_passed);
  // CET (UTC+1), winter.
  checkParsesTo("CET +01:00 winter", "2026-01-15T13:00:00+01:00", 1768478400, &all_passed);
  // CEST (UTC+2), summer.
  checkParsesTo("CEST +02:00 summer", "2026-07-15T14:00:00+02:00", 1784116800, &all_passed);
  // Negative offset.
  checkParsesTo("negative offset", "2026-01-15T07:00:00-05:00", 1768478400, &all_passed);
  // Fractional seconds are accepted and ignored.
  checkParsesTo("fractional seconds", "2026-01-15T12:00:00.123Z", 1768478400, &all_passed);
  // Year rollover / leap day.
  checkParsesTo("leap day", "2024-02-29T00:00:00Z", 1709164800, &all_passed);
  // New Year rollover across midnight.
  checkParsesTo("year rollover", "2026-01-01T00:00:00Z", 1767225600, &all_passed);

  checkRejected("rejects missing timezone", "2026-01-15T12:00:00", &all_passed);
  checkRejected("rejects bad month", "2026-13-01T00:00:00Z", &all_passed);
  checkRejected("rejects Feb 29 on non-leap year", "2026-02-29T00:00:00Z", &all_passed);
  checkRejected("rejects garbage", "not-a-timestamp", &all_passed);
  checkRejected("rejects truncated string", "2026-01-15T12:00", &all_passed);
  checkRejected("rejects malformed offset", "2026-01-15T12:00:00+0100", &all_passed);

  Serial.printf("[clock_service self-test] %s\n", all_passed ? "ALL PASS" : "SOME FAILED");
}

#endif  // defined(EHAM_ENABLE_FIXTURES)

}  // namespace services::clock
