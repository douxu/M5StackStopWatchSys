/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include <esp_log.h>
#include <esp_netif_sntp.h>
#include <esp_timer.h>
#include <atomic>

namespace {
constexpr const char* TAG = "HAL-TimeSync";
// One server per attempt also works with CONFIG_LWIP_SNTP_MAX_SERVERS=1.
constexpr const char* SERVERS[] = {"ntp.aliyun.com", "time.cloudflare.com", "pool.ntp.org"};
constexpr int64_t ATTEMPT_TIMEOUT_US = 30LL * 1000000;
constexpr int64_t RETRY_INTERVAL_US  = 60LL * 1000000;
constexpr int64_t REFRESH_INTERVAL_US = 6LL * 60 * 60 * 1000000;

std::atomic<bool> sync_received{false};
bool was_connected = false;
bool running = false;
size_t server_index = 0;
int64_t started_at = 0;
int64_t next_attempt = 0;

void stop_sync()
{
    if (running) {
        // Stop the TCP/IP callback before consuming its final notification.
        esp_netif_sntp_deinit();
        running = false;
    }
}
}  // namespace

void Hal::time_sync_tick(bool connected)
{
    // Called by the main loop, never by a Wi-Fi or TCP/IP callback. All deadlines
    // use monotonic time so correcting the wall clock cannot delay retries.
    const int64_t now = esp_timer_get_time();
    if (running && (!connected || sync_received.load() || now - started_at >= ATTEMPT_TIMEOUT_US)) {
        stop_sync();
        if (sync_received.exchange(false)) {
            // SNTP has already set the system's UTC clock. Persist it on the
            // main task; the TCP/IP task must never perform RTC/I2C operations.
            syncSystemTimeToRtc();
            next_attempt = now + REFRESH_INTERVAL_US;
            ESP_LOGI(TAG, "Network date/time synchronized; next refresh in 6 hours");
        } else {
            if (connected) ESP_LOGW(TAG, "Time sync timed out; retrying with next server in 60 seconds");
            server_index = (server_index + 1) % (sizeof(SERVERS) / sizeof(SERVERS[0]));
            next_attempt = now + RETRY_INTERVAL_US;
        }
    }

    if (!connected) {
        was_connected = false;
        return;
    }
    if (!was_connected) {
        was_connected = true;
        next_attempt = now;
    }
    if (running || now < next_attempt) return;

    sync_received.store(false);
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(SERVERS[server_index]);
    config.wait_for_sync = false;
    config.sync_cb = [](struct timeval*) { sync_received.store(true); };
    const esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        next_attempt = now + RETRY_INTERVAL_US;
        ESP_LOGW(TAG, "Cannot start time sync: %s; retrying in 60 seconds", esp_err_to_name(err));
        return;
    }
    running = true;
    started_at = now;
    ESP_LOGI(TAG, "Synchronizing date/time via %s", SERVERS[server_index]);
}
