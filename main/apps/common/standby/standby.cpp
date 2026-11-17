#include "standby.h"
#include "weather.h"
#include <codex_usage.h>
#include <hal/hal.h>
#include <assets/assets.h>
#include <driver/gpio.h>
#include <atomic>
#include <cmath>
#include <ctime>

LV_FONT_DECLARE(weather_city_24);
namespace standby {
namespace {
std::atomic<bool> ready{false}, active{false}, swallowing{false}, touchDown{false};
std::atomic<bool> overlayVisible{false};
// Keep the always-on page readable under ordinary indoor lighting.
constexpr int standbyBrightness = 25;
bool restoreDirectDisplay = false;
std::atomic<bool> stopwatch{false}, directDisplay{false}, alarm{false};
std::atomic<uint32_t> lastActivity{0};
lv_obj_t *panel = nullptr, *clockLabel = nullptr, *temperature = nullptr, *cityLabel = nullptr, *statusLabel = nullptr,
         *icon = nullptr, *condition = nullptr;
lv_obj_t *usageLabels[2] = {}, *usageBars[2] = {}, *resetLabels[2] = {};
int normalBrightness = 80, drawnCode = -1;
uint32_t lastDraw = 0;
bool buttonsDown  = false;
void input(bool down)
{
    if (!ready || !down) return;
    lastActivity = GetHAL().millis();
    if (active.exchange(false)) swallowing = true;
}
lv_obj_t* label(int y, const lv_font_t* font)
{
    auto obj = lv_label_create(panel);
    lv_obj_set_width(obj, 350);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, y);
    return obj;
}
void shape(int x, int y, int w, int h, int radius, uint32_t color)
{
    auto o = lv_obj_create(icon);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_radius(o, radius, 0);
}
void drawIcon(int code)
{
    if (drawnCode == code) return;
    drawnCode = code;
    lv_obj_clean(icon);
    if (code == 0) return;
    auto text = std::string(weather::conditionName(code));
    if (code == 113 || code == 116) {
        shape(38, 10, 44, 44, 22, 0xFFE08A);
        for (int i = 0; i < 8; ++i) {
            double a = i * 3.14159265 / 4;
            shape(58 + int(34 * std::cos(a)), 30 + int(34 * std::sin(a)), 5, 5, 2, 0xFFE08A);
        }
    }
    if (code != 113) {
        shape(16, 40, 90, 23, 12, 0xD8D8D8);
        shape(32, 22, 38, 38, 19, 0xD8D8D8);
        shape(59, 31, 32, 30, 15, 0xD8D8D8);
        if (text == "Rain" || text == "Thunderstorm" || text == "Snow / sleet")
            for (int i = 0; i < 3; ++i) shape(32 + i * 25, 70, 5, text == "Snow / sleet" ? 5 : 13, 2, 0xA8D8F0);
        if (text == "Fog") {
            shape(24, 70, 75, 3, 1, 0xD8D8D8);
            shape(34, 80, 55, 3, 1, 0xD8D8D8);
        }
        if (text == "Thunderstorm") {
            shape(58, 61, 7, 18, 0, 0xFFE08A);
            shape(51, 76, 9, 12, 0, 0xFFE08A);
        }
    }
}
void drawUsage()
{
    auto usage = codex_usage::snapshot();
    auto net = GetHAL().getWifiStatus();
    auto now = std::time(nullptr);
    const codex_usage::Window windows[] = {usage.primary, usage.secondary};
    for (int i = 0; i < 2; ++i) {
        const auto& window = windows[i];
        auto name = codex_usage::windowName(window, i == 0 ? "Primary" : "Secondary");
        if (window.valid) {
            lv_label_set_text_fmt(usageLabels[i], "%s   %d%% used", name.c_str(), window.usedPercent);
            lv_bar_set_value(usageBars[i], window.usedPercent, LV_ANIM_OFF);
        } else {
            lv_label_set_text_fmt(usageLabels[i], "%s   --", name.c_str());
            lv_bar_set_value(usageBars[i], 0, LV_ANIM_OFF);
        }
        lv_label_set_text(resetLabels[i], window.valid ? codex_usage::resetText(window, now).c_str() : "No quota data");
        lv_obj_set_style_bg_color(usageBars[i], lv_color_hex(window.usedPercent >= 90 ? 0xF3B66A : 0x83D6B5),
                                 LV_PART_INDICATOR);
    }
    if (!usage.valid) {
        lv_label_set_text(statusLabel, net.connected ? "Waiting for Codex\nSettings > Connect Codex" : "Connect device to Wi-Fi");
    } else {
        bool expired = (usage.primary.valid && usage.primary.resetsAt && now >= usage.primary.resetsAt) ||
                       (usage.secondary.valid && usage.secondary.resetsAt && now >= usage.secondary.resetsAt);
        lv_label_set_text_fmt(statusLabel, "%s\n%s", usage.stale || !net.connected || expired ? "Cached - reconnect Codex" : "Connected",
                              usage.source.c_str());
    }
}
void show()
{
    restoreDirectDisplay = directDisplay.load();
    overlayVisible       = true;
    if (restoreDirectDisplay) GetHAL().startLvglUpdate();
    normalBrightness = GetHAL().getBackLightBrightness();
    panel            = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 466, 466);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(panel, lv_color_black(), 0);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    clockLabel = label(65, &lv_font_maple_mono_medium_48);
    if (weather::mode() == weather::Mode::Codex) {
        auto title = label(123, &lv_font_montserrat_20);
        lv_label_set_text(title, "CODEX USAGE");
        lv_obj_set_style_text_color(title, lv_color_hex(0x83D6B5), 0);
        for (int i = 0; i < 2; ++i) {
            usageLabels[i] = label(166 + i * 94, &lv_font_montserrat_24);
            usageBars[i] = lv_bar_create(panel);
            lv_obj_set_size(usageBars[i], 280, 10);
            lv_obj_align(usageBars[i], LV_ALIGN_TOP_MID, 0, 201 + i * 94);
            lv_bar_set_range(usageBars[i], 0, 100);
            lv_obj_set_style_bg_color(usageBars[i], lv_color_hex(0x303030), 0);
            resetLabels[i] = label(219 + i * 94, &lv_font_montserrat_16);
        }
        statusLabel = label(362, &lv_font_montserrat_16);
        lv_obj_set_size(statusLabel, 300, 42);
        lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xABABAB), 0);
        GetHAL().setBackLightBrightness(standbyBrightness);
        lastDraw = GetHAL().millis() - 1000;
        return;
    }
    cityLabel  = label(128, &weather_city_24);
    lv_label_set_long_mode(cityLabel, LV_LABEL_LONG_DOT);
    icon = lv_obj_create(panel);
    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, 120, 92);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 170);
    temperature = label(270, &lv_font_montserrat_36);
    condition   = label(322, &lv_font_montserrat_20);
    statusLabel = label(362, &lv_font_montserrat_16);
    GetHAL().setBackLightBrightness(weather::mode() == weather::Mode::Off ? 0 : standbyBrightness);
    drawnCode = -1;
    lastDraw  = GetHAL().millis() - 1000;
}
void hide()
{
    if (!panel) return;
    if (restoreDirectDisplay && !alarm) {
        GetHAL().stopLvglUpdate();
        restoreDirectDisplay = false;
    }
    lv_obj_delete(panel);
    panel          = nullptr;
    overlayVisible = false;
    GetHAL().setBackLightBrightness(normalBrightness);
}
}  // namespace
bool isVisible()
{
    return overlayVisible || alarm;
}
void init()
{
    lastActivity = GetHAL().millis();
    ready        = true;
}
bool filterTouch(bool pressed)
{
    touchDown = pressed;
    input(pressed);
    return swallowing || active;
}
bool suppressButtons()
{
    return swallowing || active;
}
void setStopwatchRunning(bool value)
{
    stopwatch = value;
}
void setDirectDisplay(bool value)
{
    directDisplay = value;
}
void setAlarmActive(bool value)
{
    alarm = value;
    if (value) {
        active       = false;
        lastActivity = GetHAL().millis();
    }
}
void tick()
{
    if (!ready) return;
    uint32_t now = GetHAL().millis();
    buttonsDown  = !gpio_get_level(GPIO_NUM_2) || !gpio_get_level(GPIO_NUM_1);
    input(buttonsDown);
    // Touch sampling runs on another task and may have a slightly newer tick.
    // Signed subtraction avoids interpreting that race as 49 days of inactivity.
    const int32_t idle = static_cast<int32_t>(now - lastActivity.load());
    if (swallowing && !buttonsDown && !touchDown && idle > 150) swallowing = false;
    bool inhibit = stopwatch || alarm;
    if (inhibit) {
        active       = false;
        lastActivity = now;
    } else if (!swallowing && !buttonsDown && !touchDown && idle >= 60000)
        active = true;
    LvglLockGuard lock;
    if (alarm && directDisplay) {
        restoreDirectDisplay = true;
        GetHAL().startLvglUpdate();
    }
    if (!alarm && !panel && restoreDirectDisplay) {
        GetHAL().stopLvglUpdate();
        restoreDirectDisplay = false;
    }
    if (!active) {
        hide();
        return;
    }
    if (!panel) show();
    if (weather::mode() == weather::Mode::Off || uint32_t(now - lastDraw) < 1000) return;
    lastDraw = now;
    auto t   = GetHAL().getTimeHms();
    lv_label_set_text_fmt(clockLabel, "%02u:%02u", t.hour, t.minute);
    if (weather::mode() == weather::Mode::Codex) {
        drawUsage();
        return;
    }
    auto w    = weather::snapshot();
    auto city = w.valid ? w.city : weather::selectedCity();
    lv_label_set_text(cityLabel, city.empty() ? "Locating city..." : city.c_str());
    if (w.valid) {
        lv_label_set_text_fmt(temperature, "%d °C", w.temperature);
        lv_label_set_text(condition, weather::conditionName(w.code));
        drawIcon(w.code);
        if (!w.fetchedAt) {
            lv_label_set_text(statusLabel, "Cached - time unverified");
            return;
        }
        struct tm updated = {};
        localtime_r(&w.fetchedAt, &updated);
        lv_label_set_text_fmt(statusLabel, "%s %02d-%02d %02d:%02d", w.stale ? "Cached" : "Updated", updated.tm_mon + 1,
                              updated.tm_mday, updated.tm_hour, updated.tm_min);
    } else {
        lv_label_set_text(temperature, "-- °C");
        lv_label_set_text(condition, "Weather unavailable");
        lv_label_set_text(statusLabel, "Connect Wi-Fi / set city");
        drawIcon(0);
    }
}
}  // namespace standby
