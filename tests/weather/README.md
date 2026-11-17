# Standby weather verification

Run with a native C/C++ compiler, CMake, Python and an ESP-IDF checkout (for cJSON):

```sh
IDF_PATH=/path/to/esp-idf python3 tests/weather/run.py
# Optionally also validate a captured wttr.in response:
IDF_PATH=/path/to/esp-idf python3 tests/weather/run.py /path/to/response.json
```

The runner builds production code with deterministic HAL/HTTP/NVS adapters and
real LVGL. It does not contact the network or modify device settings.

Coverage:
- Both top-level and nested `data` JSON; invalid JSON, missing fields, invalid
  temperatures/codes and UTF-8 URL encoding; failed parsing preserves the result.
- Automatic IP location saved as coordinates; manual selection saved to NVS;
  cache restoration; 30-minute refresh; one-minute failure retry; immediate
  reconnect retry; mode-off suspension; city change during an in-flight request.
- Mode selection, province/city navigation, municipality selection, Chinese font.
- 60-second idle threshold; sustained touch and button wake swallowed until
  release; brightness restored; stopwatch and alarm inhibition; Off mode; timer
  wrap; temporary LVGL ownership of a direct-rendering page.
- 466×466 PPM previews saved in `/tmp/stopwatch-weather-tests`.

Hardware acceptance after flashing:
1. Open Settings → Standby display; verify Weather is selected by default, and
   City settings contains the Chinese province/city lists. Select and reboot;
   confirm mode and city persist.
2. Connect Wi-Fi and wait for weather. Leave idle 60 seconds; check the time,
   local icon, Celsius temperature, city and update time at 25% brightness.
3. Touch or hold either button; verify only wake-up occurs, even on release.
   A subsequent press/touch should act normally. Check with the stopwatch paused.
4. Start the stopwatch and wait over 60 seconds; it must remain normally visible.
5. Select Off; after 60 seconds the screen should turn fully off and wake normally.
6. Disconnect the router; keep cached weather. Restore the router; refresh should
   resume. Before any successful fetch, display Weather unavailable without dialogs.
7. Repeat standby/wake in FluidBox; confirm no mixed frames and simulation resumes.
8. Trigger an alarm while in standby; verify it wakes and can be dismissed.
9. Verify TLS/network reachability on the actual deployment Wi-Fi, circular touch
   reachability, brightness and battery consumption. Host tests cannot establish
   radio reliability, geolocation accuracy, or hardware power behavior.

The displayed Updated/Cached timestamp is the device's successful fetch time,
not the provider's meteorological observation time. The device clock is now
automatically synchronized after Wi-Fi connects; see `tests/time_sync/README.md`.

Clock regression tests cover forward/backward corrections, offline invalidation, retry backoff, in-flight response rejection, persisted distrust after reboot, and monotonic timer wrap.
