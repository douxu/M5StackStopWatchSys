#include "codex_usage.h"
#include <esp_http_server.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
#include <cstdio>

namespace codex_usage {
namespace {
Config platform;
std::mutex mutex;
std::string code;
Snapshot cache;
uint32_t receivedAt = 0;
esp_err_t receive(httpd_req_t* req)
{
    if (!platform.enabled())
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Select Codex usage on the device");
    char supplied[32] = {};
    if (httpd_req_get_hdr_value_str(req, "X-Pairing-Code", supplied, sizeof(supplied)) != ESP_OK ||
        pairingCode() != supplied)
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Incorrect pairing code");
    if (!req->content_len || req->content_len > 2048)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid payload size");
    std::string body(req->content_len, '\0');
    size_t offset = 0;
    while (offset < body.size()) {
        int count = httpd_req_recv(req, &body[offset], body.size() - offset);
        if (count <= 0) return ESP_FAIL;  // Close timed-out/partial requests.
        offset += count;
    }
    Snapshot fresh;
    if (!parsePayload(body, fresh)) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid usage data");
    {
        std::lock_guard<std::mutex> lock(mutex);
        cache      = fresh;
        receivedAt = platform.millis();
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}
void task(void*)
{
    httpd_handle_t server = nullptr;
    for (;;) {
        bool enabled = platform.enabled();
        if (enabled && !server) {
            httpd_config_t config    = HTTPD_DEFAULT_CONFIG();
            config.server_port       = port;
            config.ctrl_port         = 32769;  // Separate from the Wi-Fi/badge configuration server.
            config.stack_size        = 6144;
            config.max_open_sockets  = 2;
            config.lru_purge_enable  = true;
            config.recv_wait_timeout = 2;
            config.send_wait_timeout = 2;
            if (httpd_start(&server, &config) == ESP_OK) {
                httpd_uri_t uri = {};
                uri.uri         = "/codex/usage";
                uri.method      = HTTP_POST;
                uri.handler     = receive;
                if (httpd_register_uri_handler(server, &uri) != ESP_OK) {
                    httpd_stop(server);
                    server = nullptr;
                }
            }
        } else if (!enabled && server) {
            httpd_stop(server);
            server = nullptr;
            std::lock_guard<std::mutex> lock(mutex);
            cache.stale = true;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
}  // namespace
void init(const Config& config)
{
    platform = config;
    code = platform.loadPairingCode();
    if (code.size() != 12 || code.find_first_not_of("0123456789abcdef") != std::string::npos) {
        char value[13];
        std::snprintf(value, sizeof(value), "%08lx%04lx", static_cast<unsigned long>(esp_random()),
                      static_cast<unsigned long>(esp_random() & 0xffff));
        code = value;
        platform.savePairingCode(code);
    }
    xTaskCreate(task, "codex_usage", 4096, nullptr, 1, nullptr);
}
std::string pairingCode()
{
    return code;  // Initialized before any reader starts; immutable until reboot/reset.
}
Snapshot snapshot()
{
    std::lock_guard<std::mutex> lock(mutex);
    auto result  = cache;
    result.stale = result.stale || uint32_t(platform.millis() - receivedAt) >= staleAfterMs;
    return result;
}
}  // namespace codex_usage
