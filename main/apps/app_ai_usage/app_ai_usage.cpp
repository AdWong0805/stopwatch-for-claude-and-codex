/*
 * SPDX-License-Identifier: MIT
 */
#include "app_ai_usage.h"

#include <apps/common/audio/audio.h>
#include <hal/hal.h>
#include <hal/usage_link/usage_link.h>
#include <mooncake_log.h>
#include <assets/assets.h>

#include <algorithm>
#include <cstdio>

using namespace mooncake;

namespace {

constexpr uint32_t Background   = 0x0A0D0B;
constexpr uint32_t Text         = 0xF7F7F0;
constexpr uint32_t ClaudeOrange = 0xD97757;
constexpr uint32_t CodexBlue    = 0x566BF7;
constexpr uint32_t Track        = 0x2A2F2C;
constexpr uint32_t Muted        = 0x8A918D;
constexpr uint32_t ButtonBg     = 0x1D211F;
constexpr uint32_t ButtonBorder = 0x686F6B;

constexpr std::array<uint32_t, 2> AccentColors = {ClaudeOrange, CodexBlue};
constexpr std::array<const char*, 2> Names     = {"CLAUDE", "CODEX"};
constexpr std::array<int, 2> ArcX              = {58, 258};
constexpr int ArcY                             = 96;
constexpr int ArcSize                          = 150;
constexpr int TrackWidth                       = 120;

}  // namespace

AppAiUsage::AppAiUsage()
{
    setAppInfo().name = "AI Usage";
    setAppInfo().icon = (void*)&icon_ai_usage;
}

void AppAiUsage::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppAiUsage::actionEvent(lv_event_t* event)
{
    auto* self = static_cast<AppAiUsage*>(lv_event_get_user_data(event));
    if (self == nullptr) {
        return;
    }
    lv_obj_t* target    = static_cast<lv_obj_t*>(lv_event_get_target(event));
    const intptr_t slot = reinterpret_cast<intptr_t>(lv_obj_get_user_data(target));
    if (slot < 0 || slot >= static_cast<intptr_t>(Actions.size())) {
        return;
    }
    audio::play_tone(1200, 0.016f, 0.38f);
    GetHAL().vibrate(18, 58);
    usage_link::GetUsageLink().requestAction("claude", Actions[slot]);
}

void AppAiUsage::buildUi()
{
    lv_obj_t* parent = lv_screen_active();
    _root            = lv_obj_create(parent);
    lv_obj_set_size(_root, 466, 466);
    lv_obj_align(_root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(_root, lv_color_hex(Background), LV_PART_MAIN);
    lv_obj_set_style_border_width(_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(_root, 0, LV_PART_MAIN);

    lv_obj_t* title = lv_label_create(_root);
    lv_label_set_text(title, "AI USAGE");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_hex(Muted), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 34);

    _status_label = lv_label_create(_root);
    lv_label_set_text(_status_label, "");
    lv_obj_set_style_text_font(_status_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(_status_label, lv_color_hex(Muted), LV_PART_MAIN);
    lv_obj_align(_status_label, LV_ALIGN_TOP_MID, 0, 58);

    for (std::size_t index = 0; index < 2; ++index) {
        lv_obj_t* arc = lv_arc_create(_root);
        _arcs[index]  = arc;
        lv_obj_set_size(arc, ArcSize, ArcSize);
        lv_obj_set_pos(arc, ArcX[index], ArcY);
        lv_arc_set_bg_angles(arc, 135, 45);
        lv_arc_set_range(arc, 0, 100);
        lv_arc_set_value(arc, 0);
        lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(arc, 10, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 10, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, lv_color_hex(Track), LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, lv_color_hex(AccentColors[index]), LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);

        const int center_offset = ArcX[index] + ArcSize / 2 - 233;

        lv_obj_t* pct      = lv_label_create(_root);
        _pct_labels[index] = pct;
        lv_label_set_text(pct, "--");
        lv_obj_set_style_text_font(pct, &lv_font_montserrat_28, LV_PART_MAIN);
        lv_obj_set_style_text_color(pct, lv_color_hex(Text), LV_PART_MAIN);
        lv_obj_align(pct, LV_ALIGN_TOP_MID, center_offset, ArcY + 45);

        lv_obj_t* remaining      = lv_label_create(_root);
        _remaining_labels[index] = remaining;
        lv_label_set_text(remaining, "LEFT");
        lv_obj_set_style_text_font(remaining, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(remaining, lv_color_hex(Muted), LV_PART_MAIN);
        lv_obj_align(remaining, LV_ALIGN_TOP_MID, center_offset, ArcY + 79);

        lv_obj_t* reset      = lv_label_create(_root);
        _reset_labels[index] = reset;
        lv_label_set_text(reset, "");
        lv_obj_set_style_text_font(reset, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(reset, lv_color_hex(Muted), LV_PART_MAIN);
        lv_obj_align(reset, LV_ALIGN_TOP_MID, center_offset, ArcY + ArcSize + 24);

        lv_obj_t* name = lv_label_create(_root);
        lv_label_set_text(name, Names[index]);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(name, lv_color_hex(AccentColors[index]), LV_PART_MAIN);
        lv_obj_align(name, LV_ALIGN_TOP_MID, center_offset, ArcY + ArcSize + 4);

        lv_obj_t* track = lv_obj_create(_root);
        lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(track, ArcSize - 30, 8);
        lv_obj_set_pos(track, ArcX[index] + 15, ArcY + ArcSize + 48);
        lv_obj_set_style_bg_color(track, lv_color_hex(Track), LV_PART_MAIN);
        lv_obj_set_style_border_width(track, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(track, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_all(track, 0, LV_PART_MAIN);

        lv_obj_t* fill    = lv_obj_create(track);
        _week_bars[index] = fill;
        lv_obj_remove_flag(fill, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(fill, 0, 0);
        lv_obj_set_size(fill, 2, 8);
        lv_obj_set_style_bg_color(fill, lv_color_hex(AccentColors[index]), LV_PART_MAIN);
        lv_obj_set_style_border_width(fill, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(fill, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_all(fill, 0, LV_PART_MAIN);

        lv_obj_t* week      = lv_label_create(_root);
        _week_labels[index] = week;
        lv_label_set_text(week, "WEEK -- LEFT");
        lv_obj_set_style_text_font(week, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(week, lv_color_hex(Muted), LV_PART_MAIN);
        lv_obj_align(week, LV_ALIGN_TOP_MID, center_offset, ArcY + ArcSize + 61);
    }

    constexpr std::array<const char*, 3> ButtonNames = {"FOCUS", "ENTER", "ESC"};
    constexpr std::array<int, 3> ButtonX             = {63, 173, 283};
    for (std::size_t index = 0; index < ButtonNames.size(); ++index) {
        lv_obj_t* button = lv_button_create(_root);
        lv_obj_set_pos(button, ButtonX[index], 356);
        lv_obj_set_size(button, 100, 48);
        lv_obj_set_style_bg_color(button, lv_color_hex(ButtonBg), LV_PART_MAIN);
        lv_obj_set_style_border_color(button, lv_color_hex(ButtonBorder), LV_PART_MAIN);
        lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(button, 24, LV_PART_MAIN);
        lv_obj_set_style_bg_color(
            button, lv_color_hex(Track),
            static_cast<lv_style_selector_t>(LV_PART_MAIN) | static_cast<lv_style_selector_t>(LV_STATE_PRESSED));
        lv_obj_set_user_data(button, reinterpret_cast<void*>(static_cast<intptr_t>(index)));
        lv_obj_add_event_cb(button, actionEvent, LV_EVENT_CLICKED, this);

        lv_obj_t* label = lv_label_create(button);
        lv_label_set_text(label, ButtonNames[index]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(Text), LV_PART_MAIN);
        lv_obj_center(label);
    }
}

void AppAiUsage::refresh()
{
    const usage_link::Snapshot usage = usage_link::GetUsageLink().snapshot();

    const char* status = "LINKED";
    if (!usage.configured) {
        status = "SETUP: debug wifi / debug host";
    } else if (!usage.wifiConnected) {
        status = "WIFI CONNECTING ...";
    } else if (!usage.linkOk) {
        status = "COMPANION OFFLINE";
    }
    lv_label_set_text(_status_label, status);

    const std::array<const usage_link::Meter*, 2> sessions = {&usage.claudeSession, &usage.codexSession};
    const std::array<const usage_link::Meter*, 2> weeks    = {&usage.claudeWeek, &usage.codexWeek};

    for (std::size_t index = 0; index < 2; ++index) {
        const usage_link::Meter& session = *sessions[index];
        const usage_link::Meter& week    = *weeks[index];
        const usage_link::Meter& primary = session.valid ? session : week;
        const bool primary_is_week       = !session.valid && week.valid;

        const uint8_t primary_left = primary.valid ? static_cast<uint8_t>(100U - primary.percent) : 0;
        const uint8_t week_left    = week.valid ? static_cast<uint8_t>(100U - week.percent) : 0;

        lv_arc_set_value(_arcs[index], primary_left);

        char text[32] = {};
        if (primary.valid) {
            std::snprintf(text, sizeof(text), "%u%%", static_cast<unsigned>(primary_left));
        } else {
            std::snprintf(text, sizeof(text), "--");
        }
        lv_label_set_text(_pct_labels[index], text);
        const char* remaining_text = primary.valid ? (primary_is_week ? "WEEK LEFT" : "LEFT") : "NO DATA";
        lv_label_set_text(_remaining_labels[index], remaining_text);
        if (primary.valid && primary.reset[0] != '\0') {
            std::snprintf(text, sizeof(text), "RESET %s", primary.reset);
        } else {
            text[0] = '\0';
        }
        lv_label_set_text(_reset_labels[index], text);

        const int width = week.valid ? std::max(2, TrackWidth * week_left / 100) : 2;
        lv_obj_set_width(_week_bars[index], width);

        if (week.valid) {
            std::snprintf(text, sizeof(text), "WEEK %u%% LEFT", static_cast<unsigned>(week_left));
        } else {
            std::snprintf(text, sizeof(text), "WEEK -- LEFT");
        }
        lv_label_set_text(_week_labels[index], text);
    }
}

void AppAiUsage::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    _key_manager     = std::make_unique<input::KeyManager>();
    _last_refresh_ms = 0;

    LvglLockGuard lock;
    buildUi();
    refresh();
}

void AppAiUsage::onRunning()
{
    if (_key_manager && _key_manager->update() == input::KeyEvent::GoHome) {
        close();
        return;
    }
    const uint32_t now = GetHAL().millis();
    if (now - _last_refresh_ms < 500) {
        return;
    }
    _last_refresh_ms = now;
    LvglLockGuard lock;
    if (_root != nullptr) {
        refresh();
    }
}

void AppAiUsage::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    _key_manager.reset();
    LvglLockGuard lock;
    if (_root != nullptr) {
        lv_obj_delete(_root);
        _root = nullptr;
    }
    _status_label    = nullptr;
    _arcs            = {};
    _pct_labels      = {};
    _remaining_labels = {};
    _reset_labels    = {};
    _week_bars       = {};
    _week_labels     = {};
}
