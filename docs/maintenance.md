# Maintenance

## Rotating the KNMI anonymous token

Weather data (Milestone 9 onward) comes from the KNMI Open Data API using a
free anonymous token. KNMI occasionally rotates these tokens; when that
happens, boards in the field pick up the new token automatically — no
reflash and no Wi-Fi portal step required.

The current token lives in a separate public repository,
[`carlosarceleon/schiphol-display-config`](https://github.com/carlosarceleon/schiphol-display-config),
as `knmi-token.json` on `main`:

```json
{
  "schema_version": 1,
  "provider": "knmi-open-data-anonymous",
  "token": "<the anonymous token from https://developer.dataplatform.knmi.nl/open-data-api#token>",
  "valid_from": "YYYY-MM-DD",
  "valid_until": "YYYY-MM-DD",
  "updated_at": "YYYY-MM-DDTHH:MM:SSZ",
  "source_url": "https://developer.dataplatform.knmi.nl/open-data-api#token"
}
```

To rotate the token:

1. Fetch the current anonymous token from the KNMI developer portal.
2. Update `token`, `valid_from`, `valid_until`, and `updated_at` in
   `knmi-token.json` and merge to `main` in that repo (CI there validates the
   file's shape before merge).
3. That's it. Each board polls the raw file at most once a day
   (`config::kKnmiTokenRefreshIntervalMs`) and, once verified, saves the new
   token to NVS.

## On-device behavior (`services::knmi_token`, Milestone 8)

- The manifest is downloaded from the raw GitHub URL in
  `config::kKnmiManifestUrl`, capped at `config::kKnmiManifestMaxBytes`.
- Every field is validated: `schema_version`, `provider`, token length and
  base64url charset, and that today's local date falls within
  `valid_from`..`valid_until`.
- A manifest that passes those checks is then validated against KNMI itself
  with a single lightweight file-list request
  (`config::kKnmiFileListValidationPath`) before being trusted.
- Only a token that passes both checks is written to NVS (namespace `knmi`)
  and returned by `knmiToken()`.
- If the manifest is unreachable, malformed, or KNMI rejects the token, the
  last verified cached token (if any) keeps being used — nothing is
  overwritten and nothing appears on the display either way. Token state is
  never shown on-screen and never configurable from the Wi-Fi portal.
- Refresh cadence is daily once a token has ever been verified, or every
  `config::kKnmiTokenRetryIntervalMs` until then. Callers (e.g. the METAR
  client in Milestone 9) can call `knmiTokenForceRefresh()` after a 401/403
  to pick up a rotated token immediately.
