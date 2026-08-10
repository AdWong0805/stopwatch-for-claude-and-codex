/*
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "codex_keys.h"
#include "view/view.h"
#include <cstdint>
#include <memory>
#include <mooncake.h>

/**
 * Codex Micro compatibility controller as a launcher app. The BLE transport
 * itself is a system service started in app_main; this app only owns the UI
 * and the physical-key mapping while it is open.
 *
 * In-app gestures: a short A+B chord toggles Command/Agent, holding A+B
 * together (the launcher's usual go-home gesture) exits to the launcher.
 */
class AppCodexMicro : public mooncake::AppAbility {
public:
    AppCodexMicro();
    ~AppCodexMicro() override;

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    static constexpr uint32_t UiRefreshPeriodMs = 33;

    std::unique_ptr<codex_input::KeyManager> _key_manager;
    std::unique_ptr<view::CodexMicroView> _view;
    uint32_t _last_ui_update_ms = 0;
    bool _mic_host_active       = false;
    bool _mic_watch_active      = false;
    bool _send_host_active      = false;
};
