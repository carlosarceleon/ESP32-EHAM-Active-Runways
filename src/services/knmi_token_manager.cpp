#include "services/knmi_token_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>

#include <cstring>

#include "config.h"
#include "services/clock_service.h"

namespace services::knmi_token {

namespace {

constexpr char kNvsNamespace[] = "knmi";
constexpr char kKeyToken[] = "token";
constexpr char kKeyValidUntil[] = "valid_until";

constexpr size_t kTokenMaxLen = 512;
constexpr char kTokenAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.";

char s_token[kTokenMaxLen + 1] = "";
char s_valid_until[11] = "";  // "YYYY-MM-DD"
bool s_available = false;

bool s_force_refresh = false;
bool s_have_refreshed_once = false;
bool s_have_attempted_once = false;
unsigned long s_last_attempt_ms = 0;

/** "acce...9f2a" — never log a complete token. */
String redact(const char* token) {
  const size_t len = strlen(token);
  if (len <= 8) {
    return "***";
  }
  String out;
  out.reserve(13);
  out.concat(token, 4);
  out += "...";
  out += (token + len - 4);
  return out;
}

bool isTokenCharsetValid(const char* token) {
  for (const char* p = token; *p != '\0'; ++p) {
    if (strchr(kTokenAlphabet, *p) == nullptr) {
      return false;
    }
  }
  return true;
}

bool isIsoDate(const char* text) {
  // "YYYY-MM-DD", validated structurally only; string comparisons below rely
  // on this fixed width for correct chronological ordering.
  if (strlen(text) != 10 || text[4] != '-' || text[7] != '-') {
    return false;
  }
  for (int i = 0; i < 10; ++i) {
    if (i == 4 || i == 7) continue;
    if (!isdigit(static_cast<unsigned char>(text[i]))) return false;
  }
  return true;
}

void loadFromNvs() {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) {
    return;
  }
  const String token = prefs.getString(kKeyToken, "");
  const String valid_until = prefs.getString(kKeyValidUntil, "");
  prefs.end();

  if (token.length() == 0 || token.length() > kTokenMaxLen || !isIsoDate(valid_until.c_str())) {
    return;
  }
  strncpy(s_token, token.c_str(), kTokenMaxLen);
  s_token[kTokenMaxLen] = '\0';
  strncpy(s_valid_until, valid_until.c_str(), sizeof(s_valid_until) - 1);
  s_valid_until[sizeof(s_valid_until) - 1] = '\0';
  s_available = true;
  Serial.printf("knmi: loaded cached token from NVS: %s valid_until=%s\n", redact(s_token).c_str(),
                s_valid_until);
}

void saveToNvs(const char* token, const char* valid_until) {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) {
    Serial.println("knmi: NVS open (write) failed, verified token not persisted");
    return;
  }
  prefs.putString(kKeyToken, token);
  prefs.putString(kKeyValidUntil, valid_until);
  prefs.end();
}

/** Blocking GET with a hard body-size limit. Returns the body, or an empty string on any failure. */
String httpsGet(const char* url, const char* authorization, size_t max_bytes) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("knmi: http.begin failed");
    return "";
  }
  http.setConnectTimeout(static_cast<int>(config::kHttpConnectTimeoutMs));
  http.setTimeout(config::kHttpTotalTimeoutMs);
  http.addHeader("User-Agent", config::kHttpUserAgent);
  if (authorization != nullptr) {
    http.addHeader("Authorization", authorization);
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("knmi: HTTP %d for %s\n", code, url);
    http.end();
    return "";
  }

  const int content_length = http.getSize();
  if (content_length > static_cast<int>(max_bytes)) {
    Serial.println("knmi: response too large");
    http.end();
    return "";
  }

  const String body = http.getString();
  http.end();

  if (body.length() == 0 || body.length() > max_bytes) {
    Serial.println("knmi: empty or oversized body");
    return "";
  }
  return body;
}

/** Downloads and structurally validates the manifest. On success, fills token/valid_until. */
bool fetchAndValidateManifest(char* token_out, size_t token_out_len, char* valid_until_out,
                               size_t valid_until_out_len) {
  char today[11];
  if (!services::clock::formatLocalDate(today, sizeof(today))) {
    return false;
  }

  // Cache-bust CDN-fronted raw.githubusercontent.com with a day-granularity query param.
  const long day_number = static_cast<long>(services::clock::nowUtc() / (24 * 60 * 60));
  const String url = String(config::kKnmiManifestUrl) + "?day=" + String(day_number);

  const String body = httpsGet(url.c_str(), /*authorization=*/nullptr, config::kKnmiManifestMaxBytes);
  if (body.length() == 0) {
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.println("knmi: manifest is not valid JSON");
    return false;
  }

  if (doc["schema_version"].as<int>() != 1) {
    Serial.println("knmi: manifest schema_version mismatch");
    return false;
  }
  const char* provider = doc["provider"] | "";
  if (strcmp(provider, "knmi-open-data-anonymous") != 0) {
    Serial.println("knmi: manifest provider mismatch");
    return false;
  }

  const char* token = doc["token"] | "";
  const size_t token_len = strlen(token);
  if (token_len < 50 || token_len > kTokenMaxLen || !isTokenCharsetValid(token)) {
    Serial.println("knmi: manifest token missing or malformed");
    return false;
  }

  const char* valid_from = doc["valid_from"] | "";
  const char* valid_until = doc["valid_until"] | "";
  if (!isIsoDate(valid_from) || !isIsoDate(valid_until)) {
    Serial.println("knmi: manifest dates missing or malformed");
    return false;
  }
  // Fixed-width ISO dates compare chronologically as plain strings.
  if (strcmp(today, valid_from) < 0 || strcmp(today, valid_until) > 0) {
    Serial.println("knmi: manifest token outside its valid date range");
    return false;
  }

  if (token_len >= token_out_len || strlen(valid_until) >= valid_until_out_len) {
    return false;
  }
  strcpy(token_out, token);
  strcpy(valid_until_out, valid_until);
  return true;
}

/** Confirms a token is accepted by KNMI itself via the lightweight file-list endpoint. */
bool validateTokenAgainstKnmi(const char* token) {
  const String url = String(config::kKnmiApiBase) + config::kKnmiFileListValidationPath;
  const String body = httpsGet(url.c_str(), token, config::kKnmiValidationMaxBytes);
  if (body.length() == 0) {
    Serial.println("knmi: token rejected by KNMI file-list endpoint");
    return false;
  }
  return true;
}

unsigned long currentIntervalMs() {
  return s_have_refreshed_once ? config::kKnmiTokenRefreshIntervalMs
                                : config::kKnmiTokenRetryIntervalMs;
}

void refreshOnce() {
  char candidate_token[kTokenMaxLen + 1];
  char candidate_valid_until[11];
  if (!fetchAndValidateManifest(candidate_token, sizeof(candidate_token), candidate_valid_until,
                                 sizeof(candidate_valid_until))) {
    return;  // Keep whatever cached token (if any) is already active.
  }

  if (!validateTokenAgainstKnmi(candidate_token)) {
    return;  // Manifest well-formed but KNMI itself rejected it; keep the old cached token.
  }

  strcpy(s_token, candidate_token);
  strcpy(s_valid_until, candidate_valid_until);
  s_available = true;
  s_have_refreshed_once = true;
  saveToNvs(s_token, s_valid_until);
  Serial.printf("knmi: verified and saved new token %s valid_until=%s\n", redact(s_token).c_str(),
                s_valid_until);
}

}  // namespace

void knmiTokenInit() { loadFromNvs(); }

void knmiTokenLoop() {
  if (!services::clock::clockIsValid()) {
    return;
  }

  const unsigned long now_ms = millis();
  const bool due = s_force_refresh || !s_have_attempted_once ||
                    static_cast<uint32_t>(now_ms - s_last_attempt_ms) >= currentIntervalMs();
  if (!due) {
    return;
  }

  s_force_refresh = false;
  s_have_attempted_once = true;
  s_last_attempt_ms = now_ms;
  refreshOnce();
}

bool knmiTokenAvailable() { return s_available; }

const char* knmiToken() { return s_token; }

void knmiTokenForceRefresh() { s_force_refresh = true; }

}  // namespace services::knmi_token
