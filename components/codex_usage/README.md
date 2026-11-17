# Codex usage component

Standalone ESP-IDF component for receiving Codex quota snapshots over LAN.
It owns the HTTP receiver, payload validation, in-memory cache, pairing-code
creation, and quota/reset text helpers. Public API: `include/codex_usage.h`.

Dependencies: ESP-IDF `esp_http_server`, `esp_hw_support`, `freertos`, `json`.
There is no dependency on `main`, HAL, LVGL, weather, or application settings.

Call `codex_usage::init(config)` once at boot with four non-null callbacks:

- `enabled`: whether the application permits receiving updates (including network readiness).
- `millis`: monotonic milliseconds, with uint32_t wrap semantics.
- `loadPairingCode`: load the previously saved pairing code, or return an empty string.
- `savePairingCode`: persist a newly generated pairing code.

Callbacks must remain valid throughout application lifetime. `enabled` and
`millis` are invoked on background tasks and must be thread-safe. Storage callbacks
run synchronously during initialization. Initialize before reading snapshots or
pairing codes. The service runs for the firmware lifetime; no teardown is provided.

M5StopWatch supplies these callbacks in `main/hal/hal.cpp`. Menu and standby
rendering stay in `main`; the computer bridge stays in `scripts/sync_codex_usage.py`.
The port (8765), payload and persisted NVS key remain unchanged.

Run `python3 tests/codex_usage/run.py` and `python3 tests/weather/run.py` from
the repository root. The component directory is explicitly included in Git;
other downloaded `components/*` directories remain ignored.
