# External data contracts

## Schiphol runway usage (dutchplanespotters)

### Endpoint

```text
GET https://www.dutchplanespotters.nl/api/runways/ams?date=YYYY-MM-DD
```

`date` is the Europe/Amsterdam calendar date to query — the firmware derives
it from the synchronized clock via `services::clock::formatLocalDate()`.

### Observed response (captured 2026-07-21, see `fixtures/runway_current.json`)

```json
{
  "title": "Schiphol Runway Usage",
  "times": [
    {
      "from": "2026-07-21T00:00:02+00:00",
      "until": "2026-07-21T04:40:00+00:00",
      "landingRunways": ["06"],
      "departingRunways": ["36L"]
    }
  ],
  "peakTimes": []
}
```

- Fields consumed by the parser: `times[].from`, `times[].until`,
  `times[].landingRunways`, `times[].departingRunways`.
- Fields ignored: top-level `title`, top-level `peakTimes`.
- A full day's response is ~3 KB (21 slots observed for 2026-07-21), well
  under the 64 KiB hard limit in `config::kRunwayMaxBodyBytes`.
- Timestamps use an explicit UTC offset (`+00:00` observed year-round in this
  capture; the reference integration also documents `+01:00`/`+02:00`).
  Parsed by the existing dependency-free `services::clock::parseIso8601ToUtc`.

### Runway identifier normalization

See `data::eham_runways::normalizeHeading` / `findRunwayEnd`:

1. Trim whitespace, uppercase.
2. Bare `18` → `18C`, bare `36` → `36C` (Zwanenburgbaan aliases, per the
   reference `archofthings/ha-schiphol-runway-card` integration).
3. Leading zeroes preserved (`04`, `06`, `09`).
4. Unknown identifiers are logged and skipped — they do not fail the parse
   (see `fixtures/runway_current.json`'s `99Z`-style case exercised in
   `services::runway::runParserSelfTest`).

### Time-slot selection

Implemented in `services::runway::parseRunwayResponse`
(`src/services/runway_parser.cpp`):

1. Prefer an exact slot where `from <= now <= until`.
2. Otherwise accept the most recent past slot only if `now - until <= 10
   minutes` (`kBoundedFallbackSec`).
3. Otherwise `ParseResult::NoAcceptableSlot` — caller does not apply state.
4. Near local midnight (`HH` == `00`), a `NoAcceptableSlot` result triggers
   one retry against the previous local date
   (`services::runway::fetchAndParse`, `src/services/runway_client.cpp`).

### Fixtures

- `fixtures/runway_current.json` — full real response for 2026-07-21 (21
  slots), captured via `tools/capture_runway_fixture.py`.
- `fixtures/runway_no_exact_slot.json` — two slots with a gap, used to
  exercise the bounded-fallback and stale-rejection paths.
- `fixtures/runway_malformed.json` — truncated JSON, used to exercise the
  malformed-response path.

Regenerate the live fixture with:

```bash
python3 tools/capture_runway_fixture.py [YYYY-MM-DD]
```

### Poll interval and failure handling

See `config::kRunwayFetchIntervalMs` / `kRunwayRetryIntervalMs` /
`kRunwaySlowRetryIntervalMs` / `kRunwayFailuresBeforeSlowBackoff` /
`kRunwayFreshnessLimitSec`:

- Normal refresh: every 5 minutes.
- On failure: retry after 1 minute; after 3 consecutive failures, retry
  every 5 minutes.
- The last successful fetch is shown as live for up to 15 minutes; past
  that, the display clears active runway flags and shows
  `LIVE DATA UNAVAILABLE`.
- A BOOT short tap forces an immediate refresh
  (`services::runway::forceRefresh()`).

### HTTP behavior

- `WiFiClientSecure::setInsecure()` — no private credentials are sent to
  this endpoint; see plan §14 for the TLS-validation caveat carried over
  from earlier milestones.
- Connect timeout 5 s, total timeout 12 s
  (`config::kHttpConnectTimeoutMs` / `kHttpTotalTimeoutMs`).
- `User-Agent: ESP32-Schiphol-Runway-Display/1.0`.
- `http.end()` is always called before returning from
  `services::runway::fetchOnce`.
