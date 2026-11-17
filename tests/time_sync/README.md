# Network date/time checks

Run `python3 tests/time_sync/run.py` from the repository root. A native C++17
compiler is required. The runner compiles the production sync service with fake
ESP-IDF APIs; it does not change the host clock or use the network.

Checks cover offline startup, one request per connection, deferred RTC writes,
six-hour refresh, 30-second timeout, one-minute retry, server rotation,
disconnect cleanup, immediate reconnect, initialization failure backoff, and a
successful callback arriving during shutdown.

Hardware acceptance after building and flashing:

1. Set an incorrect date/time, connect a network with Internet access, and watch
   `HAL-TimeSync` logs. Confirm date and time update together. The servers are
   `ntp.aliyun.com`, `time.cloudflare.com`, and `pool.ntp.org`, tried in rotation.
2. Confirm Beijing time (UTC+8) on devices without a saved timezone. An existing
   `system/tz` setting is preserved. System time and the RX8130 remain in UTC.
3. Disconnect/reconnect Wi-Fi and toggle Badge AP mode. Confirm sync only runs
   with a station IP and retries immediately after reconnecting.
4. Block NTP (UDP port 123) or use Wi-Fi without Internet access. Confirm timeout
   and retry logs, usable UI/stopwatch, and no failed-attempt RTC write. Restore
   access and confirm automatic recovery.
5. After successful sync, turn Wi-Fi off and power-cycle with RTC backup power
   available. Confirm the corrected date/time survive via RX8130.
6. Keep connected for six hours and confirm a refresh. Keep the stopwatch running
   during a clock correction and verify elapsed time stays continuous.

Host tests and compilation cannot establish live NTP reachability or hardware
RTC retention. Manual time/date edits can be overwritten by the next automatic
sync while online.
