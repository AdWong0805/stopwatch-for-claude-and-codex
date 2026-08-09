/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <mooncake_log.h>
#include <mooncake.h>
#include <apps/apps.h>
#include <hal/hal.h>
#include <lv_demos.h>
#include <apps/common/audio/audio.h>
#include <hal/ble/codex_micro_ble.h>
#include <hal/usage_link/usage_link.h>
#include <hal/usage_link/usage_console.h>
#include <freertos/task.h>

using namespace mooncake;
using namespace smooth_ui_toolkit;

extern "C" void app_main(void)
{
    // Setup logger
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

    // HAL init
    GetHAL().init();

    // Codex Micro BLE transport is a system service for the device lifetime.
    if (!GetCodexMicroBle().begin()) {
        mclog::error("Codex Micro BLE initialization failed");
    } else {
        GetCodexMicroBle().setBattery(GetHAL().getBatteryLevel(), GetHAL().isBatteryCharging());
    }
    uint32_t last_codex_battery_update = GetHAL().millis();

    // Optional Wi-Fi companion channel + its serial configuration CLI.
    usage_link::GetUsageLink().begin();
    usage_console::begin();

    // Setup ui hal
    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    // Install apps
    GetMooncake().installApp(std::make_unique<AppLauncher>());
    GetMooncake().installApp(std::make_unique<AppAlarmClock>());
    GetMooncake().installApp(std::make_unique<AppWatchFace>());
    GetMooncake().installApp(std::make_unique<AppStopWatch>());
    GetMooncake().installApp(std::make_unique<AppBadge>());
    GetMooncake().installApp(std::make_unique<AppImu>());
    GetMooncake().installApp(std::make_unique<AppFft>());
    GetMooncake().installApp(std::make_unique<AppLuckyWheel>());
    GetMooncake().installApp(std::make_unique<AppCodexMicro>());
    GetMooncake().installApp(std::make_unique<AppAiUsage>());
    GetMooncake().installApp(std::make_unique<AppSetup>());
    // GetMooncake().installApp(std::make_unique<AppTemplate>());

    // Main loop
    while (1) {
        GetHAL().feedTheDog();
        GetMooncake().update();
        const uint32_t now = GetHAL().millis();
        if (now - last_codex_battery_update >= 30000) {
            last_codex_battery_update = now;
            GetCodexMicroBle().setBattery(GetHAL().getBatteryLevel(), GetHAL().isBatteryCharging());
        }
        // Keep CPU0 available to Bluedroid, Wi-Fi, and the asynchronous HID
        // sender while the launcher or a graphics-heavy app is active.
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
