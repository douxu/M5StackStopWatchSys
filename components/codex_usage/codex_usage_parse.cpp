#include "codex_usage.h"
#include <cJSON.h>
#include <cmath>
#include <memory>

namespace codex_usage {
namespace {
bool number(const cJSON* value, double min, double max)
{
    return cJSON_IsNumber(value) && std::isfinite(value->valuedouble) && value->valuedouble >= min &&
           value->valuedouble <= max;
}
bool window(const cJSON* value, Window& out)
{
    if (!value || cJSON_IsNull(value)) return true;
    if (!cJSON_IsObject(value)) return false;
    auto used     = cJSON_GetObjectItemCaseSensitive(value, "usedPercent");
    auto duration = cJSON_GetObjectItemCaseSensitive(value, "windowDurationMins");
    auto reset    = cJSON_GetObjectItemCaseSensitive(value, "resetsAt");
    if (!number(used, 0, 100)) return false;
    out.valid       = true;
    out.usedPercent = static_cast<int>(std::round(used->valuedouble));
    if (duration && !cJSON_IsNull(duration)) {
        if (!number(duration, 1, 525600) || std::floor(duration->valuedouble) != duration->valuedouble) return false;
        out.durationMinutes = static_cast<int>(duration->valuedouble);
    }
    if (reset && !cJSON_IsNull(reset)) {
        if (!number(reset, 1, 4102444800.0) || std::floor(reset->valuedouble) != reset->valuedouble) return false;
        out.resetsAt = static_cast<std::time_t>(reset->valuedouble);
    }
    return true;
}
}  // namespace
bool parsePayload(const std::string& body, Snapshot& result)
{
    if (body.empty() || body.size() > 2048 || body.find('\0') != std::string::npos) return false;
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(cJSON_ParseWithOpts(body.c_str(), nullptr, true),
                                                         cJSON_Delete);
    if (!cJSON_IsObject(root.get())) return false;
    auto version = cJSON_GetObjectItemCaseSensitive(root.get(), "version");
    if (!number(version, 1, 1)) return false;
    Snapshot fresh;
    if (!window(cJSON_GetObjectItemCaseSensitive(root.get(), "primary"), fresh.primary) ||
        !window(cJSON_GetObjectItemCaseSensitive(root.get(), "secondary"), fresh.secondary))
        return false;
    if (!fresh.primary.valid && !fresh.secondary.valid) return false;
    auto source = cJSON_GetObjectItemCaseSensitive(root.get(), "source");
    if (!cJSON_IsString(source) || !source->valuestring) return false;
    fresh.source = source->valuestring;
    if (fresh.source.empty() || fresh.source.size() > 32) return false;
    for (unsigned char c : fresh.source)
        if (c < 32 || c > 126) return false;
    fresh.valid = true;
    result      = fresh;
    return true;
}
std::string windowName(const Window& value, const char* fallback)
{
    int minutes = value.durationMinutes;
    if (!minutes) return fallback;
    if (minutes % 1440 == 0) return std::to_string(minutes / 1440) + "d";
    if (minutes % 60 == 0) return std::to_string(minutes / 60) + "h";
    return std::to_string(minutes) + "m";
}
std::string resetText(const Window& value, std::time_t now)
{
    if (!value.valid || !value.resetsAt) return "Reset time unavailable";
    if (now < 1700000000) return "Waiting for clock sync";
    if (value.resetsAt <= now) return "Awaiting quota refresh";
    auto minutes = (value.resetsAt - now + 59) / 60;
    if (minutes >= 1440)
        return "Resets in " + std::to_string(minutes / 1440) + "d " + std::to_string(minutes / 60 % 24) + "h";
    if (minutes >= 60) return "Resets in " + std::to_string(minutes / 60) + "h " + std::to_string(minutes % 60) + "m";
    return "Resets in " + std::to_string(minutes) + "m";
}
}  // namespace codex_usage
