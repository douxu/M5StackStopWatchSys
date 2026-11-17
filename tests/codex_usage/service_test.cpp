#include "codex_usage.h"
#include <weather.h>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>

using esp_err_t      = int;
using httpd_handle_t = void*;
constexpr int ESP_OK = 0, ESP_FAIL = -1, HTTPD_403_FORBIDDEN = 403, HTTPD_400_BAD_REQUEST = 400, HTTP_POST = 1;
#define pdMS_TO_TICKS(x) (x)
struct Hal {
    uint32_t now = 100;
    struct Net {
        bool connected = true, configMode = false, badgeMode = false;
    } net;
    Net getWifiStatus()
    {
        return net;
    }
    uint32_t millis()
    {
        return now;
    }
} hal;
Hal& GetHAL()
{
    return hal;
}
namespace weather {
Mode selected = Mode::Codex;
Mode mode()
{
    return selected;
}
}  // namespace weather
struct Settings {
    static std::map<std::string, std::string> data;
    Settings(const std::string&, bool)
    {
    }
    std::string GetString(const std::string& key)
    {
        return data[key];
    }
    void SetString(const std::string& key, const std::string& value)
    {
        data[key] = value;
    }
};
std::map<std::string, std::string> Settings::data;
uint32_t esp_random()
{
    return 0x1234abcd;
}
void (*taskEntry)(void*);
int xTaskCreate(void (*entry)(void*), const char*, int, void*, int, void*)
{
    taskEntry = entry;
    return 1;
}
std::function<void()> atSleep;
void vTaskDelay(int)
{
    atSleep();
}
struct httpd_req_t {
    size_t content_len = 0;
    std::string header, body, response;
    size_t offset = 0;
    int status    = 200;
    bool failRead = false;
};
struct httpd_config_t {
    int server_port = 80, ctrl_port = 32768, stack_size = 4096, max_open_sockets = 7;
    bool lru_purge_enable = false;
    int recv_wait_timeout = 5, send_wait_timeout = 5;
};
#define HTTPD_DEFAULT_CONFIG() \
    httpd_config_t             \
    {                          \
    }
struct httpd_uri_t {
    const char* uri                    = nullptr;
    int method                         = 0;
    esp_err_t (*handler)(httpd_req_t*) = nullptr;
} registered;
int starts = 0, stops = 0;
int httpd_start(httpd_handle_t* server, httpd_config_t* cfg)
{
    assert(cfg->server_port == 8765 && cfg->ctrl_port != 32768);
    ++starts;
    *server = reinterpret_cast<void*>(1);
    return ESP_OK;
}
int httpd_stop(httpd_handle_t)
{
    ++stops;
    return ESP_OK;
}
int httpd_register_uri_handler(httpd_handle_t, httpd_uri_t* uri)
{
    registered = *uri;
    return ESP_OK;
}
int httpd_req_get_hdr_value_str(httpd_req_t* req, const char*, char* out, size_t size)
{
    if (req->header.empty() || req->header.size() >= size) return ESP_FAIL;
    std::strcpy(out, req->header.c_str());
    return ESP_OK;
}
int httpd_req_recv(httpd_req_t* req, char* out, size_t size)
{
    if (req->failRead) return -1;
    size_t count = std::min({size, req->body.size() - req->offset, size_t(17)});
    std::memcpy(out, req->body.data() + req->offset, count);
    req->offset += count;
    return count;
}
int httpd_resp_send_err(httpd_req_t* req, int status, const char*)
{
    req->status = status;
    return ESP_OK;
}
void httpd_resp_set_type(httpd_req_t*, const char*)
{
}
int httpd_resp_sendstr(httpd_req_t* req, const char* body)
{
    req->response = body;
    return ESP_OK;
}
#include "service_under_test.inc"

httpd_req_t request(std::string body, std::string code = codex_usage::pairingCode())
{
    httpd_req_t req;
    req.header      = code;
    req.body        = body;
    req.content_len = body.size();
    registered.handler(&req);
    return req;
}
struct End {};
int main()
{
    using namespace codex_usage;
    init({
        [] { return weather::selected == weather::Mode::Codex && hal.net.connected && !hal.net.configMode && !hal.net.badgeMode; },
        [] { return hal.now; },
        [] { return Settings::data["pair_code"]; },
        [](const std::string& value) { Settings::data["pair_code"] = value; },
    });
    assert(pairingCode() == "1234abcdabcd");
    const auto saved = pairingCode();
    init({
        [] { return weather::selected == weather::Mode::Codex && hal.net.connected && !hal.net.configMode && !hal.net.badgeMode; },
        [] { return hal.now; },
        [] { return Settings::data["pair_code"]; },
        [](const std::string& value) { Settings::data["pair_code"] = value; },
    });
    assert(pairingCode() == saved);
    int step               = 0;
    const std::string body = R"({"version":1,"source":"Mac","primary":{"usedPercent":37}})";
    atSleep                = [&] {
        switch (++step) {
            case 1: {
                assert(starts == 1 && registered.method == HTTP_POST);
                assert(!snapshot().valid);
                assert(request(body, "wrong").status == 403);
                assert(!snapshot().valid);
                auto good = request(body);
                assert(good.status == 200 && good.response == "{\"ok\":true}");
                assert(snapshot().valid && !snapshot().stale && snapshot().primary.usedPercent == 37);
                assert(request("{}").status == 400);
                assert(request(std::string(2049, 'x')).status == 400);
                httpd_req_t partial;
                partial.header      = saved;
                partial.content_len = 100;
                partial.failRead    = true;
                assert(registered.handler(&partial) == ESP_FAIL);
                assert(snapshot().primary.usedPercent == 37);
                hal.now += staleAfterMs;
                assert(snapshot().stale);
                request(body);
                assert(!snapshot().stale);
                hal.net.connected = false;
                assert(request(body).status == 403);
                break;
            }
            case 2:
                assert(stops == 1 && snapshot().stale);
                hal.net.connected = true;
                break;
            case 3:
                assert(starts == 2);
                request(body);
                assert(!snapshot().stale);
                hal.net.badgeMode = true;
                assert(request(body).status == 403);
                break;
            case 4:
                assert(stops == 2);
                hal.net.badgeMode = false;
                weather::selected = weather::Mode::Weather;
                break;
            case 5:
                assert(starts == 2 && request(body).status == 403);
                weather::selected  = weather::Mode::Codex;
                hal.net.configMode = true;
                break;
            case 6:
                assert(starts == 2 && request(body).status == 403);
                hal.net.configMode = false;
                break;
            case 7:
                assert(starts == 3);
                hal.now = UINT32_MAX - 1000;
                request(body);
                hal.now = 2000;
                assert(!snapshot().stale);
                hal.now += staleAfterMs;
                assert(snapshot().stale);
                throw End{};
        }
    };
    try {
        taskEntry(nullptr);
    } catch (End&) {
    }
    assert(step == 7);
    std::cout << "PASS: pairing, partial/invalid requests, cache age/wrap, receiver lifecycle, AP exclusion\n";
}
