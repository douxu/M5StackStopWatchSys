# Weather component

Standalone ESP-IDF service providing HTTPS weather retrieval, response parsing,
city selection, region data, cached snapshots and persisted preferences.
Public headers live in `include/`. Dependencies are `esp_http_client`, `mbedtls`,
`freertos` and `json`; there is no dependency on HAL, LVGL or application code.

Call `weather::init(config)` once at boot before using the service. All callback
pointers are required and must remain valid for the firmware lifetime:

- `connected` and `millis` provide network readiness and monotonic milliseconds.
- `getInt`, `getString`, `setInt`, `setString`, `erase` provide storage in a dedicated
  weather namespace. Reads receive defaults; callbacks must be thread-safe.

The M5StopWatch adapter in `main/hal/hal.cpp` uses the existing NVS namespace and
keys, preserving location, mode and cached weather across this extraction.
The existing `Mode` API is retained for compatibility with saved standby choices;
only Weather mode fetches data. No shutdown API is provided.

Standby rendering/input handling lives in `main/apps/common/standby/`, while the
settings UI and fonts remain in `main/`. Region generation is handled by
`scripts/generate_weather_regions.py`; data sources are documented in `REGIONS.md`.

Verification: `python3 tests/weather/run.py` and `idf.py build` from the repository root.

Clock corrections are detected by comparing wall-clock and monotonic elapsed
seconds (tolerance: 5 seconds). `Config::wallTime` defaults to system time and can
be overridden in host tests. A correction clears the in-memory fetch timestamp,
marks it untrusted in storage, and requests a refresh. Data remains available as
cached weather. Failed refreshes retain the normal retry backoff; requests that
span a detected correction are discarded. `fetchedAt == 0` means the UI must not
format a date. Successful refreshes restore trust. This detects clock changes;
it does not certify that the clock was already accurate at boot.
