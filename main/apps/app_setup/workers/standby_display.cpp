#include "workers.h"
#include <weather.h>
#include <regions.h>
#include <codex_usage.h>
#include <vector>
LV_FONT_DECLARE(weather_city_24);
namespace setup_workers {
class StandbyDisplayWorker::View {
public:
    lv_obj_t* root = nullptr;
    int page = 0, province = 0, pending = -1;
    bool done = false;
    std::string displayedCity;
    std::string displayedAddress;
    lv_obj_t* connectionLabel = nullptr;
    std::string connectionText()
    {
        auto net = GetHAL().getWifiStatus();
        return net.connected ? "Device: " + net.ipAddress : "Connect device to Wi-Fi";
    }
    View()
    {
        render();
    }
    ~View()
    {
        if (root) lv_obj_delete(root);
    }
    void button(lv_obj_t* parent, const std::string& text, int id, bool chinese = false)
    {
        auto btn = lv_button_create(parent);
        lv_obj_set_size(btn, 330, 64);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x343434), 0);
        lv_obj_set_style_radius(btn, 22, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<intptr_t>(id)));
        lv_obj_add_event_cb(
            btn,
            [](lv_event_t* e) {
                auto self = static_cast<View*>(lv_event_get_user_data(e));
                self->pending =
                    static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(e))));
            },
            LV_EVENT_CLICKED, this);
        auto label = lv_label_create(btn);
        lv_obj_set_width(label, 298);
        lv_obj_set_style_text_font(label, chinese ? &weather_city_24 : &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(label, text.c_str());
        lv_obj_center(label);
    }
    void render()
    {
        connectionLabel = nullptr;
        displayedCity = weather::selectedCity();
        if (root) lv_obj_delete(root);
        root = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(root);
        lv_obj_set_size(root, 466, 466);
        lv_obj_set_style_bg_color(root, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
        lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
        auto title = lv_label_create(root);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_26, 0);
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 54);
        lv_label_set_text(title, page == 0 ? "Standby display" : page == 1 ? "Select province" :
                                 page == 2 ? "Select city" : "Connect Codex");
        auto list = lv_obj_create(root);
        lv_obj_set_size(list, 362, 246);
        lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 104);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(list, 0, 0);
        lv_obj_set_style_pad_all(list, 8, 0);
        lv_obj_set_style_pad_row(list, 12, 0);
        lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);
        if (page == 0) {
            const auto mode = weather::mode();
            bool weather = mode == weather::Mode::Weather;
            button(list, mode == weather::Mode::Off ? "Off  " LV_SYMBOL_OK : "Off", 1);
            button(list, weather ? "Weather  " LV_SYMBOL_OK : "Weather", 2);
            button(list, mode == weather::Mode::Codex ? "Codex usage  " LV_SYMBOL_OK : "Codex usage", 4);
            if (mode == weather::Mode::Codex) button(list, "Connect Codex", 5);
            if (weather) {
                displayedCity = weather::selectedCity();
                button(list, "City settings", 3);
                if (!displayedCity.empty()) {
                    auto value = lv_label_create(list);
                    lv_obj_set_style_text_font(value, &weather_city_24, 0);
                    lv_obj_set_style_text_color(value, lv_color_white(), 0);
                    lv_obj_set_width(value, 330);
                    lv_label_set_text(value, displayedCity.c_str());
                    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
                }
            }
        } else if (page == 3) {
            displayedAddress = connectionText();
            const std::string lines[] = {"Run the sync script\non your computer", displayedAddress,
                                         "Pair code: " + codex_usage::pairingCode()};
            for (int i = 0; i < 3; ++i) {
                auto value = lv_label_create(list);
                lv_obj_set_width(value, 330);
                lv_obj_set_style_text_font(value, &lv_font_montserrat_20, 0);
                lv_obj_set_style_text_color(value, lv_color_white(), 0);
                lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
                lv_label_set_text(value, lines[i].c_str());
                if (i == 1) connectionLabel = value;
            }
        } else if (page == 1) {
            for (size_t i = 0; i < weather::provinceCount; ++i)
                button(list, weather::provinces[i].name, weather::provinces[i].code, true);
        } else {
            int count = 0;
            for (size_t i = 0; i < weather::cityCount; ++i)
                if (weather::cities[i].province == province) {
                    button(list, weather::cities[i].name, weather::cities[i].code, true);
                    ++count;
                }
            if (!count) {
                auto empty = lv_label_create(list);
                lv_obj_set_style_text_color(empty, lv_color_white(), 0);
                lv_label_set_text(empty, "City data unavailable");
            }
        }
        button(root, "Back", 0);
        auto back = lv_obj_get_child(root, -1);
        lv_obj_set_size(back, 200, 58);
        lv_obj_align(back, LV_ALIGN_TOP_MID, 0, 365);
    }
    void update()
    {
        if (page == 3 && connectionLabel) {
            auto address = connectionText();
            if (address != displayedAddress) {
                displayedAddress = address;
                lv_label_set_text(connectionLabel, address.c_str());
            }
        }
        if (pending < 0) {
            if (page == 0 && displayedCity != weather::selectedCity()) {
                displayedCity = weather::selectedCity();
                render();
            }
            return;
        }
        int action = pending;
        pending    = -1;
        if (action == 0) {
            if (page == 0) {
                done = true;
                return;
            }
            page = page == 3 ? 0 : page - 1;
        } else if (page == 0) {
            if (action == 1) weather::setMode(weather::Mode::Off);
            if (action == 2) weather::setMode(weather::Mode::Weather);
            if (action == 3) page = 1;
            if (action == 4) weather::setMode(weather::Mode::Codex);
            if (action == 5) page = 3;
        } else if (page == 1) {
            province = action;
            page     = 2;
        } else {
            weather::selectCity(action);
            page = 0;
        }
        render();
    }
};
StandbyDisplayWorker::StandbyDisplayWorker() : _view(std::make_unique<View>())
{
}
StandbyDisplayWorker::~StandbyDisplayWorker() = default;
void StandbyDisplayWorker::update()
{
    _view->update();
    _is_done = _view->done;
}
}  // namespace setup_workers
