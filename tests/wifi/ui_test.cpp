#include <lvgl.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "hal_api.inc"
extern "C" const lv_font_t MontserratSemiBold26;
namespace setup_workers {
class WorkerBase {
public:
    virtual ~WorkerBase() = default;
    virtual void update()
    {
    }
    bool isDone()
    {
        return _is_done;
    }

protected:
    bool _is_done = false;
};
class WifiScanWorker : public WorkerBase {
public:
    WifiScanWorker();
    ~WifiScanWorker();
    void update() override;

private:
    class WifiScanView;
    std::unique_ptr<WifiScanView> _view;
};
}  // namespace setup_workers
Hal hal;
Hal& GetHAL()
{
    return hal;
}
Hal::WifiScanStatus scan_state;
Hal::WifiStatus wifi_state;
Hal::WifiConnectStatus connection_state;
Hal::WifiNetwork requested_network;
std::string requested_password;
int attempts = 0;
void Hal::startWifiScan()
{
}
void Hal::endWifiScan()
{
}
void Hal::cancelWifiConnection()
{
    connection_state = {};
}
Hal::WifiScanStatus Hal::getWifiScanStatus()
{
    return scan_state;
}
Hal::WifiStatus Hal::getWifiStatus()
{
    return wifi_state;
}
Hal::WifiConnectStatus Hal::getWifiConnectStatus()
{
    return connection_state;
}
void Hal::connectWifi(const WifiNetwork& network, const std::string& password)
{
    requested_network  = network;
    requested_password = password;
    ++attempts;
    connection_state.state = WifiConnectState::Connecting;
}
#include "wifi_scan_under_test.inc"

lv_obj_t* find_text(lv_obj_t* root, const char* text)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(root, &lv_label_class) && std::strcmp(lv_label_get_text(root), text) == 0) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto* found = find_text(lv_obj_get_child(root, i), text)) return found;
    return nullptr;
}
lv_obj_t* find_type(lv_obj_t* root, const lv_obj_class_t* type)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(root, type)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto* found = find_type(lv_obj_get_child(root, i), type)) return found;
    return nullptr;
}
void click(setup_workers::WifiScanWorker& worker, const char* text)
{
    auto* object = find_text(lv_screen_active(), text);
    assert(object);
    lv_obj_send_event(lv_obj_get_parent(object), LV_EVENT_CLICKED, nullptr);
    worker.update();
    lv_obj_update_layout(lv_screen_active());
}
void key(lv_obj_t* keyboard, const char* text)
{
    for (uint32_t i = 0;; ++i) {
        const char* value = lv_buttonmatrix_get_button_text(keyboard, i);
        assert(value);
        if (std::strcmp(value, text) == 0) {
            lv_buttonmatrix_set_selected_button(keyboard, i);
            lv_obj_send_event(keyboard, LV_EVENT_VALUE_CHANGED, nullptr);
            return;
        }
    }
}
void snapshot(const char* path)
{
    lv_obj_update_layout(lv_screen_active());
    auto* image = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB888);
    assert(image);
    FILE* file = std::fopen(path, "wb");
    assert(file);
    std::fprintf(file, "P6\n466 466\n255\n");
    for (int y = 0; y < 466; ++y)
        for (int x = 0; x < 466; ++x) {
            const auto* pixel = image->data + y * image->header.stride + x * 3;
            const int dx = x - 233, dy = y - 233;
            const unsigned char rgb[]     = {pixel[2], pixel[1], pixel[0]};
            const unsigned char outside[] = {38, 38, 38};
            std::fwrite(dx * dx + dy * dy > 233 * 233 ? outside : rgb, 1, 3, file);
        }
    std::fclose(file);
    lv_draw_buf_destroy(image);
}
int main()
{
    lv_init();
    auto* display = lv_display_create(466, 466);
    std::vector<uint8_t> buffer(466 * 466 * 4);
    lv_display_set_buffers(display, buffer.data(), nullptr, buffer.size(), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
    scan_state.networks  = {{"Office-Long-Network-Name", Hal::WifiSecurity::Personal},
                            {"Guest", Hal::WifiSecurity::Open},
                            {"Corporate", Hal::WifiSecurity::Unsupported}};
    scan_state.message   = "Available networks";
    wifi_state.connected = true;
    wifi_state.ssid      = "Office-Long-Network-Name";
    wifi_state.security  = Hal::WifiSecurity::Personal;
    {
        setup_workers::WifiScanWorker worker;
        worker.update();
        assert(find_text(lv_screen_active(), LV_SYMBOL_OK));
        snapshot("list.ppm");
        click(worker, "Office-Long-Network-Name");
        auto* password = find_type(lv_screen_active(), &lv_textarea_class);
        assert(password);
        assert(std::strlen(lv_textarea_get_text(password)) == 0 && lv_textarea_get_password_mode(password));
        snapshot("password.ppm");
        lv_obj_send_event(password, LV_EVENT_CLICKED, nullptr);
        auto* keyboard = find_type(lv_screen_active(), &lv_keyboard_class);
        assert(keyboard);
        key(keyboard, "a");
        key(keyboard, "ABC");
        key(keyboard, "B");
        key(keyboard, "1#");
        key(keyboard, "1");
        key(keyboard, "^");
        key(keyboard, "~");
        key(keyboard, "`");
        key(keyboard, "|");
        key(keyboard, " ");
        assert(std::strcmp(lv_textarea_get_text(password), "aB1^~`| ") == 0);
        snapshot("symbols.ppm");
        key(keyboard, "abc");
        snapshot("keyboard.ppm");
        key(keyboard, LV_SYMBOL_OK);
        assert(!find_type(lv_screen_active(), &lv_keyboard_class) && attempts == 0);
        click(worker, LV_SYMBOL_EYE_OPEN);
        assert(!lv_textarea_get_password_mode(password));
        click(worker, "Confirm");
        assert(attempts == 1 && requested_password == "aB1^~`| ");
        assert(lv_obj_has_state(lv_obj_get_parent(find_text(lv_screen_active(), "Confirm")), LV_STATE_DISABLED));
        snapshot("connecting.ppm");
        connection_state = {Hal::WifiConnectState::Failed, "Authentication failed. Check password."};
        worker.update();
        snapshot("error.ppm");
        click(worker, "Close");
        assert(lv_textarea_get_password_mode(password) && std::strcmp(lv_textarea_get_text(password), "aB1^~`| ") == 0);
        click(worker, "Cancel");
        click(worker, "Guest");
        assert(!find_type(lv_screen_active(), &lv_textarea_class));
        snapshot("open.ppm");
        click(worker, "Confirm");
        assert(requested_password.empty());
        connection_state    = {Hal::WifiConnectState::Succeeded, "Connected"};
        wifi_state.ssid     = "Guest";
        wifi_state.security = Hal::WifiSecurity::Open;
        worker.update();
        snapshot("success.ppm");
        auto* check = find_text(lv_screen_active(), LV_SYMBOL_OK);
        assert(check);
        assert(lv_obj_get_index(lv_obj_get_parent(check)) == 0);
        click(worker, "Corporate");
        assert(find_text(lv_screen_active(), "Unsupported authentication method."));
        click(worker, "Close");
        wifi_state.connected = false;
        worker.update();
        assert(!find_text(lv_screen_active(), LV_SYMBOL_OK));
    }
    // Reopening exercises destruction of rows, textarea and keyboard bindings.
    {
        setup_workers::WifiScanWorker worker;
        worker.update();
    }
    std::puts(
        "PASS real LVGL: row clicks, password, keyboard, symbols, confirm, failure, cancel, open Wi-Fi, checkmark and "
        "teardown");
    lv_display_delete(display);
    lv_deinit();
}
