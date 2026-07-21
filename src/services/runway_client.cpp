#include "services/runway_client.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <cstring>

#include "config.h"
#include "services/clock_service.h"
#include "services/runway_parser.h"

namespace services::runway {

namespace {

enum class FetchOutcome {
  Success,
  HttpFailure,
  MalformedJson,
  NoAcceptableSlot,
};

EhamOperationalState s_state{};
bool s_state_changed = false;
bool s_force_refresh = false;
bool s_have_fetched_once = false;
unsigned long s_last_attempt_ms = 0;
uint8_t s_consecutive_failures = 0;
bool s_have_ever_succeeded = false;
std::time_t s_last_success_utc = 0;

unsigned long currentIntervalMs() {
  if (s_consecutive_failures == 0) {
    return config::kRunwayFetchIntervalMs;
  }
  if (s_consecutive_failures >= config::kRunwayFailuresBeforeSlowBackoff) {
    return config::kRunwaySlowRetryIntervalMs;
  }
  return config::kRunwayRetryIntervalMs;
}

/** Local calendar date one day before now, formatted "YYYY-MM-DD". Relies on
 * the TZ environment already being configured by services::clock. */
bool previousLocalDate(char* out, size_t out_len) {
  const std::time_t yesterday = services::clock::nowUtc() - 24 * 60 * 60;
  struct tm local_tm {};
  localtime_r(&yesterday, &local_tm);
  return strftime(out, out_len, "%Y-%m-%d", &local_tm) > 0;
}

FetchOutcome fetchOnce(const char* date_str, EhamOperationalState* out, std::time_t* slot_from,
                        std::time_t* slot_until) {
  String url = String(config::kRunwayApiUrl) + "?date=" + date_str;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("runway: http.begin failed");
    return FetchOutcome::HttpFailure;
  }

  http.setConnectTimeout(static_cast<int>(config::kHttpConnectTimeoutMs));
  http.setTimeout(config::kHttpTotalTimeoutMs);
  http.addHeader("User-Agent", config::kHttpUserAgent);

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("runway: HTTP %d\n", code);
    http.end();
    return FetchOutcome::HttpFailure;
  }

  const int content_length = http.getSize();
  if (content_length > static_cast<int>(config::kRunwayMaxBodyBytes)) {
    Serial.printf("runway: response too large (%d bytes)\n", content_length);
    http.end();
    return FetchOutcome::HttpFailure;
  }

  const String payload = http.getString();
  http.end();

  if (payload.length() == 0 || payload.length() > config::kRunwayMaxBodyBytes) {
    Serial.println("runway: empty or oversized body");
    return FetchOutcome::HttpFailure;
  }

  const ParseResult result = parseRunwayResponse(payload.c_str(), payload.length(),
                                                  services::clock::nowUtc(), out, slot_from,
                                                  slot_until);
  switch (result) {
    case ParseResult::Ok:
      return FetchOutcome::Success;
    case ParseResult::MalformedJson:
      Serial.println("runway: malformed JSON response");
      return FetchOutcome::MalformedJson;
    case ParseResult::NoAcceptableSlot:
      return FetchOutcome::NoAcceptableSlot;
  }
  return FetchOutcome::MalformedJson;
}

bool fetchAndParse(EhamOperationalState* out, std::time_t* slot_from, std::time_t* slot_until) {
  char date_str[11];
  if (!services::clock::formatLocalDate(date_str, sizeof(date_str))) {
    Serial.println("runway: clock not valid yet, skipping fetch");
    return false;
  }

  const FetchOutcome outcome = fetchOnce(date_str, out, slot_from, slot_until);
  if (outcome == FetchOutcome::Success) {
    return true;
  }

  if (outcome == FetchOutcome::NoAcceptableSlot) {
    char local_time[6];
    const bool near_midnight = services::clock::formatLocalTime(local_time, sizeof(local_time)) &&
                                strncmp(local_time, "00:", 3) == 0;
    if (near_midnight) {
      char prev_date[11];
      if (previousLocalDate(prev_date, sizeof(prev_date))) {
        Serial.printf("runway: no acceptable slot for %s near midnight, retrying %s\n", date_str,
                      prev_date);
        return fetchOnce(prev_date, out, slot_from, slot_until) == FetchOutcome::Success;
      }
    }
  }
  return false;
}

void applyFreshnessLimit() {
  if (!s_have_ever_succeeded || !s_state.runway_data_available) {
    return;
  }
  const std::time_t now = services::clock::nowUtc();
  if (now - s_last_success_utc > config::kRunwayFreshnessLimitSec) {
    Serial.println("runway: last fetch exceeded freshness limit, clearing live state");
    s_state = EhamOperationalState{};
    s_state.runway_data_stale = true;
    s_state_changed = true;
  }
}

}  // namespace

void runwayLoop() {
  if (!services::clock::clockIsValid()) {
    return;
  }

  const unsigned long now_ms = millis();
  const bool due = s_force_refresh || !s_have_fetched_once ||
                    static_cast<uint32_t>(now_ms - s_last_attempt_ms) >= currentIntervalMs();

  if (!due) {
    applyFreshnessLimit();
    return;
  }

  s_force_refresh = false;
  s_have_fetched_once = true;
  s_last_attempt_ms = now_ms;

  EhamOperationalState fetched{};
  std::time_t slot_from = 0;
  std::time_t slot_until = 0;

  if (fetchAndParse(&fetched, &slot_from, &slot_until)) {
    s_consecutive_failures = 0;
    s_have_ever_succeeded = true;
    s_last_success_utc = services::clock::nowUtc();

    fetched.runway_data_available = true;
    fetched.runway_data_stale = false;
    fetched.runway_observed_at_utc = slot_from;
    fetched.runway_fetched_at_utc = s_last_success_utc;

    s_state = fetched;
    s_state_changed = true;
    Serial.printf("runway: applied slot %ld..%ld (fetched %ld)\n",
                  static_cast<long>(slot_from), static_cast<long>(slot_until),
                  static_cast<long>(s_last_success_utc));
  } else {
    ++s_consecutive_failures;
    Serial.printf("runway: fetch failed (%u consecutive)\n",
                  static_cast<unsigned>(s_consecutive_failures));
    applyFreshnessLimit();
  }
}

void forceRefresh() { s_force_refresh = true; }

const EhamOperationalState& currentState() { return s_state; }

bool consumeStateChanged() {
  const bool changed = s_state_changed;
  s_state_changed = false;
  return changed;
}

}  // namespace services::runway
