#include <lvgl.h>
#include "weather.h"
#include "regions.h"
#include "standby.h"
#include <codex_usage.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
struct Hal {
    uint32_t now   = 0;
    int brightness = 80;
    struct Net {
        bool connected = true;
        std::string ipAddress = "192.168.1.42";
    } net;
    Net getWifiStatus() { return net; }
    uint32_t millis()
    {
        return now;
    }
    int getBackLightBrightness()
    {
        return brightness;
    }
    void setBackLightBrightness(int n)
    {
        brightness = n;
    }
    void startLvglUpdate()
    {
        standby::setDirectDisplay(false);
    }
    void stopLvglUpdate()
    {
        standby::setDirectDisplay(true);
    }
    struct Time {
        unsigned hour = 16, minute = 25;
    };
    Time getTimeHms()
    {
        return {};
    }
} hal;
Hal& GetHAL()
{
    return hal;
}
struct LvglLockGuard {};
constexpr int GPIO_NUM_1 = 1, GPIO_NUM_2 = 2;
bool keyDown = false;
int gpio_get_level(int)
{
    return !keyDown;
}
namespace weather {
Mode testMode = Mode::Weather;
Snapshot testWeather;
int testCity = 0;
std::string testName;
Mode mode()
{
    return testMode;
}
void setMode(Mode m)
{
    testMode = m;
}
Snapshot snapshot()
{
    return testWeather;
}
std::string selectedCity()
{
    return testName;
}
int selectedCityCode()
{
    return testCity;
}
void selectCity(int c)
{
    testCity = c;
    for (size_t i = 0; i < cityCount; ++i)
        if (cities[i].code == c) testName = cities[i].name;
}
}  // namespace weather
namespace codex_usage {
Snapshot testUsage;
Snapshot snapshot() { return testUsage; }
std::string pairingCode() { return "012345abcdef"; }
}  // namespace codex_usage
namespace setup_workers {
class WorkerBase {
protected:
    bool _is_done = false;

public:
    virtual ~WorkerBase() = default;
    virtual void update() {};
    bool isDone()
    {
        return _is_done;
    }
};
class StandbyDisplayWorker : public WorkerBase {
public:
    StandbyDisplayWorker();
    ~StandbyDisplayWorker();
    void update() override;

private:
    class View;
    std::unique_ptr<View> _view;
};
}  // namespace setup_workers
LV_FONT_DECLARE(lv_font_maple_mono_medium_48);
#include "standby_under_test.inc"
#include "view_under_test.inc"
lv_obj_t* findText(lv_obj_t* root, const char* text)
{
    if (lv_obj_check_type(root, &lv_label_class) && !std::strcmp(lv_label_get_text(root), text)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto o = findText(lv_obj_get_child(root, i), text)) return o;
    return nullptr;
}
void click(setup_workers::StandbyDisplayWorker& worker, const char* text)
{
    auto label = findText(lv_screen_active(), text);
    assert(label);
    lv_obj_send_event(lv_obj_get_parent(label), LV_EVENT_CLICKED, nullptr);
    worker.update();
}
void capture(const char* name, lv_obj_t* root)
{
    lv_obj_update_layout(root);
    auto image = lv_snapshot_take(root, LV_COLOR_FORMAT_RGB888);
    assert(image);
    auto file = std::fopen(name, "wb");
    assert(file);
    std::fprintf(file, "P6\n466 466\n255\n");
    for (int y = 0; y < 466; ++y)
        for (int x = 0; x < 466; ++x) {
            auto pixel          = image->data + y * image->header.stride + x * 3;
            unsigned char rgb[] = {pixel[2], pixel[1], pixel[0]}, outside[] = {30, 30, 30};
            std::fwrite((x - 233) * (x - 233) + (y - 233) * (y - 233) > 233 * 233 ? outside : rgb, 1, 3, file);
        }
    std::fclose(file);
    lv_draw_buf_destroy(image);
}
int main()
{
    lv_init();
    auto display = lv_display_create(466, 466);
    std::vector<uint8_t> buffer(466 * 466 * 4);
    lv_display_set_buffers(display, buffer.data(), nullptr, buffer.size(), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
    {
        setup_workers::StandbyDisplayWorker worker;
        capture("settings.ppm", lv_screen_active());
        click(worker, "City settings");
        capture("provinces.ppm", lv_screen_active());
        click(worker, "北京市");
        capture("municipality.ppm", lv_screen_active());
        click(worker, "北京市");
        assert(weather::testCity == 110000 && weather::testName == "北京市");
        click(worker, "City settings");
        click(worker, "浙江省");
        capture("cities.ppm", lv_screen_active());
        click(worker, "杭州市");
        assert(weather::testCity == 330100);
        click(worker, "Off");
        assert(weather::mode() == weather::Mode::Off);
        assert(!findText(lv_screen_active(), "City settings"));
        click(worker, "Codex usage");
        assert(weather::mode() == weather::Mode::Codex);
        assert(findText(lv_screen_active(), "Codex usage  " LV_SYMBOL_OK));
        assert(findText(lv_screen_active(), "Off"));
        assert(!findText(lv_screen_active(), "City settings"));
        click(worker, "Connect Codex");
        assert(findText(lv_screen_active(), "Pair code: 012345abcdef"));
        capture("codex-pairing.ppm", lv_screen_active());
        hal.net.connected = false;
        worker.update();
        assert(findText(lv_screen_active(), "Connect device to Wi-Fi"));
        hal.net.connected = true;
        click(worker, "Back");
        click(worker, "Weather");
        assert(weather::mode() == weather::Mode::Weather);
        click(worker, "Back");
        assert(worker.isDone());
    }
    standby::init();
    hal.now = 59999;
    standby::tick();
    assert(hal.brightness == 80);
    hal.now = 60000;
    standby::tick();
    assert(hal.brightness == 25);
    capture("unavailable.ppm", lv_layer_top());
    weather::testWeather = {true, false, 16, 116, "北京市", 1792656000};
    hal.now += 1000;
    standby::tick();
    capture("weather.ppm", lv_layer_top());
    weather::testWeather.stale = true;
    hal.now += 1000;
    standby::tick();
    capture("cached.ppm", lv_layer_top());
    weather::testWeather.fetchedAt = 0;
    hal.now += 1000;
    standby::tick();
    assert(findText(lv_layer_top(), "Cached - time unverified"));
    assert(standby::filterTouch(true));
    standby::tick();
    assert(hal.brightness == 80);
    assert(standby::filterTouch(true));
    hal.now += 1000;
    standby::tick();
    assert(standby::suppressButtons());
    assert(standby::filterTouch(false));
    hal.now += 151;
    standby::tick();
    assert(!standby::suppressButtons());
    assert(!standby::filterTouch(true));
    standby::filterTouch(false);
    hal.now += 60000;
    standby::tick();
    assert(hal.brightness == 25);
    keyDown = true;
    standby::tick();
    assert(hal.brightness == 80 && standby::suppressButtons());
    hal.now += 2000;
    standby::tick();
    assert(standby::suppressButtons());
    keyDown = false;
    standby::tick();
    hal.now += 151;
    standby::tick();
    assert(!standby::suppressButtons());
    standby::setStopwatchRunning(true);
    hal.now += 60000;
    standby::tick();
    assert(hal.brightness == 80);
    standby::setStopwatchRunning(false);
    hal.now += 59999;
    standby::tick();
    assert(hal.brightness == 80);
    hal.now += 1;
    standby::tick();
    assert(hal.brightness == 25);
    standby::setAlarmActive(true);
    standby::tick();
    assert(hal.brightness == 80);
    standby::setAlarmActive(false);
    weather::setMode(weather::Mode::Off);
    hal.now += 60000;
    standby::tick();
    assert(hal.brightness == 0);
    keyDown = true;
    standby::tick();
    assert(hal.brightness == 80);
    keyDown = false;
    hal.now += 151;
    standby::tick();
    standby::setDirectDisplay(true);
    hal.now += 60000;
    standby::tick();
    assert(hal.brightness == 0 && standby::isVisible());
    keyDown = true;
    standby::tick();
    assert(hal.brightness == 80 && !standby::isVisible());
    keyDown = false;
    hal.now += 151;
    standby::tick();
    standby::setDirectDisplay(false);
    standby::setDirectDisplay(true);
    standby::setAlarmActive(true);
    standby::tick();
    assert(standby::isVisible() && !standby::directDisplay);
    standby::setAlarmActive(false);
    standby::tick();
    assert(!standby::isVisible() && standby::directDisplay);
    standby::setDirectDisplay(false);
    // A touch sample one millisecond newer than the tick must not trigger idle.
    hal.now = 100000;
    standby::filterTouch(true);
    standby::filterTouch(false);
    hal.now = 99999;
    standby::tick();
    assert(hal.brightness == 80);
    // Unsigned elapsed time remains correct across the millisecond counter wrap.
    hal.now = UINT32_MAX - 1000;
    standby::filterTouch(true);
    standby::filterTouch(false);
    standby::tick();
    hal.now = 58999;
    standby::tick();
    assert(hal.brightness == 0);
    standby::setAlarmActive(true);
    standby::tick();
    standby::setAlarmActive(false);
    weather::setMode(weather::Mode::Codex);
    hal.now += 60000;
    standby::tick();
    assert(hal.brightness == 25);
    assert(findText(lv_layer_top(), "Waiting for Codex\nSettings > Connect Codex"));
    capture("codex-waiting.ppm", lv_layer_top());
    codex_usage::testUsage = {true, false, {true, 37, 300, std::time(nullptr) + 7200},
                             {true, 91, 10080, std::time(nullptr) + 172800}, "My Mac"};
    hal.now += 1000;
    standby::tick();
    assert(findText(lv_layer_top(), "5h   37% used"));
    assert(findText(lv_layer_top(), "7d   91% used"));
    assert(findText(lv_layer_top(), "Connected\nMy Mac"));
    capture("codex-usage.ppm", lv_layer_top());
    codex_usage::testUsage.stale = true;
    hal.now += 1000;
    standby::tick();
    assert(findText(lv_layer_top(), "Cached - reconnect Codex\nMy Mac"));
    capture("codex-cached.ppm", lv_layer_top());
    codex_usage::testUsage.stale = false;
    hal.net.connected = false;
    hal.now += 1000;
    standby::tick();
    assert(findText(lv_layer_top(), "Cached - reconnect Codex\nMy Mac"));
    assert(standby::filterTouch(true));
    standby::tick();
    assert(hal.brightness == 80 && !standby::isVisible());
    standby::filterTouch(false);
    lv_display_delete(display);
    lv_deinit();
    std::puts(
        "PASS: two-level cities, mode selection, idle timeout, touch/button wake suppression, stopwatch, alarm, off, "
        "timer wrap");
}
