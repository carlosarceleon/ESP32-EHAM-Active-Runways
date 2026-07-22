#include "services/knmi_metar_client.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <cstring>

#include "config.h"
#include "services/clock_service.h"
#include "services/knmi_token_manager.h"
#include "services/metar_parser.h"

namespace services::metar_client {

namespace {

EhamWeather s_weather{};
bool s_state_changed = false;

bool s_have_attempted_once = false;
unsigned long s_last_attempt_ms = 0;
unsigned long s_jitter_ms = 0;
bool s_jitter_computed = false;

enum class WorkflowResult {
  Ok,
  AuthError,
  OtherFailure,
};

struct HttpResult {
  int code = -1;
  String body;
};

/** Blocking GET with a hard body-size limit. `authorization` is omitted entirely when null. */
HttpResult httpsGet(const char* url, const char* authorization, size_t max_bytes) {
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
  if (authorization != nullptr) {
    http.addHeader("Authorization", authorization);
  }

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

/** Percent-encodes a filename for use as a URL path segment. */
String urlEncodePathSegment(const char* filename) {
  static const char kHex[] = "0123456789ABCDEF";
  String out;
  out.reserve(strlen(filename) * 3);
  for (const char* p = filename; *p != '\0'; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    const bool unreserved = isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
    if (unreserved) {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += kHex[(c >> 4) & 0xF];
      out += kHex[c & 0xF];
    }
  }
  return out;
}

/** Finds the most recently created filename containing "EHAM" in a KNMI file-list response. */
bool findLatestEhamFilename(const String& list_body, char* out, size_t out_len) {
  JsonDocument doc;
  if (deserializeJson(doc, list_body)) {
    Serial.println("metar: file list is not valid JSON");
    return false;
  }
  JsonArray files = doc["files"].as<JsonArray>();
  for (JsonObject file : files) {
    const char* filename = file["filename"] | "";
    if (strstr(filename, "EHAM") != nullptr) {
      if (strlen(filename) >= out_len) return false;
      strcpy(out, filename);
      return true;
    }
  }
  return false;
}

bool extractDownloadUrl(const String& url_body, String* out) {
  JsonDocument doc;
  if (deserializeJson(doc, url_body)) {
    Serial.println("metar: file-url response is not valid JSON");
    return false;
  }
  const char* download_url = doc["temporaryDownloadUrl"] | "";
  if (strlen(download_url) == 0) return false;
  *out = download_url;
  return true;
}

bool isAuthError(int http_code) { return http_code == 401 || http_code == 403; }

WorkflowResult runWeatherWorkflowOnce(const char* token, EhamWeather* out) {
  const String list_url = String(config::kKnmiApiBase) + config::kKnmiMetarFileListPath;
  const HttpResult list_result = httpsGet(list_url.c_str(), token, config::kKnmiMetarListMaxBytes);
  if (list_result.code != HTTP_CODE_OK) {
    return isAuthError(list_result.code) ? WorkflowResult::AuthError : WorkflowResult::OtherFailure;
  }

  char filename[160];
  if (!findLatestEhamFilename(list_result.body, filename, sizeof(filename))) {
    Serial.println("metar: no EHAM file in the latest listing");
    return WorkflowResult::OtherFailure;
  }

  const String file_url_url = String(config::kKnmiApiBase) + config::kKnmiMetarFileUrlPathPrefix +
                               urlEncodePathSegment(filename) + config::kKnmiMetarFileUrlPathSuffix;
  const HttpResult url_result = httpsGet(file_url_url.c_str(), token, config::kKnmiMetarUrlMaxBytes);
  if (url_result.code != HTTP_CODE_OK) {
    return isAuthError(url_result.code) ? WorkflowResult::AuthError : WorkflowResult::OtherFailure;
  }

  String download_url;
  if (!extractDownloadUrl(url_result.body, &download_url)) {
    return WorkflowResult::OtherFailure;
  }

  // The temporary download URL is a different, unrelated host with its own signed query
  // string -- never forward the KNMI Authorization header to it.
  const HttpResult file_result =
      httpsGet(download_url.c_str(), /*authorization=*/nullptr, config::kKnmiMetarFileMaxBytes);
  if (file_result.code != HTTP_CODE_OK) {
    return WorkflowResult::OtherFailure;
  }

  const services::metar::ParseResult parse_result = services::metar::parseKnmiMetarFile(
      file_result.body.c_str(), file_result.body.length(), services::clock::nowUtc(), out);
  if (parse_result != services::metar::ParseResult::Ok) {
    Serial.println("metar: downloaded file did not yield a usable EHAM report");
    return WorkflowResult::OtherFailure;
  }
  return WorkflowResult::Ok;
}

void refreshOnce() {
  if (s_weather.available) {
    s_weather = EhamWeather{};
    s_state_changed = true;
  }

  if (!services::knmi_token::knmiTokenAvailable()) {
    return;
  }

  EhamWeather parsed{};
  WorkflowResult result = runWeatherWorkflowOnce(services::knmi_token::knmiToken(), &parsed);

  if (result == WorkflowResult::AuthError) {
    Serial.println("metar: KNMI rejected the token, forcing a manifest refresh and retrying once");
    services::knmi_token::knmiTokenForceRefresh();
    services::knmi_token::knmiTokenLoop();
    if (services::knmi_token::knmiTokenAvailable()) {
      result = runWeatherWorkflowOnce(services::knmi_token::knmiToken(), &parsed);
    }
  }

  if (result == WorkflowResult::Ok) {
    s_weather = parsed;
    s_state_changed = true;
    Serial.println("metar: weather updated");
  }
}

unsigned long jitterMs() {
  if (!s_jitter_computed) {
    const uint64_t chip_id = ESP.getEfuseMac();
    s_jitter_ms = static_cast<unsigned long>(chip_id % config::kKnmiMetarJitterRangeMs);
    s_jitter_computed = true;
  }
  return s_jitter_ms;
}

}  // namespace

void metarLoop() {
  if (!services::clock::clockIsValid()) {
    return;
  }

  const unsigned long now_ms = millis();
  const unsigned long interval_ms = config::kKnmiMetarFetchIntervalMs + jitterMs();
  const bool due =
      !s_have_attempted_once || static_cast<uint32_t>(now_ms - s_last_attempt_ms) >= interval_ms;
  if (!due) {
    return;
  }

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
