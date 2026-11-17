#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include "hal_api.inc"

using esp_err_t      = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1;
using esp_event_base_t             = int;
using esp_event_handler_instance_t = void*;
constexpr int WIFI_EVENT = 1, IP_EVENT = 2, ESP_EVENT_ANY_ID = -1;
constexpr int WIFI_EVENT_STA_START = 1, WIFI_EVENT_STA_STOP = 2, WIFI_EVENT_STA_CONNECTED = 3,
              WIFI_EVENT_STA_DISCONNECTED = 4, WIFI_EVENT_SCAN_DONE = 5;
constexpr int IP_EVENT_STA_GOT_IP = 1, IP_EVENT_STA_LOST_IP = 2;
constexpr int WIFI_REASON_AUTH_FAIL = 1, WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT = 2, WIFI_REASON_HANDSHAKE_TIMEOUT = 3,
              WIFI_REASON_NO_AP_FOUND = 4, WIFI_REASON_ASSOC_FAIL = 5, WIFI_REASON_BEACON_TIMEOUT = 6;
constexpr int WIFI_REASON_AUTH_EXPIRE = 7, WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY = 8,
              WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD = 9;
constexpr int WIFI_MODE_STA = 1, WIFI_IF_STA = 0, WPA3_SAE_PWE_BOTH = 2;
enum wifi_auth_mode_t {
    WIFI_AUTH_OPEN,
    WIFI_AUTH_WPA_PSK,
    WIFI_AUTH_WPA2_PSK,
    WIFI_AUTH_WPA_WPA2_PSK,
    WIFI_AUTH_WPA3_PSK,
    WIFI_AUTH_WPA2_WPA3_PSK,
    WIFI_AUTH_WPA2_ENTERPRISE
};
struct esp_netif_t {};
struct ip_addr {
    uint32_t addr;
};
struct ip_event_got_ip_t {
    struct {
        ip_addr ip;
    } ip_info;
    esp_netif_t* esp_netif;
};
struct wifi_event_sta_disconnected_t {
    uint16_t reason;
};
struct wifi_event_sta_scan_done_t {
    uint32_t status;
};
struct wifi_ap_record_t {
    uint8_t ssid[33]          = {};
    int rssi                  = 0;
    wifi_auth_mode_t authmode = WIFI_AUTH_OPEN;
};
struct wifi_config_t {
    struct {
        uint8_t ssid[32] = {}, password[64] = {};
        struct {
            wifi_auth_mode_t authmode = WIFI_AUTH_OPEN;
        } threshold;
        struct {
            bool capable = false;
        } pmf_cfg;
        int sae_pwe_h2e = 0;
    } sta;
};
struct Handler {
    int base;
    void (*callback)(void*, int, int, void*);
    void* context;
};
std::vector<Handler> handlers;
uint64_t clock_us = 1000000;
esp_netif_t netif;
wifi_config_t driver_config;
std::vector<wifi_ap_record_t> scan_records;
int stops = 0, connects = 0, starts = 0, scans = 0;
bool driver_running = false, enabled_setting = false;
int start_error = ESP_OK;
void emit(int base, int id, void* data = nullptr)
{
    for (const auto& h : handlers)
        if (h.base == base) h.callback(h.context, base, id, data);
}
int64_t esp_timer_get_time()
{
    return clock_us;
}
const char* esp_err_to_name(int)
{
    return "ESP_FAIL";
}
#define ESP_LOGW(tag, format, ...) ((void)(tag))
esp_netif_t* esp_netif_create_default_wifi_sta()
{
    return &netif;
}
void esp_ip4addr_ntoa(const ip_addr*, char* text, size_t size)
{
    std::snprintf(text, size, "192.168.1.10");
}
int esp_event_handler_instance_register(int base, int, void (*cb)(void*, int, int, void*), void* arg, void** handle)
{
    handlers.push_back({base, cb, arg});
    *handle = arg;
    return ESP_OK;
}
int esp_wifi_set_mode(int)
{
    return ESP_OK;
}
int esp_wifi_set_config(int, const wifi_config_t* config)
{
    driver_config = *config;
    return ESP_OK;
}
int esp_wifi_start()
{
    ++starts;
    if (start_error != ESP_OK) return start_error;
    driver_running = true;
    emit(WIFI_EVENT, WIFI_EVENT_STA_START);
    return ESP_OK;
}
int esp_wifi_stop()
{
    ++stops;
    driver_running = false;
    emit(WIFI_EVENT, WIFI_EVENT_STA_STOP);
    return ESP_OK;
}
int esp_wifi_connect()
{
    ++connects;
    return ESP_OK;
}
int esp_wifi_scan_start(void*, bool)
{
    ++scans;
    return ESP_OK;
}
int esp_wifi_scan_stop()
{
    return ESP_OK;
}
int esp_wifi_clear_ap_list()
{
    return ESP_OK;
}
int esp_wifi_scan_get_ap_num(uint16_t* count)
{
    *count = scan_records.size();
    return ESP_OK;
}
int esp_wifi_scan_get_ap_records(uint16_t* count, wifi_ap_record_t* records)
{
    std::copy_n(scan_records.begin(), *count, records);
    return ESP_OK;
}
int esp_wifi_sta_get_ap_info(wifi_ap_record_t* ap)
{
    std::memcpy(ap->ssid, driver_config.sta.ssid, 32);
    return ESP_OK;
}
struct SsidItem {
    std::string ssid, password;
};
class SsidManager {
public:
    static SsidManager& GetInstance()
    {
        static SsidManager manager;
        return manager;
    }
    std::vector<SsidItem> items;
    const std::vector<SsidItem>& GetSsidList()
    {
        return items;
    }
    void AddSsid(const std::string& ssid, const std::string& password)
    {
        for (auto& item : items)
            if (item.ssid == ssid) {
                item.password = password;
                return;
            }
        items.insert(items.begin(), {ssid, password});
    }
    void SetDefaultSsid(int index)
    {
        std::rotate(items.begin(), items.begin() + index, items.begin() + index + 1);
    }
    void RemoveSsid(int index)
    {
        items.erase(items.begin() + index);
    }
};
class Settings {
public:
    Settings(const char*, bool = false)
    {
    }
    bool GetBool(const char*, bool)
    {
        return enabled_setting;
    }
    void SetBool(const char*, bool value)
    {
        enabled_setting = value;
    }
};
enum class WifiEvent { ConfigModeExit };
struct WifiManagerConfig {
    std::string ssid_prefix, language;
};
class WifiManager {
public:
    bool ap = false;
    std::function<void(WifiEvent, const std::string&)> callback;
    static WifiManager& GetInstance()
    {
        static WifiManager manager;
        return manager;
    }
    bool Initialize(const WifiManagerConfig&)
    {
        return true;
    }
    void SetEventCallback(decltype(callback) cb)
    {
        callback = cb;
    }
    void StartConfigAp()
    {
        ap = true;
    }
    void StopConfigAp()
    {
        ap = false;
        callback(WifiEvent::ConfigModeExit, "");
    }
    std::string GetApSsid()
    {
        return "Setup AP";
    }
    std::string GetApWebUrl()
    {
        return "http://192.168.4.1";
    }
};
#include "hal_network_under_test.inc"

void reset(bool saved = true)
{
    handlers.clear();
    clock_us = 1000000;
    stops = connects = starts = scans = 0;
    driver_running                    = false;
    start_error                       = ESP_OK;
    scan_records.clear();
    enabled_setting                  = saved;
    SsidManager::GetInstance().items = saved ? std::vector<SsidItem>{{"A", "passwordA"}} : std::vector<SsidItem>{};
    WifiManager::GetInstance().ap    = false;
}
void pump(NetworkController& c)
{
    c.tick();
    c.tick();
    c.tick();
}
void got_ip(NetworkController& c)
{
    emit(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED);
    ip_event_got_ip_t event{{{1}}, &netif};
    emit(IP_EVENT, IP_EVENT_STA_GOT_IP, &event);
    c.tick();
}
void advance(NetworkController& c, uint32_t ms)
{
    clock_us += uint64_t(ms) * 1000;
    c.tick();
}
Hal::WifiNetwork personal(const char* ssid)
{
    return {ssid, Hal::WifiSecurity::Personal};
}
int main()
{
    {
        reset();
        NetworkController c;
        c.init();
        pump(c);
        got_ip(c);
        assert(c.status().connected && c.status().ssid == "A");
        int before = stops;
        c.start_scan();
        pump(c);
        assert(scans == 1 && stops == before && c.status().connected);
        c.cancel_connection();
        c.end_scan();
        assert(stops == before && c.status().connected);
        std::cout << "PASS scan, cancel-before-confirm and leave page preserve A\n";
    }
    {
        reset();
        NetworkController c;
        c.init();
        pump(c);
        got_ip(c);
        c.connect(personal("B"), "passwordB");
        pump(c);
        c.cancel_connection();
        // Late success from B while the replacement waits for STOP must be ignored.
        emit(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED);
        ip_event_got_ip_t event{{{1}}, &netif};
        emit(IP_EVENT, IP_EVENT_STA_GOT_IP, &event);
        pump(c);
        assert(c.connection_status().state == Hal::WifiConnectState::Idle);
        assert(!c.status().connected && SsidManager::GetInstance().items.front().ssid == "A");
        got_ip(c);
        assert(c.status().ssid == "A" && c.status().connected);
        std::cout << "PASS cancel B rejects late success and restores A\n";
    }
    {
        reset();
        NetworkController c;
        c.init();
        pump(c);
        got_ip(c);
        c.connect(personal("B"), "wrongpass");
        pump(c);
        wifi_event_sta_disconnected_t failure{WIFI_REASON_AUTH_FAIL};
        emit(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &failure);
        pump(c);
        assert(c.connection_status().state == Hal::WifiConnectState::Failed);
        assert(c.connection_status().message.find("Authentication") != std::string::npos);
        assert(SsidManager::GetInstance().items.front().password == "passwordA");
        got_ip(c);
        assert(c.status().ssid == "A");
        std::cout << "PASS wrong password leaves stored credentials intact and restores A\n";
    }
    {
        reset();
        NetworkController c;
        c.init();
        pump(c);
        got_ip(c);
        c.connect(personal("B"), "passwordB");
        pump(c);
        emit(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED);
        c.tick();
        assert(c.connection_status().state == Hal::WifiConnectState::Connecting);
        advance(c, 19999);
        assert(c.connection_status().state == Hal::WifiConnectState::Connecting);
        advance(c, 1);
        assert(c.connection_status().state == Hal::WifiConnectState::Failed);
        assert(SsidManager::GetInstance().items.front().ssid == "A");
        std::cout << "PASS DHCP required and 20-second timeout restores saved profile\n";
    }
    {
        reset();
        NetworkController c;
        c.init();
        pump(c);
        got_ip(c);
        c.connect(personal("B"), "passwordB");
        pump(c);
        got_ip(c);
        assert(c.connection_status().state == Hal::WifiConnectState::Succeeded);
        assert(SsidManager::GetInstance().items.size() == 1 && SsidManager::GetInstance().items[0].ssid == "B");
        int before = stops;
        c.end_scan();
        assert(stops == before && c.status().connected);
        wifi_event_sta_disconnected_t failure{WIFI_REASON_BEACON_TIMEOUT};
        emit(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &failure);
        c.tick();
        assert(!c.status().connected && c.connection_status().state == Hal::WifiConnectState::Idle);
        advance(c, 5000);
        pump(c);
        got_ip(c);
        assert(c.status().ssid == "B");
        std::cout << "PASS success saves only B, survives exit, and silently reconnects\n";
    }
    {
        reset(false);
        NetworkController c;
        c.init();
        c.start_scan();
        pump(c);
        c.connect({"Open", Hal::WifiSecurity::Open}, "");
        pump(c);
        got_ip(c);
        assert(c.status().connected && enabled_setting);
        assert(SsidManager::GetInstance().items.front().password.empty());
        int before = stops;
        c.connect({"Enterprise", Hal::WifiSecurity::Unsupported}, "password");
        assert(c.connection_status().state == Hal::WifiConnectState::Failed && before == stops);
        c.connect(personal("Short"), "123");
        assert(c.connection_status().state == Hal::WifiConnectState::Failed && before == stops);
        std::cout << "PASS open network enables persistence; unsupported/invalid input preserves connection\n";
    }
    {
        reset();
        NetworkController c;
        c.init();
        pump(c);
        got_ip(c);
        c.set_enabled(false, true);
        pump(c);
        advance(c, 30000);
        assert(!c.status().connected && !driver_running);
        c.set_enabled(true, true);
        pump(c);
        got_ip(c);
        assert(c.status().connected);
        c.enter_badge();
        pump(c);
        assert(c.status().badgeMode);
        c.exit_badge();
        pump(c);
        got_ip(c);
        assert(c.status().connected);
        std::cout << "PASS Wi-Fi switch and Badge ownership transitions\n";
    }
    {
        reset();
        start_error = ESP_FAIL;
        NetworkController c;
        c.init();
        pump(c);
        int before = starts;
        pump(c);
        assert(starts == before);
        advance(c, 5000);
        pump(c);
        assert(starts == before + 1);
        std::cout << "PASS radio start errors back off instead of busy retrying\n";
    }
}
