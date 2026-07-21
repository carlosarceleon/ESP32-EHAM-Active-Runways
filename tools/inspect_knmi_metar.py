#!/usr/bin/env python3
"""Discover and fixture the KNMI Open Data METAR file workflow.

Host-side only (Milestone 7). Proves the exact contract before any ESP32
KNMI client code is written:

1. Read and validate the public anonymous token manifest.
2. List the latest `metar` 1.0 file.
3. Request its temporary download URL.
4. Download the ASCII file and inspect its shape (single/multi report,
   stations, headers, line endings).

Saves sanitized fixtures under fixtures/ and never prints the complete
token or a temporary signed download URL.

Usage:
    python3 tools/inspect_knmi_metar.py
"""

import base64
import datetime
import json
import re
import string
import sys
import urllib.error
import urllib.request

MANIFEST_URL = (
    "https://raw.githubusercontent.com/carlosarceleon/schiphol-display-config"
    "/main/knmi-token.json"
)
KNMI_API_BASE = "https://api.dataplatform.knmi.nl/open-data/v1"
DATASET = "metar"
DATASET_VERSION = "1.0"
MANIFEST_MAX_BYTES = 2048
TOKEN_ALPHABET = set(string.ascii_letters + string.digits + "-_.")


def redact(token: str) -> str:
    if len(token) <= 8:
        return "***"
    return f"{token[:4]}...{token[-4:]}"


def fetch_manifest() -> dict:
    day = datetime.datetime.now(datetime.timezone.utc).toordinal()
    url = f"{MANIFEST_URL}?day={day}"
    req = urllib.request.Request(url, headers={"User-Agent": "ESP32-Schiphol-Runway-Display/1.0"})
    with urllib.request.urlopen(req, timeout=10) as resp:
        if resp.status != 200:
            raise SystemExit(f"manifest fetch failed: HTTP {resp.status}")
        body = resp.read(MANIFEST_MAX_BYTES + 1)
        if len(body) > MANIFEST_MAX_BYTES:
            raise SystemExit(f"manifest exceeds {MANIFEST_MAX_BYTES} bytes")
        return json.loads(body)


def validate_manifest(manifest: dict) -> str:
    if manifest.get("schema_version") != 1:
        raise SystemExit("manifest: unexpected schema_version")
    if manifest.get("provider") != "knmi-open-data-anonymous":
        raise SystemExit("manifest: unexpected provider")
    token = manifest.get("token", "")
    if not (50 <= len(token) <= 512):
        raise SystemExit("manifest: token length out of range")
    if not set(token) <= TOKEN_ALPHABET:
        raise SystemExit("manifest: token contains non-base64url characters")
    valid_until = datetime.date.fromisoformat(manifest["valid_until"])
    if valid_until <= datetime.datetime.now(datetime.timezone.utc).date():
        raise SystemExit("manifest: token expired")
    print(f"Manifest OK: token={redact(token)} valid_until={valid_until}")
    return token


def knmi_get(path: str, token: str, max_bytes: int) -> bytes:
    req = urllib.request.Request(
        f"{KNMI_API_BASE}{path}",
        headers={
            "Authorization": token,
            "User-Agent": "ESP32-Schiphol-Runway-Display/1.0",
        },
    )
    with urllib.request.urlopen(req, timeout=12) as resp:
        body = resp.read(max_bytes + 1)
        if len(body) > max_bytes:
            raise SystemExit(f"response for {path} exceeds {max_bytes} bytes")
        return body


def list_latest_eham_file(token: str) -> str:
    # The dataset publishes one file per station per report (roughly every
    # 30 minutes for EHAM), interleaved with EHRD/EHGG/EHLE/EHBK etc. --
    # not one combined multi-station bulletin. maxKeys=1 does NOT reliably
    # return an EHAM file, so list a page and filter by filename.
    path = f"/datasets/{DATASET}/versions/{DATASET_VERSION}/files?maxKeys=25&orderBy=created&sorting=desc"
    body = knmi_get(path, token, 16384)
    data = json.loads(body)
    files = data.get("files", [])
    if not files:
        raise SystemExit("file list: no files returned")

    sanitized = {"files": [{"filename": f["filename"]} for f in files]}
    with open("fixtures/knmi_file_list.json", "w") as f:
        json.dump(sanitized, f, indent=2)
        f.write("\n")
    print(f"Saved fixtures/knmi_file_list.json ({len(files)} entries)")

    eham_files = [f["filename"] for f in files if "EHAM" in f["filename"]]
    if not eham_files:
        raise SystemExit(
            "STOP CONDITION: no EHAM filename in the latest 25 files -- "
            "widen maxKeys or investigate publication cadence"
        )
    filename = eham_files[0]
    print(f"Latest EHAM file: {filename}")
    return filename


def request_download_url(token: str, filename: str) -> str:
    from urllib.parse import quote

    path = f"/datasets/{DATASET}/versions/{DATASET_VERSION}/files/{quote(filename, safe='')}/url"
    body = knmi_get(path, token, 16384)
    data = json.loads(body)
    sanitized = dict(data)
    sanitized["temporaryDownloadUrl"] = "<redacted-signed-url>"
    with open("fixtures/knmi_file_url.json", "w") as f:
        json.dump(sanitized, f, indent=2)
        f.write("\n")
    print("Saved fixtures/knmi_file_url.json (signed URL redacted)")
    return data["temporaryDownloadUrl"]


def download_file(url: str) -> bytes:
    req = urllib.request.Request(url, headers={"User-Agent": "ESP32-Schiphol-Runway-Display/1.0"})
    with urllib.request.urlopen(req, timeout=12) as resp:
        return resp.read()


# Each KNMI metar/1.0 file is a single-station IWXXM XML document, not a
# plain-ASCII multi-station bulletin. Every file embeds the traditional
# alphanumeric code (TAC) verbatim inside one XML comment, e.g.:
#   <!-- METAR EHAM 212155Z 34004KT 9999 FEW015 SCT026 BKN037 17/13 Q1025 NOSIG= -->
# Extracting that comment is far simpler on-device than parsing full IWXXM,
# and is the intended parsing target for Milestone 9.
TAC_COMMENT = re.compile(r"<!--\s*((?:METAR|SPECI)\s+[A-Z]{4}\s+\d{6}Z.*?)\s*-->", re.DOTALL)


def inspect_body(raw: bytes) -> None:
    has_crlf = b"\r\n" in raw
    text = raw.decode("utf-8", errors="replace")
    # Each file is a WMO bulletin: a short abbreviated-heading-line (AHL)
    # preamble (sequence number + "LANL80 EHAM DDHHMM", CRCRLF-terminated)
    # followed by the IWXXM XML payload starting at "<?xml".
    xml_offset = text.find("<?xml")

    print(f"File size: {len(raw)} bytes")
    print(f"Format: WMO bulletin (AHL preamble) + IWXXM XML" if xml_offset > 0 else "Format: unknown")
    if xml_offset > 0:
        print(f"AHL preamble: {text[:xml_offset]!r}")
    print(f"Line endings: {'CRLF' if has_crlf else 'LF'}")

    match = TAC_COMMENT.search(text)
    if not match:
        print("STOP CONDITION: no embedded TAC comment found in EHAM file")
        with open("fixtures/metar_no_eham.txt", "w") as f:
            f.write(text)
        print("Saved fixtures/metar_no_eham.txt")
        return

    tac = " ".join(match.group(1).split())
    with open("fixtures/metar_eham.txt", "w") as f:
        f.write(tac + "\n")
    print(f"Saved fixtures/metar_eham.txt: {tac!r}")


def main() -> int:
    manifest = fetch_manifest()
    token = validate_manifest(manifest)
    try:
        filename = list_latest_eham_file(token)
        download_url = request_download_url(token, filename)
        raw = download_file(download_url)
    except urllib.error.HTTPError as exc:
        print(f"STOP CONDITION: KNMI request failed: HTTP {exc.code}", file=sys.stderr)
        return 1
    inspect_body(raw)
    return 0


if __name__ == "__main__":
    sys.exit(main())
