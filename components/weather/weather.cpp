#include "weather.h"
#include "regions.h"
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
#include <atomic>

namespace weather {
namespace {
Config platform;
std::mutex mutex;
Mode currentMode = Mode::Weather;
int cityCode     = 0;
std::string cityName, query;
Snapshot cache;
uint32_t revision       = 0;
uint32_t fetchedTick    = 0;
bool restoredCache      = false;
TaskHandle_t taskHandle = nullptr;
uint32_t clockTick = 0;
std::time_t clockWall = 0;
// Called under mutex. Compare against monotonic elapsed time so normal time
// passing and millis() wrapping do not look like clock corrections.
void checkClock()
{
    const uint32_t tick = platform.millis();
    const auto wall = platform.wallTime();
    const auto drift = static_cast<int64_t>(wall) - static_cast<int64_t>(clockWall) -
                       uint32_t(tick - clockTick) / 1000;
    clockTick = tick;
    clockWall = wall;
    if (drift > 5 || drift < -5) {
        cache.stale = true;
        cache.fetchedAt = 0;
        platform.setInt("time_trusted", 0);
        ++revision;  // Forces a refresh and rejects any pre-correction request.
        if (taskHandle) xTaskNotifyGive(taskHandle);
    }
}
struct Body {
    std::string text;
    bool tooLarge = false;
};
esp_err_t receive(esp_http_client_event_t* event)
{
    auto* body = static_cast<Body*>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_DATA) {
        if (body->text.size() + event->data_len > 96 * 1024) {
            body->tooLarge = true;
            return ESP_FAIL;
        }
        body->text.append(static_cast<const char*>(event->data), event->data_len);
    }
    return ESP_OK;
}
bool request(const std::string& location, Snapshot& out, std::string& resolved)
{
    Body body;
    std::string url                 = "https://wttr.in/" + encodeLocation(location) + "?format=j1";
    esp_http_client_config_t config = {};
    config.url                      = url.c_str();
    config.timeout_ms               = 12000;
    config.crt_bundle_attach        = esp_crt_bundle_attach;
    config.event_handler            = receive;
    config.user_data                = &body;
    config.buffer_size              = 2048;
    config.user_agent               = "M5StopWatch/1.0";
    auto client                     = esp_http_client_init(&config);
    if (!client) return false;
    auto err   = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return err == ESP_OK && status == 200 && !body.tooLarge && parseResponse(body.text, out, resolved);
}
void task(void*)
{
    bool wasConnected = false, first = true;
    uint32_t lastAttempt = 0, seenRevision = 0, interval = 0;
    for (;;) {
        bool connected = platform.connected();
        uint32_t now = platform.millis();
        Mode selected;
        uint32_t rev;
        std::string target, name;
        {
            std::lock_guard<std::mutex> lock(mutex);
            checkClock();
            selected = currentMode;
            rev      = revision;
            target   = query;
            name     = cityName;
        }
        bool due = first || rev != seenRevision || !wasConnected || uint32_t(now - lastAttempt) >= interval;
        if (selected == Mode::Weather && connected && due) {
            Snapshot fresh;
            std::string resolved;
            bool success = request(target, fresh, resolved);
            lastAttempt  = platform.millis();
            first        = false;
            seenRevision = rev;
            interval     = success ? 30 * 60 * 1000 : 60 * 1000;
            std::lock_guard<std::mutex> lock(mutex);
            checkClock();
            if (rev == revision && currentMode == Mode::Weather) {
                if (success && (!target.empty() || (!resolved.empty() && !fresh.city.empty()))) {
                    fresh.fetchedAt = platform.wallTime();
                    if (!name.empty()) fresh.city = name;
                    cache         = fresh;
                    fetchedTick   = lastAttempt;
                    restoredCache = false;
                    if (target.empty()) {
                        query    = resolved;
                        cityName = fresh.city;
                        platform.setString("query", query);
                        platform.setString("city", cityName);
                    }
                    platform.setInt("temp", cache.temperature);
                    platform.setInt("code", cache.code);
                    platform.setString("cache_city", cache.city);
                    platform.setString("fetched", std::to_string(cache.fetchedAt));
                    platform.setInt("time_trusted", 1);
                } else {
                    cache.stale = true;
                    interval    = 60000;
                }
            }
        }
        wasConnected = connected;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
    }
}
}  // namespace
void init(const Config& config)
{
    platform = config;
    clockTick = platform.millis();
    clockWall = platform.wallTime();
    const int savedMode = platform.getInt("mode", 1);
    currentMode = savedMode >= 0 && savedMode <= 2 ? static_cast<Mode>(savedMode) : Mode::Weather;
    cityCode          = platform.getInt("city_code", 0);
    cityName          = platform.getString("city", "");
    query             = platform.getString("query", "");
    cache.code        = platform.getInt("code", 0);
    cache.temperature = platform.getInt("temp", 0);
    cache.city        = platform.getString("cache_city", "");
    cache.fetchedAt   = std::strtoll(platform.getString("fetched", "0").c_str(), nullptr, 10);
    cache.valid   = cache.fetchedAt > 0 && !cache.city.empty() && std::string(conditionName(cache.code)) != "Unknown";
    if (!platform.getInt("time_trusted", 1) || cache.fetchedAt > clockWall + 5) cache.fetchedAt = 0;
    cache.stale   = true;
    restoredCache = true;
    xTaskCreate(task, "weather", 8192, nullptr, 1, &taskHandle);
}
Mode mode()
{
    std::lock_guard<std::mutex> lock(mutex);
    return currentMode;
}
void setMode(Mode value)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (currentMode == value) return;
    currentMode = value;
    ++revision;
    platform.setInt("mode", static_cast<int>(value));
    if (taskHandle) xTaskNotifyGive(taskHandle);
}
std::string selectedCity()
{
    std::lock_guard<std::mutex> lock(mutex);
    return cityName;
}
int selectedCityCode()
{
    std::lock_guard<std::mutex> lock(mutex);
    return cityCode;
}
void selectCity(int code)
{
    const City* city = nullptr;
    for (size_t i = 0; i < cityCount; ++i)
        if (cities[i].code == code) {
            city = &cities[i];
            break;
        }
    if (!city) return;
    std::string province;
    for (size_t i = 0; i < provinceCount; ++i)
        if (provinces[i].code == city->province) province = provinces[i].name;
    std::lock_guard<std::mutex> lock(mutex);
    cityCode = code;
    cityName = city->name;
    query    = province == cityName ? cityName + ",China" : cityName + "," + province + ",China";
    cache    = Snapshot{};
    ++revision;
    platform.setInt("city_code", cityCode);
    platform.setString("city", cityName);
    platform.setString("query", query);
    platform.erase("fetched");
    platform.erase("cache_city");
    if (taskHandle) xTaskNotifyGive(taskHandle);
}
Snapshot snapshot()
{
    std::lock_guard<std::mutex> lock(mutex);
    checkClock();
    auto result  = cache;
    result.stale = result.stale || restoredCache || uint32_t(platform.millis() - fetchedTick) >= 30 * 60 * 1000;
    return result;
}
}  // namespace weather
