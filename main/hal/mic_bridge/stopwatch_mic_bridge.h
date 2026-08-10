/*
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <atomic>
#include <cstdint>

class StopWatchMicBridge {
public:
    enum class State : uint8_t {
        Idle,
        Recording,
        Processing,
        Success,
        Error,
    };

    /** Starts asynchronous microphone capture and Wi-Fi streaming. */
    bool start();

    /** Finishes the current stream; transcription continues asynchronously. */
    void stop();

    bool busy() const;
    State state() const;

private:
    static void taskEntry(void* context);
    void run();

    std::atomic_bool _busy{false};
    std::atomic_bool _stop_requested{false};
    std::atomic<State> _state{State::Idle};
    char _host[40] = {};
};

StopWatchMicBridge& GetStopWatchMicBridge();
