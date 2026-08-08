/*
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <apps/common/key_manager/key_manager.h>
#include <lvgl.h>
#include <array>
#include <cstdint>
#include <memory>
#include <mooncake.h>

/**
 * Claude / Codex usage dashboard backed by the usage_link Wi-Fi service.
 * Also exposes three quick actions relayed to the Claude desktop app by
 * the PC companion. Hold A+B to exit, like every other launcher app.
 */
class AppAiUsage : public mooncake::AppAbility {
public:
    AppAiUsage();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    static void actionEvent(lv_event_t* event);

    void buildUi();
    void refresh();

    std::unique_ptr<input::KeyManager> _key_manager;
    lv_obj_t* _root                                     = nullptr;
    lv_obj_t* _status_label                             = nullptr;
    std::array<lv_obj_t*, 2> _arcs                      = {};
    std::array<lv_obj_t*, 2> _pct_labels                = {};
    std::array<lv_obj_t*, 2> _reset_labels              = {};
    std::array<lv_obj_t*, 2> _week_bars                 = {};
    std::array<lv_obj_t*, 2> _week_labels               = {};
    uint32_t _last_refresh_ms                           = 0;
    static constexpr std::array<const char*, 3> Actions = {"focus", "enter", "esc"};
};
