#pragma once

namespace services::knmi_token {

/** Loads any cached token from NVS. Call once during setup(), before Wi-Fi is required. */
void knmiTokenInit();

/**
 * Call every loop() iteration once Wi-Fi and the clock are valid. Downloads and
 * validates the public manifest on a daily schedule (or a short retry interval
 * until a token has ever been verified). A manifest or validation failure
 * silently keeps using the last verified cached token, if any. Never shown on
 * the display and never configurable from the Wi-Fi portal.
 */
void knmiTokenLoop();

/** True once a token has been downloaded from the manifest and verified, this boot or a previous one. */
bool knmiTokenAvailable();

/** The current verified token, or an empty string if none is available yet. */
const char* knmiToken();

/** Requests an out-of-schedule manifest refresh on the next knmiTokenLoop() call (e.g. after a 401/403). */
void knmiTokenForceRefresh();

}  // namespace services::knmi_token
