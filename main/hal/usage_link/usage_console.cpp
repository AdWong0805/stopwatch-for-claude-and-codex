/*
 * SPDX-License-Identifier: MIT
 */
#include "usage_console.h"
#include "usage_link.h"

#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace usage_console {

namespace {

constexpr std::size_t LineCapacity = 160;

void reply(const char* command, const char* status, const char* details)
{
    if (details != nullptr && details[0] != '\0') {
        std::printf("DBG RESULT command=%s status=%s %s\r\n", command, status, details);
    } else {
        std::printf("DBG RESULT command=%s status=%s\r\n", command, status);
    }
    std::fflush(stdout);
}

void handleLine(char* line)
{
    char* save = nullptr;
    char* root = ::strtok_r(line, " \t", &save);
    if (root == nullptr) {
        return;
    }
    if (std::strcmp(root, "debug") != 0 && std::strcmp(root, "dbg") != 0) {
        return;  // Not ours; stay quiet on the shared console.
    }
    char* command = ::strtok_r(nullptr, " \t", &save);
    if (command == nullptr) {
        reply("help", "PASS", "commands=ping,wifi_[ssid]_[pass],host_[ip]_[port],usage,usage_clear");
        return;
    }
    if (std::strcmp(command, "ping") == 0) {
        reply("ping", "PASS", "reply=pong");
        return;
    }
    if (std::strcmp(command, "wifi") == 0) {
        char* ssid     = ::strtok_r(nullptr, " \t", &save);
        char* password = ::strtok_r(nullptr, " \t", &save);
        if (ssid == nullptr) {
            reply("wifi", "FAIL", "reason=usage_debug_wifi_ssid_password");
            return;
        }
        usage_link::GetUsageLink().setWifiConfig(ssid, password == nullptr ? "" : password);
        reply("wifi", "PASS", "action=saved note=reboot_to_apply");
        return;
    }
    if (std::strcmp(command, "host") == 0) {
        char* host = ::strtok_r(nullptr, " \t", &save);
        char* port = ::strtok_r(nullptr, " \t", &save);
        if (host == nullptr) {
            reply("host", "FAIL", "reason=usage_debug_host_ip_port");
            return;
        }
        long parsed = port == nullptr ? 8787 : std::strtol(port, nullptr, 10);
        if (parsed <= 0 || parsed > 65535) {
            parsed = 8787;
        }
        usage_link::GetUsageLink().setHostConfig(host, static_cast<uint16_t>(parsed));
        reply("host", "PASS", "action=saved note=reboot_to_apply");
        return;
    }
    if (std::strcmp(command, "usage") == 0) {
        char* sub = ::strtok_r(nullptr, " \t", &save);
        if (sub != nullptr && std::strcmp(sub, "clear") == 0) {
            usage_link::GetUsageLink().clearConfig();
            reply("usage", "PASS", "action=config_cleared note=reboot_to_apply");
            return;
        }
        const usage_link::Snapshot usage = usage_link::GetUsageLink().snapshot();
        char details[224]                = {};
        std::snprintf(details, sizeof(details),
                      "configured=%d wifi=%d link=%d claude_session=%d claude_week=%d codex_session=%d codex_week=%d",
                      usage.configured ? 1 : 0, usage.wifiConnected ? 1 : 0, usage.linkOk ? 1 : 0,
                      usage.claudeSession.valid ? usage.claudeSession.percent : -1,
                      usage.claudeWeek.valid ? usage.claudeWeek.percent : -1,
                      usage.codexSession.valid ? usage.codexSession.percent : -1,
                      usage.codexWeek.valid ? usage.codexWeek.percent : -1);
        reply("usage", "PASS", details);
        return;
    }
    reply(command, "FAIL", "reason=unknown_command");
}

void consoleTask(void*)
{
    static char line[LineCapacity];
    std::size_t length = 0;
    char buffer[64];
    while (true) {
        const ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (count <= 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        for (ssize_t index = 0; index < count; ++index) {
            const char value = buffer[index];
            if (value == '\r' || value == '\n') {
                if (length > 0) {
                    line[length] = '\0';
                    handleLine(line);
                    length = 0;
                }
                continue;
            }
            if (value >= 0x20 && value <= 0x7E && length + 1 < LineCapacity) {
                line[length++] = value;
            }
        }
    }
}

}  // namespace

void begin()
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = {
            .tx_buffer_size = 2048,
            .rx_buffer_size = 2048,
        };
        if (usb_serial_jtag_driver_install(&config) != ESP_OK) {
            return;
        }
    }
    usb_serial_jtag_vfs_use_driver();
    const int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }
    xTaskCreatePinnedToCore(consoleTask, "usage_console", 4096, nullptr, 2, nullptr, 0);
    std::printf("DBG READY transport=usb-serial-jtag scope=usage-config\r\n");
    std::fflush(stdout);
#endif
}

}  // namespace usage_console
