# Codex usage verification

Run `python3 tests/codex_usage/run.py` for payload validation, optional windows,
multi-bucket selection, reset countdowns, authenticated POST construction,
JSON-RPC initialization/notifications/errors/timeouts, and subprocess cleanup.
The receiver harness also checks pairing, partial requests, stale cache including
timer wrap, server lifecycle, and rejection in Wi-Fi/badge configuration modes.
Only the Python standard library, a C++17 compiler, and ESP-IDF's cJSON are needed.
Set `IDF_PATH` if ESP-IDF is installed somewhere else.

Run `python3 tests/weather/run.py` for real LVGL rendering, settings selection,
pairing details, live IP changes, usage bars, waiting/cached/offline states,
saved Codex mode, and regressions for weather, idle, wake, stopwatch and alarms.
Screenshots are written under `/tmp/stopwatch-weather-tests` as PPM files.

Hardware acceptance (requires a flashed device and signed-in Codex account):

1. Select Settings → Standby display → Codex usage; reboot and verify it remains selected.
2. Connect Codex shows the device IP and a persistent 12-character pairing code.
3. Run `python3 scripts/sync_codex_usage.py --device DEVICE_IP --once`; verify success.
4. Wait 60 seconds, confirm quota windows match the same account's Codex usage,
   percentages mean **used**, reset countdowns are readable within the round display.
5. Run continuously; disconnect the computer for over 3 minutes and verify cached status;
   reconnect and verify fresh values. Wi-Fi loss should immediately show cached status.
6. Wrong pairing codes must be rejected. Select Off/Weather and verify the receiver
   stops within a few seconds. Badge/configuration AP must not accept usage updates.
7. Touch/button wakes without triggering the underlying app; test FluidBox, running
   stopwatch and an alarm. Normal brightness must be restored after wake.
8. API-key accounts/no quota windows must report an error instead of publishing 0%.

Host tests use a fake Codex subprocess; they do not access credentials or verify
the live account/network/hardware. The firmware build verifies ESP-IDF API compatibility.
