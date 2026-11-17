#include "weather.h"
#include <cJSON.h>
#include <cstdlib>
#include <cmath>
#include <memory>
namespace weather {
namespace {
const cJSON* field(const cJSON* obj, const char* name)
{
    return cJSON_GetObjectItemCaseSensitive(obj, name);
}
std::string str(const cJSON* obj)
{
    return cJSON_IsString(obj) ? obj->valuestring : "";
}
bool number(const cJSON* obj, int low, int high, int& out)
{
    double n;
    if (cJSON_IsNumber(obj))
        n = obj->valuedouble;
    else if (cJSON_IsString(obj) && obj->valuestring[0]) {
        char* end = nullptr;
        n         = std::strtod(obj->valuestring, &end);
        if (!end || *end) return false;
    } else
        return false;
    if (!std::isfinite(n) || n < low || n > high || std::floor(n) != n) return false;
    out = static_cast<int>(n);
    return true;
}
std::string firstValue(const cJSON* obj)
{
    return str(field(cJSON_GetArrayItem(obj, 0), "value"));
}
}  // namespace
std::string encodeLocation(const std::string& value)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
            c == '_' || c == '~')
            out += c;
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}
bool parseResponse(const std::string& body, Snapshot& result, std::string& location)
{
    std::unique_ptr<cJSON, decltype(&cJSON_Delete)> doc(cJSON_Parse(body.c_str()), cJSON_Delete);
    if (!doc) return false;
    const cJSON* root = doc.get();
    if (cJSON_IsObject(field(root, "data"))) root = field(root, "data");
    auto current = cJSON_GetArrayItem(field(root, "current_condition"), 0);
    Snapshot next;
    if (!number(field(current, "temp_C"), -100, 70, next.temperature) ||
        !number(field(current, "weatherCode"), 100, 999, next.code))
        return false;
    if (std::string(conditionName(next.code)) == "Unknown") return false;
    auto area           = cJSON_GetArrayItem(field(root, "nearest_area"), 0);
    std::string name    = firstValue(field(area, "areaName"));
    std::string country = firstValue(field(area, "country"));
    if (name.size() > 120 || country.size() > 80) return false;
    next.city  = name;
    next.valid = true;
    // Coordinates eliminate name ambiguity on subsequent automatically located requests.
    std::string lat = str(field(area, "latitude")), lon = str(field(area, "longitude"));
    auto coordinate = [](const std::string& s, double bound) {
        if (s.empty()) return false;
        char* end = nullptr;
        double v  = std::strtod(s.c_str(), &end);
        return end && !*end && std::isfinite(v) && std::abs(v) <= bound;
    };
    location.clear();
    if (coordinate(lat, 90) && coordinate(lon, 180)) location = lat + "," + lon;
    result = next;
    return true;
}
const char* conditionName(int c)
{
    switch (c) {
        case 113:
            return "Clear";
        case 116:
            return "Partly cloudy";
        case 119:
        case 122:
            return "Cloudy";
        case 143:
        case 248:
        case 260:
            return "Fog";
        case 176:
        case 185:
        case 263:
        case 266:
        case 281:
        case 284:
        case 293:
        case 296:
        case 299:
        case 302:
        case 305:
        case 308:
        case 311:
        case 314:
        case 353:
        case 356:
        case 359:
            return "Rain";
        case 179:
        case 182:
        case 227:
        case 230:
        case 317:
        case 320:
        case 323:
        case 326:
        case 329:
        case 332:
        case 335:
        case 338:
        case 350:
        case 362:
        case 365:
        case 368:
        case 371:
        case 374:
        case 377:
            return "Snow / sleet";
        case 200:
        case 386:
        case 389:
        case 392:
        case 395:
            return "Thunderstorm";
        default:
            return "Unknown";
    }
}
}  // namespace weather
