#!/usr/bin/env python3
"""One-shot Wi-Fi/host configuration for the Stopwatch Micro usage link.

Example:
  py -3 setup_watch.py --port COM3 --ssid MyWifi --password MyPass --host 192.168.1.20

Requires: pip install pyserial
The watch must be running (normal mode, not download mode) and connected
over USB. The device reboots automatically after saving.
"""

import argparse
import sys
import time

try:
    import serial  # type: ignore
except ImportError:
    print("pyserial missing: run 'py -3 -m pip install pyserial' first")
    sys.exit(1)


def send(port: serial.Serial, line: str) -> str:
    port.reset_input_buffer()
    port.write((line + "\r\n").encode("ascii"))
    port.flush()
    deadline = time.time() + 3.0
    output = []
    while time.time() < deadline:
        chunk = port.read(256).decode("ascii", errors="replace")
        if chunk:
            output.append(chunk)
            if "DBG RESULT" in "".join(output):
                break
    text = "".join(output).strip()
    print(f"> {line}\n{text}\n")
    return text


def main() -> None:
    parser = argparse.ArgumentParser(description="Configure Stopwatch Micro usage link")
    parser.add_argument("--port", required=True, help="serial port, e.g. COM3")
    parser.add_argument("--ssid", required=True, help="2.4GHz Wi-Fi SSID (no spaces)")
    parser.add_argument("--password", default="", help="Wi-Fi password (no spaces)")
    parser.add_argument("--host", required=True, help="companion PC LAN IP")
    parser.add_argument("--http-port", default="8787", help="companion port (default 8787)")
    args = parser.parse_args()

    if " " in args.ssid or " " in args.password:
        print("The debug CLI is space-delimited: SSID/password must not contain spaces.")
        sys.exit(1)

    with serial.Serial(args.port, 115200, timeout=0.2) as port:
        time.sleep(1.5)  # opening the JTAG serial port may reset the board
        send(port, "debug ping")
        ok_wifi = "PASS" in send(port, f"debug wifi {args.ssid} {args.password}")
        ok_host = "PASS" in send(port, f"debug host {args.host} {args.http_port}")
        if not (ok_wifi and ok_host):
            print("Configuration failed; check the output above.")
            sys.exit(1)
    print("Saved. Power-cycle the watch (or press the power button once) to apply.")
    print("Check status later with: debug usage")


if __name__ == "__main__":
    main()
