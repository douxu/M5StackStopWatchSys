#include "weather.h"
#include "regions.h"
#include <cassert>
#include <map>
#include <functional>
#include <iostream>
#include <fstream>
#include <iterator>
#include <mutex>
#include <atomic>
#include <cstdlib>
using TaskHandle_t   = void*;
using TickType_t     = unsigned;
constexpr int pdTRUE = 1, ESP_OK = 0, ESP_FAIL = -1, HTTP_EVENT_ON_DATA = 1;
using esp_err_t = int;
#define pdMS_TO_TICKS(x) (x)
struct End {};
std::function<void()> atSleep, duringRequest;
void (*taskEntry)(void*) = nullptr;
int xTaskCreate(void (*fn)(void*), const char*, int, void*, int, TaskHandle_t* out)
{
    taskEntry = fn;
    *out      = (void*)1;
    return 1;
}
void xTaskNotifyGive(TaskHandle_t)
{
}
void ulTaskNotifyTake(int, unsigned)
{
    atSleep();
}
struct Settings {
    static std::map<std::string, std::string> values;
    Settings(const std::string&, bool = false)
    {
    }
    int GetInt(const std::string& k, int d = 0)
    {
        return values.count(k) ? std::stoi(values[k]) : d;
    }
    std::string GetString(const std::string& k, const std::string& d = "")
    {
        return values.count(k) ? values[k] : d;
    }
    void SetInt(const std::string& k, int v)
    {
        values[k] = std::to_string(v);
    }
    void SetString(const std::string& k, const std::string& v)
    {
        values[k] = v;
    }
    void EraseKey(const std::string& k)
    {
        values.erase(k);
    }
};
std::map<std::string, std::string> Settings::values;
struct Hal {
    uint32_t now   = 0;
    bool connected = true;
    uint32_t millis()
    {
        return now;
    }
    struct Net {
        bool connected;
    };
    Net getWifiStatus()
    {
        return {connected};
    }
} hal;
Hal& GetHAL()
{
    return hal;
}
struct esp_http_client_event_t {
    int event_id;
    void* user_data;
    void* data;
    int data_len;
};
struct esp_http_client_config_t {
    const char* url;
    int timeout_ms;
    void (*crt_bundle_attach)();
    int (*event_handler)(esp_http_client_event_t*);
    void* user_data;
    int buffer_size;
    const char* user_agent;
};
void esp_crt_bundle_attach()
{
}
esp_http_client_config_t httpConfig;
std::string requestedURL, body;
int requests = 0, httpStatus = 200;
bool networkError = false;
void* esp_http_client_init(esp_http_client_config_t* cfg)
{
    httpConfig   = *cfg;
    requestedURL = cfg->url;
    return (void*)1;
}
int esp_http_client_perform(void*)
{
    ++requests;
    if (duringRequest) duringRequest();
    if (networkError) return ESP_FAIL;
    esp_http_client_event_t event{HTTP_EVENT_ON_DATA, httpConfig.user_data, body.data(), static_cast<int>(body.size())};
    return httpConfig.event_handler(&event);
}
int esp_http_client_get_status_code(void*)
{
    return httpStatus;
}
void esp_http_client_cleanup(void*)
{
}
int64_t wallOffset = 0;
std::time_t fakeWall() { return 1794000000 + hal.now / 1000 + wallOffset; }
#include "service_under_test.inc"
int main(int argc, char** argv)
{
    assert(argc == 2);
    std::ifstream file(argv[1]);
    body = std::string((std::istreambuf_iterator<char>(file)), {});
    weather::init({
        [] { return GetHAL().getWifiStatus().connected; },
        [] { return GetHAL().millis(); },
        [](const std::string& key, int fallback) { return Settings("weather").GetInt(key, fallback); },
        [](const std::string& key, const std::string& fallback) { return Settings("weather").GetString(key, fallback); },
        [](const std::string& key, int value) { Settings("weather", true).SetInt(key, value); },
        [](const std::string& key, const std::string& value) { Settings("weather", true).SetString(key, value); },
        [](const std::string& key) { Settings("weather", true).EraseKey(key); },
        fakeWall,
    });
    assert(weather::mode() == weather::Mode::Weather);
    int step = 0;
    atSleep  = [&] {
        ++step;
        hal.now += 1000;
        switch (step) {
            case 1:
                assert(requests == 1 && weather::snapshot().valid);
                assert(Settings::values["query"] == "39.929,116.388");
                assert(requestedURL == "https://wttr.in/?format=j1");
                break;
            case 2:
                assert(requests == 1);
                hal.connected = false;
                break;
            case 3:
                assert(requests == 1);
                hal.connected = true;
                break;
            case 4:
                assert(requests == 2);
                weather::selectCity(310000);
                assert(!weather::snapshot().valid);
                duringRequest = [] { weather::selectCity(330100); };
                break;
            case 5:
                assert(requests == 3 && !weather::snapshot().valid);  // stale Shanghai response rejected
                assert(weather::selectedCity() == "杭州市");
                duringRequest = {};
                break;
            case 6:
                assert(requests == 4 && weather::snapshot().valid && weather::snapshot().city == "杭州市");
                assert(requestedURL.find("%E6%9D%AD") != std::string::npos);
                hal.now += 1800000;
                networkError = true;
                break;
            case 7:
                assert(requests == 5 && weather::snapshot().valid && weather::snapshot().stale);
                break;
            case 8:
                assert(requests == 5);
                hal.connected = false;
                break;
            case 9:
                assert(requests == 5);
                hal.connected = true;
                networkError  = false;
                break;
            case 10:
                assert(requests == 6 && !weather::snapshot().stale);
                weather::setMode(weather::Mode::Off);
                hal.now += 1800000;
                break;
            case 11:
                assert(requests == 6);
                weather::setMode(weather::Mode::Weather);
                httpStatus = 503;
                break;
            case 12:
                assert(requests == 7 && weather::snapshot().valid && weather::snapshot().stale);
                assert(Settings::values["city_code"] == "330100");
                throw End{};
        }
    };
    try {
        taskEntry(nullptr);
    } catch (End&) {
    }
    assert(step == 12);
    // A fresh boot restores mode, manual location and cached weather from NVS.
    weather::init({
        [] { return GetHAL().getWifiStatus().connected; },
        [] { return GetHAL().millis(); },
        [](const std::string& key, int fallback) { return Settings("weather").GetInt(key, fallback); },
        [](const std::string& key, const std::string& fallback) { return Settings("weather").GetString(key, fallback); },
        [](const std::string& key, int value) { Settings("weather", true).SetInt(key, value); },
        [](const std::string& key, const std::string& value) { Settings("weather", true).SetString(key, value); },
        [](const std::string& key) { Settings("weather", true).EraseKey(key); },
        fakeWall,
    });
    assert(weather::selectedCityCode() == 330100 && weather::snapshot().valid && weather::snapshot().stale);
    weather::setMode(weather::Mode::Codex);
    weather::init({
        [] { return GetHAL().getWifiStatus().connected; },
        [] { return GetHAL().millis(); },
        [](const std::string& key, int fallback) { return Settings("weather").GetInt(key, fallback); },
        [](const std::string& key, const std::string& fallback) { return Settings("weather").GetString(key, fallback); },
        [](const std::string& key, int value) { Settings("weather", true).SetInt(key, value); },
        [](const std::string& key, const std::string& value) { Settings("weather", true).SetString(key, value); },
        [](const std::string& key) { Settings("weather", true).EraseKey(key); },
        fakeWall,
    });
    assert(weather::mode() == weather::Mode::Codex);
    atSleep = [] { assert(requests == 7); throw End{}; };
    try { taskEntry(nullptr); } catch (End&) {}
    Settings::values["mode"] = "99";
    weather::init({
        [] { return GetHAL().getWifiStatus().connected; },
        [] { return GetHAL().millis(); },
        [](const std::string& key, int fallback) { return Settings("weather").GetInt(key, fallback); },
        [](const std::string& key, const std::string& fallback) { return Settings("weather").GetString(key, fallback); },
        [](const std::string& key, int value) { Settings("weather", true).SetInt(key, value); },
        [](const std::string& key, const std::string& value) { Settings("weather", true).SetString(key, value); },
        [](const std::string& key) { Settings("weather", true).EraseKey(key); },
        fakeWall,
    });
    assert(weather::mode() == weather::Mode::Weather);
    httpStatus = 200;
    int phase = 0;
    int before = requests;
    atSleep = [&] {
        ++phase;
        hal.now += 1000;
        if (phase == 1) {
            assert(requests == before + 1 && weather::snapshot().fetchedAt > 0);
            wallOffset += 86400;
            auto invalid = weather::snapshot();
            assert(invalid.valid && invalid.stale && invalid.fetchedAt == 0);
            assert(Settings::values["time_trusted"] == "0");
        } else if (phase == 2) {
            assert(requests == before + 2 && weather::snapshot().fetchedAt > 0);
            assert(Settings::values["time_trusted"] == "1");
            hal.connected = false;
            wallOffset -= 172800;
            assert(weather::snapshot().fetchedAt == 0);
        } else if (phase == 3) {
            assert(requests == before + 2 && weather::snapshot().fetchedAt == 0);
            hal.connected = true;
            networkError = true;
        } else if (phase == 4) {
            assert(requests == before + 3 && weather::snapshot().fetchedAt == 0);
        } else if (phase == 5) {
            assert(requests == before + 3); // failed refresh retains retry backoff
            networkError = false;
            hal.now += 60000;
            duringRequest = [] { wallOffset += 3600; };
        } else if (phase == 6) {
            assert(weather::snapshot().fetchedAt == 0); // in-flight correction rejects response
            duringRequest = {};
        } else {
            assert(requests == before + 5 && weather::snapshot().fetchedAt > 0);
            throw End{};
        }
    };
    try { taskEntry(nullptr); } catch (End&) {}
    assert(phase == 7);
    wallOffset += 3600;
    assert(weather::snapshot().fetchedAt == 0);
    weather::init(weather::platform); // simulated reboot retains untrusted marker
    assert(weather::snapshot().valid && weather::snapshot().fetchedAt == 0);
    hal.now = UINT32_MAX - 1000;
    weather::init(weather::platform);
    const auto rev = weather::revision;
    hal.now = 999;
    wallOffset += 4294967; // wall time continues across the millis() wrap
    weather::snapshot();
    assert(weather::revision == rev);

    std::cout << "PASS: first-IP location, persisted city/cache, 30-minute refresh, disconnect/reconnect, retry delay, "
                 "HTTP failure, mode off, in-flight city race\n";
}
