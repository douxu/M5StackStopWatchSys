#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <sys/time.h>

using esp_err_t = int;
constexpr int ESP_OK = 0;
struct esp_sntp_config_t {
    const char* server;
    bool wait_for_sync = true;
    void (*sync_cb)(struct timeval*) = nullptr;
};
#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server) esp_sntp_config_t{server}
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
int64_t monotonic_us = 0;
int starts = 0, stops = 0, rtc_writes = 0;
int init_error = ESP_OK;
bool active = false, notify_on_stop = false;
void (*callback)(struct timeval*) = nullptr;
std::string server;
int64_t esp_timer_get_time() { return monotonic_us; }
int esp_netif_sntp_init(const esp_sntp_config_t* config)
{
    assert(!active);
    assert(!config->wait_for_sync);
    ++starts;
    if (init_error) return init_error;
    active = true;
    server = config->server;
    callback = config->sync_cb;
    return ESP_OK;
}
void esp_netif_sntp_deinit()
{
    assert(active);
    // Model a callback arriving while the main loop is stopping the client.
    if (notify_on_stop) callback(nullptr);
    notify_on_stop = false;
    active = false;
    callback = nullptr;
    ++stops;
}
class Hal {
public:
    void time_sync_tick(bool connected);
    void syncSystemTimeToRtc() { assert(!active); ++rtc_writes; }
};
#include "time_sync_under_test.inc"

int main()
{
    Hal hal;
    hal.time_sync_tick(false);
    assert(starts == 0 && rtc_writes == 0);
    hal.time_sync_tick(true);
    assert(starts == 1 && active && server == "ntp.aliyun.com");
    hal.time_sync_tick(true);
    assert(starts == 1); // Repeated main-loop ticks must not restart SNTP.
    callback(nullptr);
    assert(rtc_writes == 0); // No RTC access on the TCP/IP callback task.
    hal.time_sync_tick(true);
    assert(stops == 1 && rtc_writes == 1 && !active);
    monotonic_us += 6LL * 60 * 60 * 1000000 - 1;
    hal.time_sync_tick(true);
    assert(starts == 1);
    ++monotonic_us;
    hal.time_sync_tick(true);
    assert(starts == 2 && active);
    monotonic_us += 30LL * 1000000;
    hal.time_sync_tick(true);
    assert(!active && rtc_writes == 1);
    monotonic_us += 60LL * 1000000 - 1;
    hal.time_sync_tick(true);
    assert(starts == 2);
    ++monotonic_us;
    hal.time_sync_tick(true);
    assert(starts == 3 && server == "time.cloudflare.com");
    hal.time_sync_tick(false); // Wi-Fi off, lost IP, or AP-only mode.
    assert(!active && rtc_writes == 1);
    hal.time_sync_tick(true); // Reconnect bypasses the retry delay.
    assert(starts == 4 && server == "pool.ntp.org");
    notify_on_stop = true;
    hal.time_sync_tick(false);
    assert(!active && rtc_writes == 2); // A completed sync is saved even on disconnect.
    init_error = -1;
    hal.time_sync_tick(true);
    assert(starts == 5 && !active);
    hal.time_sync_tick(true);
    assert(starts == 5); // Initialization failures are rate-limited.
    monotonic_us += 60LL * 1000000;
    init_error = 0;
    hal.time_sync_tick(true);
    assert(starts == 6 && active);
    monotonic_us += 30LL * 1000000;
    hal.time_sync_tick(true);
    monotonic_us += 60LL * 1000000;
    hal.time_sync_tick(true);
    assert(starts == 7 && server == "ntp.aliyun.com"); // Server rotation wraps.
    callback(nullptr);
    hal.time_sync_tick(true);
    assert(rtc_writes == 3 && !active);
    hal.time_sync_tick(true);
    assert(rtc_writes == 3);
    puts("Time sync service checks passed");
}
