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

---

## KNMI METAR (Milestone 7 discovery)

Discovered with `tools/inspect_knmi_metar.py` against the live KNMI Open
Data API, using the anonymous token from the public
`schiphol-display-config` manifest.

### Endpoints used

1. `GET /datasets/metar/versions/1.0/files?maxKeys=25&orderBy=created&sorting=desc`
   — file listing, `Authorization: <token>` header.
2. `GET /datasets/metar/versions/1.0/files/<url-encoded-filename>/url`
   — same auth header, returns a short-lived `temporaryDownloadUrl`.
3. `GET <temporaryDownloadUrl>` — no Authorization header (plain HTTPS,
   signed query string).

### Critical deviation from the plan's assumption

The plan assumed one ASCII bulletin containing multiple stations. The real
dataset is **one file per station per observation**, not a combined
bulletin. `maxKeys=1&orderBy=created&sorting=desc` does **not** reliably
return an EHAM file — reports for EHAM, EHRD, EHGG, EHLE, EHBK, and others
are interleaved by creation time, roughly every 30 minutes per station.
The client must list a page (`maxKeys=25` covers well over a full rotation
of all Dutch stations) and filter filenames containing `EHAM`.

### Filename pattern (stable, observed across ~100 files)

```text
A_LANL80<ICAO><DDHHMM>_C_<ICAO>_<YYMMDDHHMMSS>.xml
```

Example: `A_LANL80EHAM212150_C_EHAM_210726215016.xml`.

### File body format

Each file is a WMO GTS bulletin, not raw METAR text and not standalone
XML:

1. An abbreviated-heading-line (AHL) preamble, CRCRLF-separated, e.g.:
   ```text
   0000350701\r\r\nLANL80 EHAM 212150\r\r\n
   ```
2. Followed immediately by an IWXXM XML document (`<?xml version="1.0" ?>`
   ... `<iwxxm:METAR automated=...>`).
3. **The traditional alphanumeric code (TAC) is embedded verbatim inside
   an XML comment** near the top of the IWXXM payload:
   ```text
   <!-- METAR EHAM 212155Z 34004KT 9999 FEW015 SCT026 BKN037 17/13 Q1025 NOSIG= -->
   ```

This TAC comment is the intended parsing target for Milestone 9 — trivial
substring extraction (`<!-- METAR ` / `<!-- SPECI ` up to ` -->`), not
IWXXM XML parsing. Observed properties:

- Encoding: UTF-8/ASCII, CRLF line endings.
- Typical file size: ~3.0–3.6 KB.
- One report per file — no multi-report or multi-station files observed.
- Reports start with `METAR` or `SPECI`, always followed by the 4-letter
  ICAO station and a `DDHHMM Z` timestamp.
- A file for a given station may legitimately contain no match for a
  different station — this is the normal, expected "no EHAM report" case
  when the queried file belongs to another station, not a failure.

### Fixtures captured

- `fixtures/knmi_file_list.json` — sanitized filename list (no other
  metadata retained).
- `fixtures/knmi_file_url.json` — file-url response with
  `temporaryDownloadUrl` redacted (signed, short-lived).
- `fixtures/metar_eham.txt` — extracted TAC line from a real EHAM file.
- `fixtures/metar_no_eham.txt` — full body of a real non-EHAM (EHRD) file,
  demonstrating the AHL+IWXXM wrapper and the absence of an EHAM match.

### Stop condition check

Not triggered. The embedded TAC comment makes on-device parsing practical
without an IWXXM XML parser; no architecture change or proxy service is
required.
