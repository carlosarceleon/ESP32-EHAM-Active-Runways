# Maintenance

## AWC METAR weather

Weather comes from the public [Aviation Weather Center Data API](https://aviationweather.gov/data/api/)
using the raw EHAM METAR endpoint configured as `config::kAwcMetarUrl`.
No API key or token rotation is required.

The device sends `config::kHttpUserAgent`, polls every 30 minutes with a
deterministic per-device spread of up to two minutes, and drops the weather
row when the response is unavailable or stale. AWC asks clients to identify
themselves with a custom user agent and stay below 100 requests per minute;
this firmware is well below that limit.
