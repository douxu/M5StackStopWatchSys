/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <assets/assets.h>
#include <algorithm>
#include <array>
#include <deque>

using namespace setup_workers;
namespace {
// Keep all keys inside the circular display. LVGL handles case/symbol switching,
// cursor movement, backspace and insertion; OK only dismisses the keyboard.
const char* const lower_keys[]  = {"q",
                                   "w",
                                   "e",
                                   "r",
                                   "t",
                                   "y",
                                   "u",
                                   "i",
                                   "o",
                                   "p",
                                   "\n",
                                   "a",
                                   "s",
                                   "d",
                                   "f",
                                   "g",
                                   "h",
                                   "j",
                                   "k",
                                   "l",
                                   "\n",
                                   "z",
                                   "x",
                                   "c",
                                   "v",
                                   "b",
                                   "n",
                                   "m",
                                   LV_SYMBOL_BACKSPACE,
                                   "\n",
                                   "ABC",
                                   "1#",
                                   LV_SYMBOL_LEFT,
                                   " ",
                                   LV_SYMBOL_RIGHT,
                                   LV_SYMBOL_OK,
                                   ""};
const char* const upper_keys[]  = {"Q",
                                   "W",
                                   "E",
                                   "R",
                                   "T",
                                   "Y",
                                   "U",
                                   "I",
                                   "O",
                                   "P",
                                   "\n",
                                   "A",
                                   "S",
                                   "D",
                                   "F",
                                   "G",
                                   "H",
                                   "J",
                                   "K",
                                   "L",
                                   "\n",
                                   "Z",
                                   "X",
                                   "C",
                                   "V",
                                   "B",
                                   "N",
                                   "M",
                                   LV_SYMBOL_BACKSPACE,
                                   "\n",
                                   "abc",
                                   "1#",
                                   LV_SYMBOL_LEFT,
                                   " ",
                                   LV_SYMBOL_RIGHT,
                                   LV_SYMBOL_OK,
                                   ""};
const char* const symbol_keys[] = {"1",          "2",  "3",   "4",  "5",  "6", "7",
                                   "8",          "9",  "0",   "\n", "!",  "@", "#",
                                   "$",          "%",  "^",   "&",  "*",  "(", ")",
                                   "\n",         "-",  "_",   "+",  "=",  "/", "\\",
                                   ":",          ";",  "\"",  "'",  "\n", "[", "]",
                                   "{",          "}",  "<",   ">",  ",",  ".", "?",
                                   "|",          "\n", "abc", "~",  "`",  " ", LV_SYMBOL_BACKSPACE,
                                   LV_SYMBOL_OK, ""};
// Maps and control arrays must outlive the keyboard, including mode switches.
const std::array<lv_buttonmatrix_ctrl_t, 33> letter_controls = [] {
    std::array<lv_buttonmatrix_ctrl_t, 33> result{};
    result.fill(static_cast<lv_buttonmatrix_ctrl_t>(1));
    result[27] = static_cast<lv_buttonmatrix_ctrl_t>(2);  // ABC
    result[28] = static_cast<lv_buttonmatrix_ctrl_t>(2);  // symbols
    result[30] = static_cast<lv_buttonmatrix_ctrl_t>(3);  // space
    result[32] = static_cast<lv_buttonmatrix_ctrl_t>(2);  // done
    return result;
}();
const std::array<lv_buttonmatrix_ctrl_t, 46> symbol_controls = [] {
    std::array<lv_buttonmatrix_ctrl_t, 46> result{};
    result.fill(static_cast<lv_buttonmatrix_ctrl_t>(1));
    result[40] = static_cast<lv_buttonmatrix_ctrl_t>(2);
    result[43] = static_cast<lv_buttonmatrix_ctrl_t>(3);
    result[44] = static_cast<lv_buttonmatrix_ctrl_t>(2);
    result[45] = static_cast<lv_buttonmatrix_ctrl_t>(2);
    return result;
}();
void hidden(lv_obj_t* object, bool value)
{
    if (value)
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
}
void disabled(lv_obj_t* object, bool value)
{
    if (value)
        lv_obj_add_state(object, LV_STATE_DISABLED);
    else
        lv_obj_remove_state(object, LV_STATE_DISABLED);
}
lv_obj_t* panel(lv_obj_t* parent)
{
    auto* object = lv_obj_create(parent);
    lv_obj_set_size(object, 466, 466);
    lv_obj_center(object);
    lv_obj_set_style_bg_color(object, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}
lv_obj_t* label(lv_obj_t* parent, const char* text, int width, int y, const lv_font_t* font = &lv_font_montserrat_22)
{
    auto* object = lv_label_create(parent);
    lv_label_set_text(object, text);
    lv_obj_set_width(object, width);
    lv_obj_set_style_text_color(object, lv_color_white(), 0);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(object, LV_ALIGN_TOP_MID, 0, y);
    return object;
}
lv_obj_t* button(lv_obj_t* parent, const char* text, int x, int y, int width = 148, int height = 56)
{
    auto* object = lv_button_create(parent);
    lv_obj_set_size(object, width, height);
    lv_obj_align(object, LV_ALIGN_TOP_MID, x, y);
    lv_obj_set_style_radius(object, 24, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(0x4C4C4C), 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    auto* text_object = label(object, text, width, 0);
    lv_obj_center(text_object);
    return object;
}
}  // namespace

class WifiScanWorker::WifiScanView {
    enum class Action { None, Refresh, Back, Select, Cancel, Connect, CloseError };

public:
    WifiScanView()
    {
        _root   = panel(lv_screen_active());
        _search = panel(_root);
        label(_search, "Wi-Fi Networks", 320, 48, &MontserratSemiBold26);
        _scan_message = label(_search, "Scanning...", 340, 88);
        lv_obj_set_style_text_color(_scan_message, lv_color_hex(0xAAAAAA), 0);
        _list = lv_obj_create(_search);
        lv_obj_set_size(_list, 366, 222);
        lv_obj_align(_list, LV_ALIGN_TOP_MID, 0, 126);
        lv_obj_set_style_bg_color(_list, lv_color_hex(0x242424), 0);
        lv_obj_set_style_border_width(_list, 0, 0);
        lv_obj_set_style_pad_all(_list, 6, 0);
        lv_obj_set_style_radius(_list, 20, 0);
        lv_obj_set_scroll_dir(_list, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(_list, LV_SCROLLBAR_MODE_ACTIVE);
        _refresh = button(_search, "Refresh", -78, 362);
        on_click(_refresh, Action::Refresh);
        on_click(button(_search, "Back", 78, 362), Action::Back);
        create_password_page();
        create_error_dialog();
    }
    ~WifiScanView()
    {
        lv_obj_delete(_root);
    }

    bool update()
    {
        // Dispatch after LVGL event processing, so rows/dialogs are never
        // destroyed while their click callbacks are still executing.
        const auto action = _action;
        _action           = Action::None;
        switch (action) {
            case Action::Refresh:
                GetHAL().startWifiScan();
                break;
            case Action::Back:
                return true;
            case Action::Select:
                if (_selected.security == Hal::WifiSecurity::Unsupported)
                    show_error("Unsupported authentication method.");
                else
                    open_password();
                break;
            case Action::Cancel:
                GetHAL().cancelWifiConnection();
                show_list();
                break;
            case Action::Connect:
                set_keyboard(false);
                GetHAL().connectWifi(_selected, lv_textarea_get_text(_password));
                _waiting = true;
                set_busy(true);
                break;
            case Action::CloseError:
                hidden(_modal, true);
                lv_textarea_set_password_mode(_password, true);
                break;
            default:
                break;
        }
        if (_first_scan) {
            _first_scan = false;
            GetHAL().startWifiScan();
        }
        if (_waiting) {
            const auto state = GetHAL().getWifiConnectStatus();
            if (state.state == Hal::WifiConnectState::Succeeded) {
                _waiting = false;
                show_list();
            } else if (state.state == Hal::WifiConnectState::Failed) {
                _waiting = false;
                set_busy(false);
                show_error(state.message.c_str());
            }
        }
        update_list(GetHAL().getWifiScanStatus(), GetHAL().getWifiStatus());
        return false;
    }

private:
    struct ClickContext {
        WifiScanView* view;
        Action action;
    };
    // Stable storage: event callbacks retain pointers into this vector.
    std::deque<ClickContext> _clicks;
    void on_click(lv_obj_t* object, Action action)
    {
        _clicks.push_back({this, action});
        lv_obj_add_event_cb(
            object,
            [](lv_event_t* event) {
                auto* context          = static_cast<ClickContext*>(lv_event_get_user_data(event));
                context->view->_action = context->action;
            },
            LV_EVENT_CLICKED, &_clicks.back());
    }
    void create_password_page()
    {
        _entry        = panel(_root);
        _title        = label(_entry, "Connect Wi-Fi", 320, 48, &MontserratSemiBold26);
        _network_name = label(_entry, "", 320, 94);
        lv_label_set_long_mode(_network_name, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        _password = lv_textarea_create(_entry);
        lv_obj_set_size(_password, 282, 56);
        lv_obj_align(_password, LV_ALIGN_TOP_MID, -25, 158);
        lv_obj_set_style_text_font(_password, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(_password, lv_color_white(), 0);
        lv_obj_set_style_bg_color(_password, lv_color_hex(0x242424), 0);
        lv_obj_set_style_border_color(_password, lv_color_hex(0x777777), 0);
        lv_textarea_set_one_line(_password, true);
        lv_textarea_set_password_mode(_password, true);
        lv_textarea_set_password_show_time(_password, 0);
        lv_textarea_set_max_length(_password, 64);
        lv_textarea_set_placeholder_text(_password, "Password");
        lv_obj_add_event_cb(
            _password,
            [](lv_event_t* event) { static_cast<WifiScanView*>(lv_event_get_user_data(event))->set_keyboard(true); },
            LV_EVENT_CLICKED, this);
        _eye = button(_entry, LV_SYMBOL_EYE_OPEN, 145, 158, 48, 56);
        lv_obj_add_event_cb(
            _eye,
            [](lv_event_t* event) {
                auto* self = static_cast<WifiScanView*>(lv_event_get_user_data(event));
                lv_textarea_set_password_mode(self->_password, !lv_textarea_get_password_mode(self->_password));
            },
            LV_EVENT_CLICKED, this);
        _hint   = label(_entry, "Enter network password", 320, 246);
        _cancel = button(_entry, "Cancel", -78, 344);
        on_click(_cancel, Action::Cancel);
        _confirm = button(_entry, "Confirm", 78, 344);
        lv_obj_set_style_bg_color(_confirm, lv_color_hex(0x237A4C), 0);
        on_click(_confirm, Action::Connect);
        _keyboard = lv_keyboard_create(_entry);
        lv_obj_set_size(_keyboard, 330, 206);
        lv_obj_align(_keyboard, LV_ALIGN_TOP_MID, 0, 184);
        lv_obj_set_style_bg_color(_keyboard, lv_color_black(), 0);
        lv_obj_set_style_pad_all(_keyboard, 0, 0);
        lv_obj_set_style_pad_row(_keyboard, 4, 0);
        lv_obj_set_style_pad_column(_keyboard, 3, 0);
        lv_obj_set_style_radius(_keyboard, 5, LV_PART_ITEMS);
        lv_obj_set_style_bg_color(_keyboard, lv_color_hex(0x404040), LV_PART_ITEMS);
        lv_obj_set_style_text_color(_keyboard, lv_color_white(), LV_PART_ITEMS);
        lv_obj_set_style_text_font(_keyboard, &lv_font_montserrat_20, LV_PART_ITEMS);
        lv_keyboard_set_map(_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, lower_keys, letter_controls.data());
        lv_keyboard_set_map(_keyboard, LV_KEYBOARD_MODE_TEXT_UPPER, upper_keys, letter_controls.data());
        lv_keyboard_set_map(_keyboard, LV_KEYBOARD_MODE_SPECIAL, symbol_keys, symbol_controls.data());
        lv_keyboard_set_textarea(_keyboard, _password);
        lv_obj_add_event_cb(
            _keyboard,
            [](lv_event_t* event) {
                const auto code = lv_event_get_code(event);
                if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL)
                    static_cast<WifiScanView*>(lv_event_get_user_data(event))->set_keyboard(false);
            },
            LV_EVENT_ALL, this);
        hidden(_keyboard, true);
        hidden(_entry, true);
    }
    void create_error_dialog()
    {
        _modal = panel(_root);
        lv_obj_set_style_bg_opa(_modal, LV_OPA_80, 0);
        auto* box = lv_obj_create(_modal);
        lv_obj_set_size(box, 332, 250);
        lv_obj_center(box);
        lv_obj_set_style_pad_all(box, 0, 0);
        lv_obj_set_style_bg_color(box, lv_color_hex(0x242424), 0);
        lv_obj_set_style_border_width(box, 0, 0);
        lv_obj_set_style_radius(box, 24, 0);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        label(box, "Unable to connect", 300, 20);
        _error = label(box, "", 290, 65, &lv_font_montserrat_20);
        on_click(button(box, "Close", 0, 180, 150, 50), Action::CloseError);
        hidden(_modal, true);
    }
    void set_keyboard(bool visible)
    {
        if (_waiting && visible) return;
        hidden(_keyboard, !visible);
        hidden(_title, visible);
        hidden(_hint, visible);
        hidden(_cancel, visible);
        hidden(_confirm, visible);
        lv_obj_align(_network_name, LV_ALIGN_TOP_MID, 0, visible ? 64 : 94);
        lv_obj_align(_password, LV_ALIGN_TOP_MID, -25, visible ? 110 : 158);
        lv_obj_align(_eye, LV_ALIGN_TOP_MID, 145, visible ? 110 : 158);
    }
    void set_busy(bool busy)
    {
        disabled(_confirm, busy);
        disabled(_password, busy);
        disabled(_eye, busy);
        lv_label_set_text(_hint, busy ? "Connecting...\nUp to 20 seconds"
                                      : (_selected.security == Hal::WifiSecurity::Open ? "No password required"
                                                                                       : "Enter network password"));
    }
    void open_password()
    {
        GetHAL().cancelWifiConnection();
        _waiting = false;
        lv_label_set_text(_network_name, _selected.name.c_str());
        lv_textarea_set_text(_password, "");
        lv_textarea_set_password_mode(_password, true);
        lv_keyboard_set_mode(_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        set_keyboard(false);
        const bool open = _selected.security == Hal::WifiSecurity::Open;
        hidden(_password, open);
        hidden(_eye, open);
        set_busy(false);
        hidden(_search, true);
        hidden(_entry, false);
    }
    void show_list()
    {
        _waiting = false;
        lv_textarea_set_text(_password, "");
        set_keyboard(false);
        hidden(_entry, true);
        hidden(_search, false);
    }
    void show_error(const char* message)
    {
        lv_label_set_text(_error, message);
        hidden(_modal, false);
    }
    void update_list(const Hal::WifiScanStatus& scan, const Hal::WifiStatus& wifi)
    {
        lv_label_set_text(_scan_message, scan.scanning ? "Scanning..." : scan.message.c_str());
        disabled(_refresh, scan.scanning);
        auto networks               = scan.networks;
        const std::string connected = wifi.connected ? wifi.ssid : "";
        if (!connected.empty()) {
            auto found = std::find_if(networks.begin(), networks.end(), [&](const auto& n) {
                return n.name == connected && n.security == wifi.security;
            });
            if (found == networks.end()) {
                // The current AP can be absent from a scan (e.g. hidden SSID).
                networks.insert(networks.begin(), {connected, wifi.security});
            } else {
                std::rotate(networks.begin(), found, found + 1);
            }
        }
        bool same = connected == _last_connected && networks.size() == _rows.size();
        if (same)
            for (size_t i = 0; i < networks.size(); ++i) {
                if (networks[i].name != _rows[i].name || networks[i].security != _rows[i].security) {
                    same = false;
                    break;
                }
            }
        if (same) return;
        _last_connected = connected;
        lv_obj_clean(_list);
        _rows = std::move(networks);
        lv_obj_scroll_to_y(_list, 0, LV_ANIM_OFF);
        for (size_t i = 0; i < _rows.size(); ++i) {
            auto* row = button(_list, "", 0, i * 52, 342, 48);
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            auto* name = lv_obj_get_child(row, 0);
            lv_label_set_text(name, _rows[i].name.c_str());
            lv_obj_set_width(name, 288);
            lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
            lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
            lv_obj_align(name, LV_ALIGN_LEFT_MID, 8, 0);
            if (i == 0 && !connected.empty()) {
                auto* check = label(row, LV_SYMBOL_OK, 30, 0);
                lv_obj_align(check, LV_ALIGN_RIGHT_MID, -4, 0);
                lv_obj_set_style_text_color(check, lv_color_hex(0x4AD78C), 0);
            }
            lv_obj_add_event_cb(
                row,
                [](lv_event_t* event) {
                    auto* self       = static_cast<WifiScanView*>(lv_event_get_user_data(event));
                    auto* target     = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
                    const auto index = lv_obj_get_index(target);
                    if (index >= 0 && static_cast<size_t>(index) < self->_rows.size()) {
                        self->_selected = self->_rows[index];
                        self->_action   = Action::Select;
                    }
                },
                LV_EVENT_CLICKED, this);
        }
    }
    lv_obj_t *_root, *_search, *_scan_message, *_list, *_refresh;
    lv_obj_t *_entry, *_title, *_network_name, *_password, *_eye, *_hint, *_cancel, *_confirm, *_keyboard;
    lv_obj_t *_modal, *_error;
    std::vector<Hal::WifiNetwork> _rows;
    Hal::WifiNetwork _selected;
    std::string _last_connected;
    Action _action   = Action::None;
    bool _waiting    = false;
    bool _first_scan = true;
};

WifiScanWorker::WifiScanWorker() : _view(std::make_unique<WifiScanView>())
{
}
WifiScanWorker::~WifiScanWorker()
{
    GetHAL().endWifiScan();
}
void WifiScanWorker::update()
{
    _is_done = _view->update();
}
