#pragma once
#include <cstdint>
#include <ctime>
#include <string>

namespace codex_usage {
constexpr uint16_t port         = 8765;
constexpr uint32_t staleAfterMs = 180000;
struct Window {
    bool valid           = false;
    int usedPercent      = 0;
    int durationMinutes  = 0;
    std::time_t resetsAt = 0;
};
struct Snapshot {
    bool valid = false;
    bool stale = false;
    Window primary, secondary;
    std::string source;
};
// Call once at boot, before any other component API. Callbacks must remain
// valid for the application lifetime and be safe on background tasks.
struct Config {
    bool (*enabled)();
    uint32_t (*millis)();
    std::string (*loadPairingCode)();
    void (*savePairingCode)(const std::string&);
};
void init(const Config& config);
std::string pairingCode();
Snapshot snapshot();
// Strict, bounded bridge payload; a rejected update never replaces the cache.
bool parsePayload(const std::string& body, Snapshot& result);
std::string windowName(const Window& window, const char* fallback);
std::string resetText(const Window& window, std::time_t now);
}  // namespace codex_usage
