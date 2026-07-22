#include "services/metar_parser.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace services::metar {

namespace {

bool isAllDigits(const char* s, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (!isdigit(static_cast<unsigned char>(s[i]))) return false;
  }
  return true;
}

int atoiN(const char* s, size_t len) {
  int v = 0;
  for (size_t i = 0; i < len; ++i) v = v * 10 + (s[i] - '0');
  return v;
}

bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

int daysInMonth(int y, int m) {
  static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && isLeapYear(y)) return 29;
  return kDays[m - 1];
}

/** Days since the Unix epoch for a UTC civil date (Howard Hinnant's algorithm). */
int64_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

std::time_t timeForYmdHm(int y, int m, int d, int hh, int mm) {
  const int64_t days = daysFromCivil(y, m, d);
  return static_cast<std::time_t>(days * 86400LL + hh * 3600LL + mm * 60LL);
}

/**
 * Resolves a bare "day of month, hour, minute" observation time against
 * `now_utc`'s year/month, trying the current month plus its neighbors so a
 * report published just before/after a month boundary still resolves to the
 * correct calendar date. Prefers the most recent candidate that isn't more
 * than 5 minutes in the future (clock skew tolerance); falls back to the
 * closest candidate overall if every one is further in the future than that.
 */
bool inferObservationTime(int dd, int hh, int mm, std::time_t now_utc, std::time_t* out) {
  struct tm now_tm {};
  gmtime_r(&now_utc, &now_tm);
  const int year = now_tm.tm_year + 1900;
  const int month = now_tm.tm_mon + 1;

  bool have_candidate = false;
  std::time_t best_candidate = 0;
  int64_t best_score = 0;
  bool best_is_valid_past = false;

  for (int delta = -1; delta <= 1; ++delta) {
    int m2 = month + delta;
    int y2 = year;
    if (m2 < 1) {
      m2 += 12;
      y2 -= 1;
    } else if (m2 > 12) {
      m2 -= 12;
      y2 += 1;
    }
    if (dd < 1 || dd > daysInMonth(y2, m2)) continue;

    const std::time_t candidate = timeForYmdHm(y2, m2, dd, hh, mm);
    const int64_t diff = static_cast<int64_t>(now_utc) - static_cast<int64_t>(candidate);
    const bool valid_past = diff >= -300;
    const int64_t score = diff >= 0 ? diff : -diff;

    if (!have_candidate || (valid_past && !best_is_valid_past) ||
        (valid_past == best_is_valid_past && score < best_score)) {
      have_candidate = true;
      best_candidate = candidate;
      best_score = score;
      best_is_valid_past = valid_past;
    }
  }

  if (!have_candidate) return false;
  *out = best_candidate;
  return true;
}

/** "34004KT" / "27015G25KT" / "VRB03KT" / "00000KT". */
bool parseWindGroup(const char* tok, EhamWeather* w) {
  const size_t len = strlen(tok);
  if (len < 7 || len > 12 || strcmp(tok + len - 2, "KT") != 0) return false;

  char body[16];
  const size_t body_len = len - 2;
  if (body_len >= sizeof(body)) return false;
  memcpy(body, tok, body_len);
  body[body_len] = '\0';

  int gust = -1;
  char* gpos = strchr(body, 'G');
  if (gpos != nullptr) {
    const char* gdigits = gpos + 1;
    const size_t glen = strlen(gdigits);
    if (glen < 2 || glen > 3 || !isAllDigits(gdigits, glen)) return false;
    gust = atoiN(gdigits, glen);
    *gpos = '\0';
  }

  bool variable = false;
  int direction = 0;
  const char* speed_start;
  if (strncmp(body, "VRB", 3) == 0) {
    variable = true;
    speed_start = body + 3;
  } else {
    if (strlen(body) < 5 || !isAllDigits(body, 3)) return false;
    direction = atoiN(body, 3);
    if (direction > 360) return false;
    speed_start = body + 3;
  }

  const size_t speed_len = strlen(speed_start);
  if (speed_len < 2 || speed_len > 3 || !isAllDigits(speed_start, speed_len)) return false;
  const int speed = atoiN(speed_start, speed_len);

  w->variable_wind = variable;
  w->wind_direction_deg = static_cast<uint16_t>(direction);
  w->wind_speed_kt = static_cast<uint16_t>(speed);
  w->has_gust = gust >= 0;
  w->gust_speed_kt = gust >= 0 ? static_cast<uint16_t>(gust) : 0;
  w->calm_wind = (!variable && direction == 0 && speed == 0);
  return true;
}

/** "17/13" / "M02/M05" / "17/M03". Dewpoint shape is validated but discarded. */
bool parseTempGroup(const char* tok, EhamWeather* w) {
  const char* slash = strchr(tok, '/');
  if (slash == nullptr) return false;

  const bool neg = tok[0] == 'M';
  const char* digits = neg ? tok + 1 : tok;
  const size_t dlen = static_cast<size_t>(slash - digits);
  if (dlen != 2 || !isAllDigits(digits, dlen)) return false;

  const char* dew = slash + 1;
  const bool dew_neg = dew[0] == 'M';
  const char* dew_digits = dew_neg ? dew + 1 : dew;
  const size_t dew_len = strlen(dew_digits);
  if (dew_len != 2 || !isAllDigits(dew_digits, dew_len)) return false;

  int t = atoiN(digits, 2);
  if (neg) t = -t;
  w->temperature_c = static_cast<int8_t>(t);
  return true;
}

}  // namespace

bool extractTacComment(const char* body, size_t len, char* out, size_t out_len) {
  if (body == nullptr || out == nullptr || out_len == 0) return false;

  for (size_t i = 0; i + 4 <= len; ++i) {
    if (memcmp(body + i, "<!--", 4) != 0) continue;

    size_t j = i + 4;
    while (j < len && isspace(static_cast<unsigned char>(body[j]))) ++j;
    if (j + 5 > len) continue;
    if (memcmp(body + j, "METAR", 5) != 0 && memcmp(body + j, "SPECI", 5) != 0) continue;

    size_t end = 0;
    for (size_t k = j; k + 3 <= len; ++k) {
      if (memcmp(body + k, "-->", 3) == 0) {
        end = k;
        break;
      }
    }
    if (end == 0) continue;

    size_t oi = 0;
    bool last_space = false;
    for (size_t p = j; p < end && oi + 1 < out_len; ++p) {
      const char c = body[p];
      if (isspace(static_cast<unsigned char>(c))) {
        if (!last_space && oi > 0) {
          out[oi++] = ' ';
          last_space = true;
        }
      } else {
        out[oi++] = c;
        last_space = false;
      }
    }
    while (oi > 0 && out[oi - 1] == ' ') --oi;
    out[oi] = '\0';
    return oi > 0;
  }
  return false;
}

ParseResult parseTacLine(const char* tac, std::time_t now_utc, EhamWeather* out) {
  if (tac == nullptr || out == nullptr) return ParseResult::MalformedReport;

  char buf[220];
  strncpy(buf, tac, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char* saveptr = nullptr;
  char* tok = strtok_r(buf, " ", &saveptr);
  if (tok == nullptr || (strcmp(tok, "METAR") != 0 && strcmp(tok, "SPECI") != 0)) {
    return ParseResult::MalformedReport;
  }

  tok = strtok_r(nullptr, " ", &saveptr);
  if (tok == nullptr) return ParseResult::MalformedReport;
  if (strcmp(tok, "EHAM") != 0) return ParseResult::NoEhamReport;

  tok = strtok_r(nullptr, " ", &saveptr);
  if (tok == nullptr || strlen(tok) != 7 || tok[6] != 'Z' || !isAllDigits(tok, 6)) {
    return ParseResult::MalformedReport;
  }
  const int dd = atoiN(tok, 2);
  const int hh = atoiN(tok + 2, 2);
  const int mi = atoiN(tok + 4, 2);
  if (dd < 1 || dd > 31 || hh > 23 || mi > 59) return ParseResult::MalformedReport;

  std::time_t observed = 0;
  if (!inferObservationTime(dd, hh, mi, now_utc, &observed)) return ParseResult::MalformedReport;
  if (observed - now_utc > 300) return ParseResult::MalformedReport;
  if (now_utc - observed > kMaxReportAgeSec) return ParseResult::Stale;

  EhamWeather w{};
  w.observed_at_utc = observed;
  bool have_wind = false;
  bool have_temp = false;

  while ((tok = strtok_r(nullptr, " ", &saveptr)) != nullptr) {
    const size_t tlen = strlen(tok);
    if (!have_wind && tlen >= 7 && strcmp(tok + tlen - 2, "KT") == 0) {
      if (!parseWindGroup(tok, &w)) return ParseResult::MalformedReport;
      have_wind = true;
      continue;
    }
    if (have_wind && !w.variable_wind && tlen == 7 && tok[3] == 'V' && isAllDigits(tok, 3) &&
        isAllDigits(tok + 4, 3)) {
      w.variable_wind = true;
      continue;
    }
    if (!have_temp && parseTempGroup(tok, &w)) {
      have_temp = true;
      continue;
    }
  }

  if (!have_wind || !have_temp) return ParseResult::MalformedReport;

  w.available = true;
  *out = w;
  return ParseResult::Ok;
}

ParseResult parseKnmiMetarFile(const char* body, size_t len, std::time_t now_utc, EhamWeather* out) {
  char tac[220];
  if (!extractTacComment(body, len, tac, sizeof(tac))) return ParseResult::NoEhamReport;
  return parseTacLine(tac, now_utc, out);
}

#if defined(EHAM_ENABLE_FIXTURES)

namespace {

void expect(const char* name, bool ok) {
  Serial.printf("metar_parser self-test: %s: %s\n", name, ok ? "PASS" : "FAIL");
}

}  // namespace

void runMetarParserSelfTest() {
  // "now" chosen to sit just after each fixture's observation time.
  const std::time_t kNow = timeForYmdHm(2026, 7, 21, 22, 0);

  {
    EhamWeather w{};
    const ParseResult r = parseTacLine("METAR EHAM 212155Z 34004KT 9999 FEW015 SCT026 BKN037 17/13 Q1025 NOSIG=",
                                        kNow, &w);
    expect("standard METAR", r == ParseResult::Ok && w.wind_direction_deg == 340 && w.wind_speed_kt == 4 &&
                                  !w.has_gust && !w.variable_wind && !w.calm_wind && w.temperature_c == 17);
  }
  {
    EhamWeather w{};
    const ParseResult r =
        parseTacLine("SPECI EHAM 212230Z 25018G32KT 220V280 9999 SCT025 BKN035 14/09 Q1008 NOSIG=", kNow, &w);
    expect("SPECI with gust and variable direction",
           r == ParseResult::Ok && w.wind_direction_deg == 250 && w.wind_speed_kt == 18 && w.has_gust &&
               w.gust_speed_kt == 32 && w.variable_wind && w.temperature_c == 14);
  }
  {
    EhamWeather w{};
    const ParseResult r = parseTacLine("METAR EHAM 212200Z VRB03KT 9999 FEW020 22/15 Q1015 NOSIG=", kNow, &w);
    expect("variable (VRB) wind",
           r == ParseResult::Ok && w.variable_wind && w.wind_speed_kt == 3 && w.temperature_c == 22);
  }
  {
    EhamWeather w{};
    const ParseResult r = parseTacLine("METAR EHAM 212200Z 00000KT 9999 FEW020 08/M02 Q1030 NOSIG=", kNow, &w);
    expect("calm wind and negative temperature",
           r == ParseResult::Ok && w.calm_wind && w.wind_speed_kt == 0 && w.temperature_c == 8);
  }
  {
    // Observation on the last day of the previous month, "now" just after a month rollover.
    const std::time_t now_after_rollover = timeForYmdHm(2026, 8, 1, 0, 30);
    EhamWeather w{};
    const ParseResult r =
        parseTacLine("METAR EHAM 312350Z 18010KT 9999 FEW020 20/14 Q1012 NOSIG=", now_after_rollover, &w);
    expect("month-boundary inference", r == ParseResult::Ok && w.wind_direction_deg == 180);
  }
  {
    EhamWeather w{};
    const ParseResult r =
        parseTacLine("METAR EHRD 212155Z AUTO 28001KT 9999 FEW032 15/13 Q1026 NOSIG=", kNow, &w);
    expect("non-EHAM station rejected", r == ParseResult::NoEhamReport);
  }
  {
    EhamWeather w{};
    const ParseResult r = parseTacLine("METAR EHAM garbled report text", kNow, &w);
    expect("malformed report rejected", r == ParseResult::MalformedReport);
  }
  {
    EhamWeather w{};
    const std::time_t two_hours_ago = kNow - 2 * 3600;
    char stale_tac[96];
    struct tm tm_stale {};
    gmtime_r(&two_hours_ago, &tm_stale);
    snprintf(stale_tac, sizeof(stale_tac), "METAR EHAM %02d%02d%02dZ 18010KT 9999 FEW020 20/14 Q1012 NOSIG=",
             tm_stale.tm_mday, tm_stale.tm_hour, tm_stale.tm_min);
    const ParseResult r = parseTacLine(stale_tac, kNow, &w);
    expect("report older than 90 minutes rejected", r == ParseResult::Stale);
  }
  {
    const char kWrapped[] =
        "0000350701\r\r\nLANL80 EHRD 212155\r\r\n<?xml version=\"1.0\" ?>\n"
        "<!-- METAR EHRD 212155Z AUTO 28001KT 9999 FEW032 15/13 Q1026 NOSIG= -->\n"
        "<iwxxm:METAR/>";
    EhamWeather w{};
    const ParseResult r = parseKnmiMetarFile(kWrapped, sizeof(kWrapped) - 1, kNow, &w);
    expect("TAC extraction from wrapped IWXXM body", r == ParseResult::NoEhamReport);
  }
}

#endif  // defined(EHAM_ENABLE_FIXTURES)

}  // namespace services::metar
