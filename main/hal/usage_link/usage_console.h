/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal non-blocking USB-serial CLI for configuring the usage link:
 *   debug ping
 *   debug wifi <ssid> <password>
 *   debug host <ip> [port]
 *   debug usage
 *   debug usage clear
 */
#pragma once

namespace usage_console {

/** Installs the USB Serial/JTAG driver and starts the polling task. */
void begin();

}  // namespace usage_console
