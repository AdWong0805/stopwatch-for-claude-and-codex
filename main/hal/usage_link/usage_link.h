/*
 * SPDX-License-Identifier: MIT
 *
 * UsageLink: optional Wi-Fi companion channel.
 *
 * Connects to the user's Wi-Fi as a station and polls a small companion
 * program running on the host PC for Claude / Codex usage-limit data.
 * Control actions for the Claude desktop app are sent back over the same
 * HTTP channel. The BLE Codex Micro transport is not touched by this
 * module; both radios share the 2.4GHz PHY through the IDF coexistence
 * layer.
 *
 * Configuration lives in NVS (namespace "usagelink") and is set over the
 * USB serial debug CLI:
 *   debug wifi <ssid> <password>
 *   debug host <ip> [port]
 */
#pragma once

#include <cstdint>

namespace usage_link {

struct Meter {
    bool valid      = false;
    uint8_t percent = 0;   // 0-100 utilization
    char reset[20]  = {};  // human-readable reset hint, e.g. "1h23m"
};

struct Snapshot {
    bool configured       = false;  // Wi-Fi credentials present in NVS
    bool wifiConnected    = false;  // station got an IP
    bool linkOk           = false;  // companion answered the last poll
    uint32_t lastUpdateMs = 0;      // GetHAL().millis() of last good poll
    Meter claudeSession;
    Meter claudeWeek;
    Meter codexSession;
    Meter codexWeek;
};

class UsageLink {
public:
    /** Reads NVS config and, when configured, starts Wi-Fi and the poll worker. */
    void begin();

    /** Thread-safe copy of the latest usage state. */
    Snapshot snapshot();

    /**
     * Queues a control action for the companion, e.g. target="claude",
     * action="enter". Non-blocking; returns false when the queue is full
     * or the link is not configured.
     */
    bool requestAction(const char* target, const char* action);

    /** Saves Wi-Fi credentials to NVS. Takes effect after reboot. */
    void setWifiConfig(const char* ssid, const char* password);

    /** Saves companion host/port to NVS. Takes effect after reboot. */
    void setHostConfig(const char* host, uint16_t port);

    /** Erases the stored configuration. */
    void clearConfig();

    bool configured() const;
};

UsageLink& GetUsageLink();

}  // namespace usage_link
