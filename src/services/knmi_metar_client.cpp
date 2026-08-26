#include "services/knmi_metar_client.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <cstring>

#include "config.h"
#include "services/clock_service.h"
#include "services/metar_parser.h"

namespace services::metar_client {

namespace {

EhamWeather s_weather{};
bool s_state_changed = false;

bool s_have_attempted_once = false;
unsigned long s_last_attempt_ms = 0;
unsigned long s_jitter_ms = 0;
bool s_jitter_computed = false;

struct HttpResult {
  int code = -1;
  String body;
};

HttpResult httpsGet(const char* url, size_t max_bytes) {
  HttpResult result;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("metar: http.begin failed");
    return result;
  }
  http.setConnectTimeout(static_cast<int>(config::kHttpConnectTimeoutMs));
  http.setTimeout(config::kHttpTotalTimeoutMs);
  http.addHeader("User-Agent", config::kHttpUserAgent);

  result.code = http.GET();
  if (result.code != HTTP_CODE_OK) {
    Serial.printf("metar: HTTP %d for %s\n", result.code, url);
    http.end();
    return result;
  }

  const int content_length = http.getSize();
  if (content_length > static_cast<int>(max_bytes)) {
    Serial.println("metar: response too large");
    http.end();
    result.code = -1;
    return result;
  }

  result.body = http.getString();
  http.end();

  if (result.body.length() == 0 || result.body.length() > max_bytes) {
    Serial.println("metar: empty or oversized body");
    result.code = -1;
    result.body = "";
  }
  return result;
}

bool parseAwcMetarBody(const String& body, EhamWeather* out) {
  const char* cursor = body.c_str();
  const std::time_t now_utc = services::clock::nowUtc();
  char line[220];

  while (*cursor != '\0') {
    const char* end = strchr(cursor, '\n');
    size_t len = end == nullptr ? strlen(cursor) : static_cast<size_t>(end - cursor);
    while (len > 0 && (cursor[len - 1] == '\r' || cursor[len - 1] == ' ' || cursor[len - 1] == '\t')) {
      --len;
    }

    if (len > 0 && len < sizeof(line)) {
      memcpy(line, cursor, len);
      line[len] = '\0';
      if (services::metar::parseTacLine(line, now_utc, out) == services::metar::ParseResult::Ok) {
        return true;
      }
    }

    if (end == nullptr) break;
    cursor = end + 1;
  }
  return false;
}

void refreshOnce() {
  if (s_weather.available) {
    s_weather = EhamWeather{};
    s_state_changed = true;
  }

  const HttpResult result = httpsGet(config::kAwcMetarUrl, config::kAwcMetarMaxBytes);
  if (result.code != HTTP_CODE_OK) return;

  EhamWeather parsed{};
  if (!parseAwcMetarBody(result.body, &parsed)) {
    Serial.println("metar: AWC response did not yield a usable EHAM report");
    return;
  }

  s_weather = parsed;
  s_state_changed = true;
  Serial.println("metar: weather updated from AWC");
}

unsigned long jitterMs() {
  if (!s_jitter_computed) {
    const uint64_t chip_id = ESP.getEfuseMac();
    s_jitter_ms = static_cast<unsigned long>(chip_id % config::kMetarJitterRangeMs);
    s_jitter_computed = true;
  }
  return s_jitter_ms;
}

}  // namespace

void metarLoop() {
  if (!services::clock::clockIsValid()) return;

  const unsigned long now_ms = millis();
  const unsigned long interval_ms = config::kMetarFetchIntervalMs + jitterMs();
  const bool due =
      !s_have_attempted_once || static_cast<uint32_t>(now_ms - s_last_attempt_ms) >= interval_ms;
  if (!due) return;

  s_have_attempted_once = true;
  s_last_attempt_ms = now_ms;
  refreshOnce();
}

const EhamWeather& currentWeather() { return s_weather; }

bool consumeWeatherChanged() {
  const bool changed = s_state_changed;
  s_state_changed = false;
  return changed;
}

}  // namespace services::metar_client
