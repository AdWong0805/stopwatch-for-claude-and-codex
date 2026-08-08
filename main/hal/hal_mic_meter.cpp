/*
 * SPDX-License-Identifier: MIT
 *
 * Non-invasive microphone level meter for the Codex Micro PTT overlay.
 * Uses the existing Hal::audioRecord() path in short bursts instead of
 * touching the factory audio codec code.
 */
#include <hal/hal.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

std::atomic<bool> g_enabled{false};
std::atomic<int> g_level_milli{0};
TaskHandle_t g_task = nullptr;

void micMeterTask(void*)
{
    std::vector<int16_t> samples;
    while (true) {
        if (!g_enabled.load()) {
            g_level_milli.store(0);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        samples.clear();
        GetHAL().audioRecord(samples, 40, 2.0f);
        if (!g_enabled.load()) {
            continue;
        }
        double sum = 0.0;
        for (const int16_t sample : samples) {
            const double value = static_cast<double>(sample) / 32768.0;
            sum += value * value;
        }
        const double rms = samples.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(samples.size()));
        // Perceptual-ish scaling: quiet rooms sit near 0, speech fills the bar.
        int level = static_cast<int>(std::pow(std::min(1.0, rms * 6.0), 0.6) * 1000.0);
        g_level_milli.store(level);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void ensureTask()
{
    if (g_task == nullptr) {
        xTaskCreatePinnedToCore(micMeterTask, "mic_meter", 4096, nullptr, 4, &g_task, 0);
    }
}

}  // namespace

void Hal::setMicrophoneMeterEnabled(bool enabled)
{
    ensureTask();
    const bool was = g_enabled.exchange(enabled);
    if (!was && enabled && g_task != nullptr) {
        xTaskNotifyGive(g_task);
    }
}

bool Hal::isMicrophoneMeterEnabled()
{
    return g_enabled.load();
}

float Hal::getMicrophoneLevel()
{
    return static_cast<float>(g_level_milli.load()) / 1000.0f;
}
