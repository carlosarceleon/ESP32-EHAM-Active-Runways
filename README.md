# EHAM Active Runways

<img width="800" height="450" alt="eham-runways" src="https://github.com/user-attachments/assets/716d0992-dab8-47ba-8f1a-2aec7f607419" />

Firmware for an **ESP32-C3 Super Mini** and a **1.28″ round GC9A01** display
(240×240). Shows the current landing and departure runways in use at
**Amsterdam Schiphol (EHAM)**, plus an optional compact official EHAM
weather summary, with **WiFiManager** for first-time Wi‑Fi setup.

Forked from [MatixYo/ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar)
(the original ADS-B radar screen has since been removed — see
**Attribution** below). Runway schematic layout adapted from
[archofthings/ha-schiphol-runway-card](https://github.com/archofthings/ha-schiphol-runway-card).

## What it does

1. **Wi‑Fi setup** (if needed) — captive portal on AP **`SchipholRunways-Setup`**
2. **Runway status** — active EHAM landing/departure runways rendered on a
   stylized Schiphol map, refreshed every 5 minutes from
   [dutchplanespotters.nl](https://www.dutchplanespotters.nl/)
3. **Weather (optional)** — a compact wind/temperature row from the official
   KNMI EHAM METAR, refreshed every 30 minutes, shown only when a recent
   report is available

After Wi‑Fi is saved, the device reconnects automatically and keeps running
without further input.

## Data sources

| Data | Source | Refresh | Behavior when unavailable |
|------|--------|---------|----------------------------|
| Runway usage | [dutchplanespotters.nl](https://www.dutchplanespotters.nl/api/runways/ams) (aggregates LVNL data) | 5 min (1 min / 5 min backoff on failure) | Last known state shown as live for up to 15 min, then a `LIVE DATA UNAVAILABLE` banner replaces the map annotations |
| Weather (METAR) | [KNMI Open Data](https://developer.dataplatform.knmi.nl/) `metar` dataset (CC BY 4.0) | 30 min + per-device jitter | Weather row silently disappears; never affects runway display |

Both feeds run as independent services with their own schedules and error
handling — a KNMI outage never delays or blocks a runway refresh, and vice
versa. See `docs/external-data-contracts.md` for the full wire-format
details these clients were built against.

### KNMI anonymous token

Weather fetching needs a KNMI Open Data API token. This firmware never
embeds one directly — it fetches a small, publicly-hosted manifest
(`config::kKnmiManifestUrl`, a separate `schiphol-display-config` repo)
containing a shared anonymous token, validates it structurally and against
KNMI's own API before trusting it, and caches the result in NVS. See
`docs/maintenance.md` for the token-rotation runbook. If the manifest or
KNMI is unreachable, the last verified cached token keeps being used —
nothing changes on-screen or requires end-user action either way.

## Controls (BOOT, GPIO 9, active LOW)

| Action | Effect |
|--------|--------|
| **Short tap** | Force an immediate runway refresh (test builds cycle EHAM fixture states instead — see below) |
| **Hold 3 s** | Clear Wi‑Fi; reboot into setup portal |

During setup you can also hold BOOT at power-on to force a credential reset
(same as the long press).

## Wi‑Fi setup portal

**First-time setup** (no saved Wi‑Fi):

1. Connect to **`SchipholRunways-Setup`**
2. Open **`http://schiphol-runways.local`** (preferred) or
   **`http://192.168.4.1`** — both are shown on the setup screen; captive
   portal may open automatically
3. Set home Wi‑Fi, then save

**Reconfigure anytime** (after the device is on your network):

1. Open **`http://schiphol-runways.local`** or **`http://<device-ip>`**
   (e.g. from your router or serial log at boot)
2. Change Wi‑Fi; save

The same portal runs on the setup AP and on the device's LAN IP while
connected to Wi‑Fi. mDNS hostname is `schiphol-runways` →
**schiphol-runways.local** (`kPortalHostname` in `config.h`). Some clients
resolve `.local` slowly; use the IP if needed. Only Wi‑Fi setup is exposed
in the portal — there are no other configurable fields.

After a reset, the device reboots and shows the setup screen immediately (no
"Connecting" loop on stale credentials).

## Configuration

Edit **`include/config.h`** for hardware and behavior:

| Area | Keys / notes |
|------|----------------|
| Firmware | `kFirmwareVersion` |
| Portal | `kPortalApName`, `kPortalIp`, `kPortalHostname` / `kPortalHostUrl` (mDNS; needs `-DWM_MDNS` in `platformio.ini`) |
| Wi‑Fi timing | connect attempts, reconnect grace, portal timeout (`0` = no timeout) |
| BOOT | `kBootPin`, `kBootResetHoldMs`, `kBootTapMinMs` |
| Display SPI | pins, `kDisplayInvert`, `kDisplayRgbOrder`, `kDisplaySpiWriteHz` |
| Runway API | `kRunwayApiUrl`, fetch/retry/backoff intervals, freshness limit |
| KNMI token | `kKnmiManifestUrl`, `kKnmiApiBase`, refresh/retry intervals |
| KNMI METAR | file-list/url paths, body-size limits, fetch interval, jitter range |

## Project layout

```
include/
  config.h
  hardware/
    lgfx_config.hpp
    display.h
    display_font.h
  data/
    eham_runways.h
  domain/
    eham_state.h
  ui/
    schiphol_theme.h
    eham_display.h
    status_screens.h
  services/
    wifi_setup.h
    clock_service.h
    runway_client.h
    runway_parser.h
    knmi_token_manager.h
    knmi_metar_client.h
    metar_parser.h
data/
  ui_font.vlw              — embedded smooth UI font (Noto Sans Bold)
fixtures/                  — captured/hand-built API response fixtures, mirrored
                              in each service's on-device self-test
tools/                     — host-side scripts that captured the fixtures above
src/
  main.cpp
  data/
  domain/
  hardware/
  ui/
  services/
```

## Wiring (GC9A01 ↔ ESP32-C3 Super Mini)

| Display | ESP32-C3 |
|---------|----------|
| VCC | 3V3 |
| GND | GND |
| RST | GPIO **0** |
| CS | GPIO **1** |
| DC | GPIO **10** |
| SDA (MOSI) | GPIO **3** |
| SCL (SCLK) | GPIO **4** |
| BOOT (user) | GPIO **9** |

## Build

```bash
pio run -t upload
pio device monitor
```

- PlatformIO env: **`supermini`**
- Serial: **115200** baud
- USB CDC on boot enabled in `platformio.ini` for the Super Mini

### Test build (fixture mode)

`supermini_eham_selftest` builds the same firmware plus a boot-time serial
self-test of every parser (runway, clock, METAR) and a BOOT-short-tap
fixture cycler on the display for palette/layout verification, in place of
live Wi‑Fi/network behavior. Not for normal flashing:

```bash
pio run -e supermini_eham_selftest -t upload
```

### Web-flashable release image

Single `.bin` for [esptool-js](https://espressif.github.io/esptool-js/) and
similar tools (ESP32-C3, 4 MB, flash at **0x0**):

```bash
chmod +x scripts/merge-firmware.sh   # once
./scripts/merge-firmware.sh
```

Writes `release/eham-runways-merged.bin`. Skip rebuild if firmware is
already built:

```bash
./scripts/merge-firmware.sh --no-build
```

Or via PlatformIO only (output: `.pio/build/supermini/firmware-merged.bin`):

```bash
pio run -e supermini
pio run -t merge -e supermini
```

Put the board in download mode (hold **BOOT**, tap **RESET**), then flash
with Chrome/Edge over USB.

### CI and releases (GitHub Actions)

| Workflow | When | Output |
|----------|------|--------|
| [Build](.github/workflows/build.yml) | Push / PR to `main` | Artifact `eham-runways-supermini` (merged + split `.bin` files, ~90 days) |
| [Release](.github/workflows/release.yml) | Git tag `v*` (e.g. `v1.0.0`) | GitHub Release asset `eham-runways-v1.0.0.bin` + `.sha256` |

To ship a version users can download:

```bash
git tag v1.0.0
git push origin v1.0.0
```

The release workflow builds firmware in CI and attaches the merged image to
the release. Download from **Releases** on GitHub, then flash at **0x0**
(ESP32-C3, 4 MB).

## Troubleshooting

| Symptom | Check |
|---------|-------|
| Stuck on setup screen | Confirm the home network is 2.4 GHz (ESP32-C3 has no 5 GHz radio) |
| `LIVE DATA UNAVAILABLE` banner | Normal after ~15 min without a successful runway fetch; check Wi‑Fi and the serial log for `runway:` HTTP errors |
| No weather row | Expected whenever no recent EHAM METAR is available (KNMI publishes roughly every 30 min); check the serial log for `metar:`/`knmi:` messages before assuming a bug |
| Can't reach the portal by hostname | Use the device's IP instead — some OSes resolve `.local` slowly or not at all |
| Colors look swapped (red/blue) on a new panel revision | See `ui::schiphol::toPanelColor()`'s doc comment and `paletteCalibrationDraw()` (fixture build only) |

## Attribution

- Original firmware: [MatixYo/ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar) (MIT)
- EHAM runway schematic reference: [archofthings/ha-schiphol-runway-card](https://github.com/archofthings/ha-schiphol-runway-card) (MIT)
- Runway usage data: [dutchplanespotters.nl](https://www.dutchplanespotters.nl/), aggregating LVNL data
- Weather data: KNMI Open Data `metar` dataset (CC BY 4.0)

## Dependencies

- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
