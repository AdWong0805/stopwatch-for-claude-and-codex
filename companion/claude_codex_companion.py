#!/usr/bin/env python3
"""Stopwatch Micro companion: serves Claude/Codex usage data to the watch
and relays Claude control actions to the Claude desktop app.

Endpoints (HTTP, LAN only):
  GET  /usage    -> {"claude": {"session": {...}, "week": {...}}, "codex": {...}}
  POST /control  -> {"target": "claude", "action": "focus" | "enter" | "esc"}

Usage sources (best effort, cached 60 s):
  * Claude: Claude Code OAuth credentials (~/.claude/.credentials.json)
            queried against the api.anthropic.com OAuth usage endpoint.
  * Codex:  Codex CLI auth (~/.codex/auth.json) queried against the
            chatgpt.com backend usage endpoint.
  * Manual override: usage_override.json next to this script, same shape
    as the /usage response. Overrides win over live data per provider.

These endpoints are not stable public APIs; when a provider fails the
corresponding meters simply report invalid and the watch shows "--".

No third-party packages required. Windows only for /control (uses Win32
via ctypes); /usage works anywhere.
"""

import json
import os
import sys
import threading
import time
import urllib.request
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

PORT = 8787
CACHE_SECONDS = 60
OVERRIDE_FILE = Path(__file__).with_name("usage_override.json")

# --------------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------------


def _log(message: str) -> None:
    print(f"[{time.strftime('%H:%M:%S')}] {message}", flush=True)


def _reset_hint(resets_at) -> str:
    """Turns an ISO timestamp or epoch into a short 'in Xh Ym' hint."""
    try:
        if resets_at is None:
            return ""
        if isinstance(resets_at, (int, float)):
            target = datetime.fromtimestamp(float(resets_at), tz=timezone.utc)
        else:
            text = str(resets_at).replace("Z", "+00:00")
            target = datetime.fromisoformat(text)
            if target.tzinfo is None:
                target = target.replace(tzinfo=timezone.utc)
        delta = (target - datetime.now(timezone.utc)).total_seconds()
        if delta <= 0:
            return "resets now"
        hours, rest = divmod(int(delta), 3600)
        minutes = rest // 60
        if hours >= 48:
            return f"{hours // 24}d {hours % 24}h"
        if hours > 0:
            return f"{hours}h {minutes:02d}m"
        return f"{minutes}m"
    except Exception:
        return ""


def _meter(pct, resets_at=None) -> dict:
    try:
        value = max(0, min(100, round(float(pct))))
    except Exception:
        return {}
    return {"pct": value, "reset": _reset_hint(resets_at)}


def _http_json(url: str, headers: dict, timeout: int = 10):
    request = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def _first(mapping, *keys):
    for key in keys:
        if isinstance(mapping, dict) and key in mapping and mapping[key] is not None:
            return mapping[key]
    return None


# --------------------------------------------------------------------------
# Claude usage (Claude Code OAuth credentials)
# --------------------------------------------------------------------------


def fetch_claude_usage() -> dict:
    credential_paths = [
        Path.home() / ".claude" / ".credentials.json",
        Path.home() / ".config" / "claude" / ".credentials.json",
    ]
    token = None
    for path in credential_paths:
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            token = _first(_first(data, "claudeAiOauth", "oauth") or {}, "accessToken", "access_token")
            if token:
                break
        except Exception:
            continue
    if not token:
        return {}

    try:
        payload = _http_json(
            "https://api.anthropic.com/api/oauth/usage",
            {
                "Authorization": f"Bearer {token}",
                "anthropic-beta": "oauth-2025-04-20",
                "User-Agent": "stopwatch-micro-companion/1.0",
            },
        )
    except Exception as error:
        _log(f"claude usage fetch failed: {error}")
        return {}

    result = {}
    session = _first(payload, "five_hour", "session", "primary")
    week = _first(payload, "seven_day", "week", "secondary")
    for name, node in (("session", session), ("week", week)):
        if not isinstance(node, dict):
            continue
        pct = _first(node, "utilization", "used_percent", "percent")
        reset = _first(node, "resets_at", "reset_at", "resetsAt")
        meter = _meter(pct, reset)
        if meter:
            result[name] = meter
    return result


# --------------------------------------------------------------------------
# Codex usage (Codex CLI auth)
# --------------------------------------------------------------------------


def fetch_codex_usage() -> dict:
    auth_path = Path.home() / ".codex" / "auth.json"
    try:
        auth = json.loads(auth_path.read_text(encoding="utf-8"))
    except Exception:
        return {}
    tokens = _first(auth, "tokens") or {}
    token = _first(tokens, "access_token", "accessToken") or _first(auth, "access_token")
    account = _first(tokens, "account_id", "accountId") or _first(auth, "account_id")
    if not token:
        return {}

    headers = {
        "Authorization": f"Bearer {token}",
        "User-Agent": "stopwatch-micro-companion/1.0",
    }
    if account:
        headers["chatgpt-account-id"] = str(account)

    payload = None
    for url in (
        "https://chatgpt.com/backend-api/codex/usage",
        "https://chatgpt.com/backend-api/wham/usage",
    ):
        try:
            payload = _http_json(url, headers)
            break
        except Exception as error:
            _log(f"codex usage fetch failed ({url.rsplit('/', 1)[-1]}): {error}")
    if not isinstance(payload, dict):
        return {}

    rate_limits = _first(payload, "rate_limits", "rateLimits") or payload
    session = _first(rate_limits, "primary", "five_hour", "session")
    week = _first(rate_limits, "secondary", "seven_day", "week")
    result = {}
    for name, node in (("session", session), ("week", week)):
        if not isinstance(node, dict):
            continue
        pct = _first(node, "used_percent", "utilization", "percent")
        reset = _first(node, "resets_at", "reset_at", "resets_in_seconds")
        if isinstance(reset, (int, float)) and reset < 10_000_000:
            reset = time.time() + float(reset)
        meter = _meter(pct, reset)
        if meter:
            result[name] = meter
    return result


# --------------------------------------------------------------------------
# usage cache + override
# --------------------------------------------------------------------------

_cache_lock = threading.Lock()
_cache_data: dict = {}
_cache_time = 0.0


def usage_payload() -> dict:
    global _cache_data, _cache_time
    with _cache_lock:
        if time.time() - _cache_time < CACHE_SECONDS and _cache_data:
            return _cache_data
    data = {"claude": fetch_claude_usage(), "codex": fetch_codex_usage()}
    try:
        override = json.loads(OVERRIDE_FILE.read_text(encoding="utf-8"))
        for provider in ("claude", "codex"):
            if isinstance(override.get(provider), dict):
                data[provider] = override[provider]
    except FileNotFoundError:
        pass
    except Exception as error:
        _log(f"override file ignored: {error}")
    with _cache_lock:
        _cache_data = data
        _cache_time = time.time()
    return data


# --------------------------------------------------------------------------
# Claude desktop control (Windows)
# --------------------------------------------------------------------------


def _find_claude_window():
    import ctypes
    from ctypes import wintypes

    user32 = ctypes.windll.user32
    matches = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def enum_proc(hwnd, _lparam):
        if not user32.IsWindowVisible(hwnd):
            return True
        length = user32.GetWindowTextLengthW(hwnd)
        if length == 0:
            return True
        buffer = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, buffer, length + 1)
        title = buffer.value
        if title == "Claude" or title.startswith("Claude "):
            matches.append(hwnd)
        return True

    user32.EnumWindows(enum_proc, 0)
    return matches[0] if matches else None


def _press_key(virtual_key: int) -> None:
    import ctypes

    KEYEVENTF_KEYUP = 0x0002
    ctypes.windll.user32.keybd_event(virtual_key, 0, 0, 0)
    time.sleep(0.02)
    ctypes.windll.user32.keybd_event(virtual_key, 0, KEYEVENTF_KEYUP, 0)


def handle_control(target: str, action: str) -> bool:
    if target != "claude":
        _log(f"unsupported control target: {target}")
        return False
    if sys.platform != "win32":
        _log("control actions require Windows")
        return False

    import ctypes

    VK_RETURN, VK_ESCAPE = 0x0D, 0x1B
    hwnd = _find_claude_window()
    if hwnd is None:
        _log("Claude window not found")
        return False
    ctypes.windll.user32.SetForegroundWindow(hwnd)
    time.sleep(0.15)
    if action == "focus":
        return True
    if action == "enter":
        _press_key(VK_RETURN)
        return True
    if action == "esc":
        _press_key(VK_ESCAPE)
        return True
    _log(f"unknown action: {action}")
    return False


# --------------------------------------------------------------------------
# HTTP server
# --------------------------------------------------------------------------


class Handler(BaseHTTPRequestHandler):
    def _reply(self, status: int, payload: dict) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):  # noqa: N802
        if self.path.split("?")[0] == "/usage":
            self._reply(200, usage_payload())
        else:
            self._reply(404, {"error": "unknown path"})

    def do_POST(self):  # noqa: N802
        if self.path.split("?")[0] != "/control":
            self._reply(404, {"error": "unknown path"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            request = json.loads(self.rfile.read(length).decode("utf-8"))
            target = str(request.get("target", ""))
            action = str(request.get("action", ""))
        except Exception:
            self._reply(400, {"error": "bad request"})
            return
        ok = handle_control(target, action)
        _log(f"control {target}/{action} -> {'ok' if ok else 'failed'}")
        self._reply(200 if ok else 500, {"ok": ok})

    def log_message(self, *args):  # silence default request logging
        pass


def local_ip() -> str:
    import socket

    try:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.connect(("8.8.8.8", 80))
        address = probe.getsockname()[0]
        probe.close()
        return address
    except Exception:
        return "127.0.0.1"


def main() -> None:
    address = local_ip()
    _log(f"Stopwatch Micro companion on http://{address}:{PORT}")
    _log("Configure the watch over USB serial:")
    _log("  debug wifi <ssid> <password>")
    _log(f"  debug host {address} {PORT}")
    _log("Then reboot the watch. Ctrl+C stops the companion.")
    usage_payload()  # warm the cache and surface provider errors early
    server = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        _log("stopped")


if __name__ == "__main__":
    main()
