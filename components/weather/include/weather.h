#pragma once
#include <string>
#include <cstdint>
#include <ctime>
namespace weather {
enum class Mode { Off = 0, Weather = 1, Codex = 2 };
struct Snapshot {
    bool valid      = false;
    bool stale      = false;
    int temperature = 0;
    int code        = 0;
    std::string city;
    // Zero means the fetch time is untrusted; weather data may still be valid.
    std::time_t fetchedAt = 0;
};
// Initialize once before use. Callbacks are required and must be thread-safe.
// Storage callbacks operate in the application's weather settings namespace.
struct Config {
    bool (*connected)();
    uint32_t (*millis)();
    int (*getInt)(const std::string&, int);
    std::string (*getString)(const std::string&, const std::string&);
    void (*setInt)(const std::string&, int);
    void (*setString)(const std::string&, const std::string&);
    void (*erase)(const std::string&);
    std::time_t (*wallTime)() = [] { return std::time(nullptr); };
};
void init(const Config& config);
Mode mode();
void setMode(Mode value);
std::string selectedCity();
int selectedCityCode();
void selectCity(int code);
Snapshot snapshot();
// Parsing is shared with host tests; malformed responses never replace cache.
bool parseResponse(const std::string& body, Snapshot& result, std::string& location);
std::string encodeLocation(const std::string& value);
const char* conditionName(int code);
}  // namespace weather
