/*
 * SPDX-License-Identifier: MIT
 */
#include "stopwatch_mic_bridge.h"

#include <hal/hal.h>
#include <hal/usage_link/usage_link.h>

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/task.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr const char* Tag = "StopWatchMic";
constexpr uint16_t VoiceBridgePort = 8788;
constexpr uint16_t CaptureChunkMs  = 60;
constexpr float CaptureGain        = 12.0f;
constexpr uint8_t ProtocolMagic[8] = {'S', 'W', 'M', 'I', 'C', '0', '1', '\n'};

bool sendAll(int socket_fd, const void* data, std::size_t length)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    std::size_t sent  = 0;
    while (sent < length) {
        const int result = send(socket_fd, bytes + sent, length - sent, 0);
        if (result <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

void appendLittleEndian32(uint8_t* output, uint32_t value)
{
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8U);
    output[2] = static_cast<uint8_t>(value >> 16U);
    output[3] = static_cast<uint8_t>(value >> 24U);
}

float microphoneLevel(const std::vector<int16_t>& samples)
{
    if (samples.empty()) {
        return 0.0f;
    }
    double sum = 0.0;
    for (const int16_t sample : samples) {
        const double value = static_cast<double>(sample) / 32768.0;
        sum += value * value;
    }
    const double rms = std::sqrt(sum / static_cast<double>(samples.size()));
    return static_cast<float>(std::pow(std::min(1.0, rms * 6.0), 0.6));
}

}  // namespace

bool StopWatchMicBridge::start()
{
    bool expected = false;
    if (!_busy.compare_exchange_strong(expected, true)) {
        ESP_LOGW(Tag, "start rejected: previous session still busy state=%u",
                 static_cast<unsigned>(_state.load()));
        return false;
    }
    if (!usage_link::GetUsageLink().copyCompanionHost(_host, sizeof(_host))) {
        ESP_LOGW(Tag, "start rejected: Wi-Fi companion host is not ready");
        _busy.store(false);
        _state.store(State::Error);
        return false;
    }

    _stop_requested.store(false);
    _state.store(State::Recording);
    GetHAL().setMicrophoneMeterExternal(true);
    GetHAL().setMicrophoneLevel(0.0f);
    // The Codex Remote view leaves internal RAM fragmented enough that a 6 KiB
    // contiguous task stack is not reliably available. This worker does not
    // perform flash/NVS operations, so its stack can safely live in PSRAM.
    const BaseType_t created = xTaskCreatePinnedToCoreWithCaps(
        &StopWatchMicBridge::taskEntry, "watch_mic", 6144, this, 4, nullptr, 0,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (created != pdPASS) {
        ESP_LOGE(Tag, "task creation failed: internal_free=%u largest=%u psram_free=%u",
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
        GetHAL().setMicrophoneMeterExternal(false);
        _busy.store(false);
        _state.store(State::Error);
        return false;
    }
    ESP_LOGI(Tag, "recording started: host=%s:%u", _host, static_cast<unsigned>(VoiceBridgePort));
    return true;
}

void StopWatchMicBridge::stop()
{
    if (_busy.load()) {
        _stop_requested.store(true);
    }
}

bool StopWatchMicBridge::busy() const
{
    return _busy.load();
}

StopWatchMicBridge::State StopWatchMicBridge::state() const
{
    return _state.load();
}

void StopWatchMicBridge::taskEntry(void* context)
{
    static_cast<StopWatchMicBridge*>(context)->run();
    vTaskDeleteWithCaps(nullptr);
}

void StopWatchMicBridge::run()
{
    int socket_fd = -1;
    bool success  = false;
    do {
        char port[8] = {};
        std::snprintf(port, sizeof(port), "%u", static_cast<unsigned>(VoiceBridgePort));
        addrinfo hints = {};
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* address = nullptr;
        if (getaddrinfo(_host, port, &hints, &address) != 0 || address == nullptr) {
            ESP_LOGE(Tag, "cannot resolve VoiceBridge host %s", _host);
            break;
        }

        socket_fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket_fd < 0) {
            freeaddrinfo(address);
            break;
        }
        timeval send_timeout = {.tv_sec = 2, .tv_usec = 0};
        timeval recv_timeout = {.tv_sec = 45, .tv_usec = 0};
        setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
        setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout, sizeof(recv_timeout));
        const int connected = connect(socket_fd, address->ai_addr, address->ai_addrlen);
        freeaddrinfo(address);
        if (connected != 0) {
            ESP_LOGE(Tag, "VoiceBridge connection to %s:%u failed", _host, static_cast<unsigned>(VoiceBridgePort));
            break;
        }

        uint8_t header[16] = {};
        std::memcpy(header, ProtocolMagic, sizeof(ProtocolMagic));
        appendLittleEndian32(header + 8, static_cast<uint32_t>(GetHAL().getAudioSampleRate()));
        header[12] = 1;
        header[13] = 16;
        if (!sendAll(socket_fd, header, sizeof(header))) {
            break;
        }

        std::vector<int16_t> samples;
        std::size_t total_bytes = 0;
        while (!_stop_requested.load()) {
            GetHAL().audioRecord(samples, CaptureChunkMs, CaptureGain);
            if (samples.empty()) {
                ESP_LOGE(Tag, "microphone capture returned no samples");
                break;
            }
            GetHAL().setMicrophoneLevel(microphoneLevel(samples));
            const std::size_t bytes = samples.size() * sizeof(int16_t);
            if (!sendAll(socket_fd, samples.data(), bytes)) {
                ESP_LOGE(Tag, "audio stream interrupted after %u bytes", static_cast<unsigned>(total_bytes));
                break;
            }
            total_bytes += bytes;
        }

        if (!_stop_requested.load() || total_bytes == 0) {
            break;
        }
        _state.store(State::Processing);
        shutdown(socket_fd, SHUT_WR);
        char reply[64] = {};
        const int received = recv(socket_fd, reply, sizeof(reply) - 1, 0);
        if (received <= 0) {
            ESP_LOGE(Tag, "VoiceBridge did not acknowledge transcription");
            break;
        }
        reply[received] = '\0';
        success         = std::strncmp(reply, "OK", 2) == 0;
        ESP_LOGI(Tag, "VoiceBridge reply: %s", reply);
    } while (false);

    if (socket_fd >= 0) {
        close(socket_fd);
    }
    GetHAL().setMicrophoneLevel(0.0f);
    GetHAL().setMicrophoneMeterExternal(false);
    _state.store(success ? State::Success : State::Error);
    _busy.store(false);
}

StopWatchMicBridge& GetStopWatchMicBridge()
{
    static StopWatchMicBridge bridge;
    return bridge;
}
