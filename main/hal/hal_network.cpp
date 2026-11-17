/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "utils/settings/settings.h"
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <ssid_manager.h>
#include <wifi_manager.h>
#include <algorithm>
#include <cstring>
#include <deque>
#include <mutex>

namespace {
constexpr const char* TAG          = "HAL-WiFi";
constexpr uint32_t CONNECT_TIMEOUT = 20000;
constexpr uint32_t RETRY_INTERVAL  = 5000;
uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}
bool expired(uint32_t now, uint32_t deadline)
{
    return deadline && static_cast<int32_t>(now - deadline) >= 0;
}

Hal::WifiSecurity security(wifi_auth_mode_t auth)
{
    switch (auth) {
        case WIFI_AUTH_OPEN:
            return Hal::WifiSecurity::Open;
        case WIFI_AUTH_WPA_PSK:
        case WIFI_AUTH_WPA2_PSK:
        case WIFI_AUTH_WPA_WPA2_PSK:
        case WIFI_AUTH_WPA3_PSK:
        case WIFI_AUTH_WPA2_WPA3_PSK:
            return Hal::WifiSecurity::Personal;
        default:
            return Hal::WifiSecurity::Unsupported;
    }
}

std::string disconnect_reason(uint16_t reason)
{
    switch (reason) {
        case WIFI_REASON_AUTH_FAIL:
            return "Authentication failed. Check password.";
        case WIFI_REASON_AUTH_EXPIRE:
            return "Authentication timed out. Check router or signal.";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "Handshake failed. Check password or signal.";
        case WIFI_REASON_NO_AP_FOUND:
            return "Network not found. Check router or signal.";
        case WIFI_REASON_ASSOC_FAIL:
            return "Router refused the connection.";
        case WIFI_REASON_BEACON_TIMEOUT:
            return "Wi-Fi signal lost.";
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
        case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
            return "Network security is incompatible.";
        default:
            return "Connection failed (reason " + std::to_string(reason) + ").";
    }
}

// The event task only queues notifications. All driver transitions, storage and
// UI-visible state changes are serialized here, never inside an event callback.
class NetworkController {
    enum class Owner { None, Station, ConfigAp, Badge };
    enum class EventKind { Started, Stopped, Associated, Disconnected, GotIp, LostIp, ScanDone, ApExit };
    struct Event {
        EventKind kind;
        uint32_t value     = 0;
        esp_netif_t* netif = nullptr;
        std::string ip     = {};
    };
    struct Credentials {
        std::string ssid;
        std::string password;
        Hal::WifiSecurity security = Hal::WifiSecurity::Personal;
    };

public:
    void init()
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _status.enabled = load_enabled();
        load_saved();
        if (_status.enabled) resume();
    }

    void tick()
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        std::deque<Event> events;
        {
            std::lock_guard<std::mutex> event_lock(_event_mutex);
            events.swap(_events);
        }
        for (const auto& event : events) handle(event);
        const auto now = now_ms();
        if (_owner != Owner::Station) return;
        if (_scan.scanning && expired(now, _scan_deadline)) {
            esp_wifi_scan_stop();
            esp_wifi_clear_ap_list();
            _scan.scanning = false;
            _scan.message  = "Scan timed out. Tap Refresh.";
        }
        // A STOP notification is the barrier between connection attempts. Old
        // disconnect/IP events cannot complete a replacement or cancelled attempt.
        if (_start_pending && !_waiting_stop) start_radio();
        if ((_manual || _attempting) && expired(now, _connect_deadline)) {
            fail("Connection timed out. Could not connect or obtain IP.");
        }
        if (!_attempting && !_status.connected && !_start_pending && !_waiting_stop && _status.enabled &&
            !_saved.ssid.empty() && !_scan.scanning && expired(now, _retry_at)) {
            request_station(_saved);
        }
        if (_scan_requested && !_start_pending && !_waiting_stop && _radio_running && !_attempting) {
            begin_scan();
        }
    }

    void set_enabled(bool enabled, bool save)
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (save) {
            Settings settings("system", true);
            settings.SetBool("wifi_enabled", enabled);
        }
        _status.enabled = enabled;
        _manual         = false;
        _connection     = {};
        if (_owner == Owner::Badge) return;
        if (enabled)
            resume();
        else {
            stop_owner();
            set_disconnected(Hal::WifiMode::Off, "Wi-Fi off");
        }
    }
    bool is_enabled(bool stored)
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return stored ? load_enabled() : _status.enabled;
    }
    Hal::WifiStatus status()
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _status;
    }
    Hal::WifiScanStatus scan_status()
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _scan;
    }
    Hal::WifiConnectStatus connection_status()
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        return _connection;
    }

    void start_scan()
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (_owner == Owner::Badge) {
            _scan.message = "Exit Badge mode to scan.";
            return;
        }
        if (_scan.scanning || _scan_requested) return;
        if (!initialize()) {
            _scan.message = "Wi-Fi initialization failed";
            return;
        }
        _scan_requested = true;
        _scan.message   = "Waiting to scan...";
        // A connected station stays up throughout the scan. Only AP ownership
        // needs to be released; background retries pause while scanning.
        if (_owner != Owner::Station) request_station({});
    }

    void end_scan()
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        cancel_connection();
        stop_scan();
        // Leaving the page never tears down a successful station connection.
        if (!_status.enabled && _owner == Owner::Station) {
            stop_owner();
            set_disconnected(Hal::WifiMode::Off, "Wi-Fi off");
        } else if (_status.enabled && _saved.ssid.empty() && _owner == Owner::Station) {
            resume();
        }
    }

    void connect(const Hal::WifiNetwork& network, const std::string& password)
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (_connection.state == Hal::WifiConnectState::Connecting) return;
        auto reject = [this](const char* message) { _connection = {Hal::WifiConnectState::Failed, message}; };
        if (_owner == Owner::Badge) {
            reject("Exit Badge mode to connect.");
            return;
        }
        if (network.name.empty() || network.name.size() > 32) {
            reject("Invalid network name.");
            return;
        }
        if (network.security == Hal::WifiSecurity::Unsupported) {
            reject("Unsupported authentication method.");
            return;
        }
        const bool hex_psk = password.size() == 64 && std::all_of(password.begin(), password.end(), [](char c) {
                                 return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
                             });
        if (network.security != Hal::WifiSecurity::Open && !hex_psk && (password.size() < 8 || password.size() > 63)) {
            reject("Use 8-63 characters or a 64-digit hex key.");
            return;
        }
        if (!initialize()) {
            reject("Wi-Fi initialization failed.");
            return;
        }
        _manual           = true;
        _connection       = {Hal::WifiConnectState::Connecting, "Connecting..."};
        _connect_deadline = now_ms() + CONNECT_TIMEOUT;
        _scan_requested   = false;
        request_station({network.name, network.security == Hal::WifiSecurity::Open ? "" : password, network.security});
    }

    void cancel_connection()
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        if (_manual) {
            _manual = false;
            restore();
        }
        _connection = {};
    }

    void enter_badge()
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        initialize();
        _manual     = false;
        _connection = {};
        stop_owner();
        _owner = Owner::Badge;
        set_disconnected(Hal::WifiMode::BadgeAp, "Badge edit AP");
        _status.badgeMode = true;
        _status.apUrl     = "http://192.168.4.1";
    }
    void exit_badge()
    {
        std::lock_guard<std::recursive_mutex> lock(_mutex);
        _owner            = Owner::None;
        _status.badgeMode = false;
        resume();
    }

private:
    void enqueue(Event event)
    {
        std::lock_guard<std::mutex> lock(_event_mutex);
        _events.push_back(std::move(event));
    }
    bool initialize()
    {
        if (_initialized) return true;
        WifiManagerConfig config;
        config.ssid_prefix = "M5StopWatch";
        config.language    = "en-US";
        auto& manager      = WifiManager::GetInstance();
        manager.SetEventCallback([this](WifiEvent event, const std::string&) {
            if (event == WifiEvent::ConfigModeExit) enqueue({EventKind::ApExit});
        });
        if (!manager.Initialize(config)) return false;
        if (!_netif) _netif = esp_netif_create_default_wifi_sta();
        if (!_netif) return false;
        if (!_wifi_handler &&
            esp_event_handler_instance_register(
                WIFI_EVENT, ESP_EVENT_ANY_ID,
                [](void* arg, esp_event_base_t, int32_t id, void* data) {
                    auto* self = static_cast<NetworkController*>(arg);
                    switch (id) {
                        case WIFI_EVENT_STA_START:
                            self->enqueue({EventKind::Started});
                            break;
                        case WIFI_EVENT_STA_STOP:
                            self->enqueue({EventKind::Stopped});
                            break;
                        case WIFI_EVENT_STA_CONNECTED:
                            self->enqueue({EventKind::Associated});
                            break;
                        case WIFI_EVENT_STA_DISCONNECTED:
                            self->enqueue(
                                {EventKind::Disconnected, static_cast<wifi_event_sta_disconnected_t*>(data)->reason});
                            break;
                        case WIFI_EVENT_SCAN_DONE:
                            self->enqueue(
                                {EventKind::ScanDone, static_cast<wifi_event_sta_scan_done_t*>(data)->status});
                            break;
                        default:
                            break;
                    }
                },
                this, &_wifi_handler) != ESP_OK)
            return false;
        if (!_ip_handler && esp_event_handler_instance_register(
                                IP_EVENT, ESP_EVENT_ANY_ID,
                                [](void* arg, esp_event_base_t, int32_t id, void* data) {
                                    auto* self = static_cast<NetworkController*>(arg);
                                    if (id == IP_EVENT_STA_GOT_IP) {
                                        auto* event = static_cast<ip_event_got_ip_t*>(data);
                                        char ip[16];
                                        esp_ip4addr_ntoa(&event->ip_info.ip, ip, sizeof(ip));
                                        self->enqueue({EventKind::GotIp, event->ip_info.ip.addr, event->esp_netif, ip});
                                    } else if (id == IP_EVENT_STA_LOST_IP)
                                        self->enqueue({EventKind::LostIp});
                                },
                                this, &_ip_handler) != ESP_OK)
            return false;
        _initialized = true;
        return true;
    }
    bool load_enabled()
    {
        Settings settings("system");
        return settings.GetBool("wifi_enabled", false);
    }
    void load_saved()
    {
        const auto& list = SsidManager::GetInstance().GetSsidList();
        if (!list.empty()) {
            _saved = {list.front().ssid, list.front().password,
                      list.front().password.empty() ? Hal::WifiSecurity::Open : Hal::WifiSecurity::Personal};
        }
    }
    void save_success()
    {
        // Do not clear the old credentials first: cancelled/failed attempts must
        // never overwrite them. Keep only this successful network afterwards.
        auto& manager = SsidManager::GetInstance();
        manager.AddSsid(_target.ssid, _target.password);
        const auto& list = manager.GetSsidList();
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].ssid == _target.ssid) {
                manager.SetDefaultSsid(i);
                break;
            }
        }
        while (manager.GetSsidList().size() > 1) manager.RemoveSsid(1);
        _saved = _target;
        Settings settings("system", true);
        settings.SetBool("wifi_enabled", true);
        _status.enabled = true;
    }
    void set_disconnected(Hal::WifiMode mode, const std::string& message)
    {
        const bool enabled = _status.enabled;
        _status            = {};
        _status.enabled    = enabled;
        _status.mode       = mode;
        _status.message    = message;
        _associated        = false;
    }
    void stop_scan()
    {
        if (_scan_requested || _scan.scanning) {
            _scan.message = _scan.networks.empty() ? "Tap Refresh to scan." : "Available networks";
        }
        _scan_requested = false;
        if (_scan.scanning) {
            esp_wifi_scan_stop();
            esp_wifi_clear_ap_list();
        }
        _scan.scanning = false;
    }
    void stop_owner()
    {
        auto previous  = _owner;
        _owner         = Owner::None;
        _start_pending = false;
        _attempting    = false;
        _retry_at      = 0;
        stop_scan();
        if (previous == Owner::ConfigAp) {
            WifiManager::GetInstance().StopConfigAp();
        } else if (previous == Owner::Station && _radio_running) {
            _waiting_stop = esp_wifi_stop() == ESP_OK;
        }
        _radio_running = false;
    }
    void resume()
    {
        if (!_status.enabled) {
            set_disconnected(Hal::WifiMode::Off, "Wi-Fi off");
            return;
        }
        if (!initialize()) {
            set_disconnected(Hal::WifiMode::Error, "Wi-Fi initialization failed");
            return;
        }
        if (!_saved.ssid.empty())
            request_station(_saved);
        else {
            stop_owner();
            _owner        = Owner::ConfigAp;
            auto& manager = WifiManager::GetInstance();
            manager.StartConfigAp();
            set_disconnected(Hal::WifiMode::ConfigAp, "No saved Wi-Fi");
            _status.configMode = true;
            _status.apSsid     = manager.GetApSsid();
            _status.apUrl      = manager.GetApWebUrl();
        }
    }
    void request_station(const Credentials& credentials)
    {
        const bool scan_requested = _scan_requested;
        stop_owner();
        _scan_requested = scan_requested;
        _owner          = Owner::Station;
        _target         = credentials;
        _start_pending  = true;
        set_disconnected(credentials.ssid.empty() ? Hal::WifiMode::Off : Hal::WifiMode::Connecting,
                         credentials.ssid.empty() ? "Ready to scan" : "Connecting...");
        _status.ssid     = credentials.ssid;
        _status.security = credentials.security;
    }
    void start_radio()
    {
        _start_pending       = false;
        wifi_config_t config = {};
        memcpy(config.sta.ssid, _target.ssid.data(), _target.ssid.size());
        memcpy(config.sta.password, _target.password.data(), _target.password.size());
        config.sta.threshold.authmode =
            _target.security == Hal::WifiSecurity::Open ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
        config.sta.pmf_cfg.capable = true;
        config.sta.sae_pwe_h2e     = WPA3_SAE_PWE_BOTH;
        esp_err_t err              = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &config);
        if (err == ESP_OK) err = esp_wifi_start();
        if (err != ESP_OK) {
            const bool manual = _manual;
            fail(std::string("Wi-Fi start failed: ") + esp_err_to_name(err));
            if (!manual) {
                _start_pending = false;
                _retry_at      = now_ms() + RETRY_INTERVAL;
            }
            return;
        }
        _radio_running = true;
        _attempting    = !_target.ssid.empty();
        if (!_manual) _connect_deadline = now_ms() + CONNECT_TIMEOUT;
    }
    void restore()
    {
        if (_status.enabled && !_saved.ssid.empty())
            request_station(_saved);
        else
            request_station({});
    }
    void fail(const std::string& message)
    {
        _attempting = false;
        if (_manual) {
            _manual     = false;
            _connection = {Hal::WifiConnectState::Failed, message};
            restore();
        } else {
            // Stop a timed-out association/DHCP exchange before scheduling retry.
            request_station({});
            _retry_at       = now_ms() + RETRY_INTERVAL;
            _status.message = message;
        }
    }
    void begin_scan()
    {
        _scan_requested = false;
        _scan.scanning  = true;
        _scan.message   = "Scanning...";
        _scan_deadline  = now_ms() + 15000;
        const auto err  = esp_wifi_scan_start(nullptr, false);
        if (err != ESP_OK) {
            _scan.scanning = false;
            _scan.message  = "Scan failed. Tap Refresh.";
            ESP_LOGW(TAG, "Scan: %s", esp_err_to_name(err));
        }
    }
    void scan_done(uint32_t result)
    {
        if (!_scan.scanning) return;
        _scan.scanning = false;
        uint16_t count = 0;
        auto err       = result == 0 ? esp_wifi_scan_get_ap_num(&count) : ESP_FAIL;
        std::vector<wifi_ap_record_t> records(count);
        if (err == ESP_OK && count) err = esp_wifi_scan_get_ap_records(&count, records.data());
        esp_wifi_clear_ap_list();
        if (err != ESP_OK) {
            _scan.message = "Scan failed. Tap Refresh.";
            return;
        }
        records.resize(count);
        std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) { return a.rssi > b.rssi; });
        _scan.networks.clear();
        for (const auto& record : records) {
            std::string name(reinterpret_cast<const char*>(record.ssid),
                             strnlen(reinterpret_cast<const char*>(record.ssid), sizeof(record.ssid)));
            auto auth = security(record.authmode);
            // Keep distinct security types separate even if the SSID is identical.
            if (!name.empty() && std::none_of(_scan.networks.begin(), _scan.networks.end(),
                                              [&](const auto& n) { return n.name == name && n.security == auth; }))
                _scan.networks.push_back({name, auth});
        }
        _scan.message = _scan.networks.empty() ? "No networks found" : "Available networks";
    }
    void handle(const Event& event)
    {
        if (event.kind == EventKind::Stopped) {
            _waiting_stop = false;
            return;
        }
        if (event.kind == EventKind::ApExit) {
            if (_owner == Owner::ConfigAp) {
                _owner = Owner::None;
                load_saved();
                resume();
            }
            return;
        }
        if (_owner != Owner::Station || _waiting_stop || _start_pending) return;
        switch (event.kind) {
            case EventKind::Started:
                if (_attempting) {
                    auto err = esp_wifi_connect();
                    if (err != ESP_OK) fail(std::string("Connection failed: ") + esp_err_to_name(err));
                }
                break;
            case EventKind::Associated:
                _associated = _attempting;
                break;
            case EventKind::GotIp: {
                if (!_associated || event.netif != _netif || !event.value) break;
                wifi_ap_record_t ap = {};
                if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK ||
                    _target.ssid != std::string(reinterpret_cast<char*>(ap.ssid),
                                                strnlen(reinterpret_cast<char*>(ap.ssid), sizeof(ap.ssid))))
                    break;
                _attempting       = false;
                _retry_at         = 0;
                _status.connected = true;
                _status.mode      = Hal::WifiMode::Connected;
                _status.ssid      = _target.ssid;
                _status.ipAddress = event.ip;
                _status.message   = "Connected";
                if (_manual) {
                    save_success();
                    _manual     = false;
                    _connection = {Hal::WifiConnectState::Succeeded, "Connected"};
                }
                break;
            }
            case EventKind::Disconnected:
            case EventKind::LostIp:
                _associated       = false;
                _status.connected = false;
                _status.ipAddress.clear();
                _status.mode = _status.enabled ? Hal::WifiMode::Connecting : Hal::WifiMode::Off;
                if (_manual)
                    fail(event.kind == EventKind::LostIp ? "IP address lost." : disconnect_reason(event.value));
                else {
                    _attempting     = false;
                    _retry_at       = now_ms() + RETRY_INTERVAL;
                    _status.message = "Disconnected. Retrying...";
                }
                break;
            case EventKind::ScanDone:
                scan_done(event.value);
                break;
            default:
                break;
        }
    }
    std::recursive_mutex _mutex;
    std::mutex _event_mutex;
    std::deque<Event> _events;
    bool _initialized                          = false;
    esp_netif_t* _netif                        = nullptr;
    esp_event_handler_instance_t _wifi_handler = nullptr;
    esp_event_handler_instance_t _ip_handler   = nullptr;
    Owner _owner                               = Owner::None;
    bool _radio_running                        = false;
    bool _waiting_stop                         = false;
    bool _start_pending                        = false;
    bool _associated                           = false;
    bool _attempting                           = false;
    bool _manual                               = false;
    bool _scan_requested                       = false;
    uint32_t _scan_deadline                    = 0;
    uint32_t _connect_deadline                 = 0;
    uint32_t _retry_at                         = 0;
    Credentials _target, _saved;
    Hal::WifiStatus _status;
    Hal::WifiScanStatus _scan;
    Hal::WifiConnectStatus _connection;
};
NetworkController& controller()
{
    static NetworkController instance;
    return instance;
}
}  // namespace

void Hal::wifi_init()
{
    controller().init();
}
void Hal::wifi_tick()
{
    controller().tick();
}
void Hal::setWifiEnabled(bool enabled, bool save)
{
    controller().set_enabled(enabled, save);
}
bool Hal::isWifiEnabled(bool stored)
{
    return controller().is_enabled(stored);
}
Hal::WifiStatus Hal::getWifiStatus()
{
    return controller().status();
}
void Hal::wifi_enter_badge_ap()
{
    controller().enter_badge();
}
void Hal::wifi_exit_badge_ap()
{
    controller().exit_badge();
}
void Hal::startWifiScan()
{
    controller().start_scan();
}
Hal::WifiScanStatus Hal::getWifiScanStatus()
{
    return controller().scan_status();
}
void Hal::endWifiScan()
{
    controller().end_scan();
}
void Hal::connectWifi(const WifiNetwork& network, const std::string& password)
{
    controller().connect(network, password);
}
Hal::WifiConnectStatus Hal::getWifiConnectStatus()
{
    return controller().connection_status();
}
void Hal::cancelWifiConnection()
{
    controller().cancel_connection();
}
