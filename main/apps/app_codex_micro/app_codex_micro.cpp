/*
 * SPDX-License-Identifier: MIT
 */
#include "app_codex_micro.h"

#include <hal/ble/codex_micro_ble.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <system_config.h>
#include <assets/assets.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <new>
#include <utility>
#include <vector>

using namespace mooncake;

AppCodexMicro::AppCodexMicro()
{
    setAppInfo().name = "Codex Remote";
    setAppInfo().icon = (void*)&icon_codex_remote;
}

AppCodexMicro::~AppCodexMicro() = default;

void AppCodexMicro::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppCodexMicro::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    ESP_LOGI("CodexRemote", "open: internal_free=%u internal_largest=%u psram_free=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    std::unique_ptr<codex_input::KeyManager> key_manager(new (std::nothrow) codex_input::KeyManager());
    std::unique_ptr<view::CodexMicroView> app_view(new (std::nothrow) view::CodexMicroView());
    if (key_manager == nullptr || app_view == nullptr) {
        mclog::tagError(getAppInfo().name, "failed to allocate app state");
        close();
        return;
    }
    _key_manager       = std::move(key_manager);
    _last_ui_update_ms = 0;
    _mic_host_active   = false;
    _send_host_active  = false;

    LvglLockGuard lock;
    _view = std::move(app_view);
    _view->init(lv_screen_active());
    ESP_LOGI("CodexRemote", "ready: internal_free=%u internal_largest=%u psram_free=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

void AppCodexMicro::onRunning()
{
    GetHAL().updateButtonStates();

    // Holding A+B together is the firmware-wide go-home gesture.
    if (GetHAL().btnA.isHolding() && GetHAL().btnB.isHolding()) {
        close();
        return;
    }

    const codex_input::KeyEvent event =
        _key_manager == nullptr ? codex_input::KeyEvent::None : _key_manager->update(false);
    const uint32_t now     = GetHAL().millis();
    const bool refresh_due = now - _last_ui_update_ms >= UiRefreshPeriodMs;
    if (event == codex_input::KeyEvent::None && !refresh_due) {
        return;
    }
    const CodexMicroState state = GetCodexMicroBle().snapshot();

    if (!state.connected) {
        _mic_host_active  = false;
        _send_host_active = false;
    }

    bool mic_view_changed = false;
    if (codex_input::hasKeyEvent(event, codex_input::KeyEvent::MicPress) && state.connected) {
        _mic_host_active = GetCodexMicroBle().sendKey(CodexMicroControl::Mic, CodexMicroKeyAction::Press);
        mic_view_changed = true;
    }
    if (codex_input::hasKeyEvent(event, codex_input::KeyEvent::SendPress) && state.connected) {
        _send_host_active = GetCodexMicroBle().sendKey(CodexMicroControl::Send, CodexMicroKeyAction::Press);
    }
    if (codex_input::hasKeyEvent(event, codex_input::KeyEvent::MicRelease)) {
        if (_mic_host_active && state.connected) {
            GetCodexMicroBle().sendKey(CodexMicroControl::Mic, CodexMicroKeyAction::Release);
        }
        _mic_host_active = false;
        mic_view_changed = true;
    }
    if (codex_input::hasKeyEvent(event, codex_input::KeyEvent::SendRelease)) {
        if (_send_host_active && state.connected) {
            GetCodexMicroBle().sendKey(CodexMicroControl::Send, CodexMicroKeyAction::Release);
        }
        _send_host_active = false;
    }
    const bool toggle_page = codex_input::hasKeyEvent(event, codex_input::KeyEvent::TogglePage) && state.connected;

    // BLE report creation/transmission stays outside the LVGL mutex so the
    // touch task can keep sampling while reports are sent.
    LvglLockGuard lock;
    if (_view == nullptr) {
        return;
    }
    _view->update(state);
    if (mic_view_changed) {
        _view->setMicActive(_mic_host_active);
    }
    if (toggle_page) {
        _view->setMicActive(false);
        _view->togglePage();
    }
    _last_ui_update_ms = now;
}

void AppCodexMicro::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    if (_mic_host_active) {
        GetCodexMicroBle().sendKey(CodexMicroControl::Mic, CodexMicroKeyAction::Release);
        _mic_host_active = false;
    }
    if (_send_host_active) {
        GetCodexMicroBle().sendKey(CodexMicroControl::Send, CodexMicroKeyAction::Release);
        _send_host_active = false;
    }
    _key_manager.reset();
    GetHAL().stopVibrate();
    std::vector<int16_t> empty_audio;
    GetHAL().audioPlay(empty_audio, true);

    LvglLockGuard lock;
    _view.reset();
}
