#!/usr/bin/env python3
"""Sync the locally signed-in Codex account's quota windows to M5StopWatch.

Uses only the Python standard library and the official Codex app-server protocol.
Never reads or sends account credentials, prompts, or conversation history.
"""
import argparse
import getpass
import ipaddress
import json
import math
import os
from pathlib import Path
import selectors
import shutil
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request


class SyncError(Exception):
    pass


class AppServer:
    def __init__(self, executable):
        self.process = subprocess.Popen(
            [executable, "app-server", "--listen", "stdio://"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        )
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        self.buffer = b""
        self.sequence = 0

    def send(self, message):
        self.process.stdin.write(json.dumps(message).encode() + b"\n")
        self.process.stdin.flush()

    def request(self, method, params=None, timeout=30):
        self.sequence += 1
        request_id = self.sequence
        self.send({"id": request_id, "method": method, "params": params})
        deadline = time.monotonic() + timeout
        while True:
            if b"\n" not in self.buffer:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not self.selector.select(remaining):
                    raise SyncError(f"Codex timed out during {method}")
                chunk = os.read(self.process.stdout.fileno(), 65536)
                if not chunk:
                    raise SyncError("Codex app-server exited; check your Codex installation/login")
                self.buffer += chunk
                if len(self.buffer) > 4 * 1024 * 1024:
                    raise SyncError("Codex response exceeded the size limit")
                continue
            line, self.buffer = self.buffer.split(b"\n", 1)
            message = json.loads(line)
            if "method" in message:
                if "id" in message:
                    self.send({"id": message["id"], "error": {"code": -32601, "message": "Unsupported by usage sync"}})
                continue
            if message.get("id") != request_id:
                continue
            if "error" in message:
                raise SyncError(f"Codex rejected {method}; check ChatGPT login and quota availability")
            return message["result"]

    def initialize(self):
        self.request("initialize", {
            "clientInfo": {"name": "m5stopwatch_usage", "title": "M5StopWatch Usage", "version": "1.0.0"}
        })
        self.send({"method": "initialized"})

    def close(self):
        self.selector.close()
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self.process.stdin.close()
        self.process.stdout.close()


def normalize_window(window):
    if window is None:
        return None
    if not isinstance(window, dict):
        raise SyncError("Invalid quota window returned by Codex")
    used = window.get("usedPercent")
    if isinstance(used, bool) or not isinstance(used, (int, float)) or not math.isfinite(used) or not 0 <= used <= 100:
        raise SyncError("Invalid usage percentage returned by Codex")
    result = {"usedPercent": used}
    for key, maximum in (("windowDurationMins", 525600), ("resetsAt", 4102444800)):
        value = window.get(key)
        if value is not None and (type(value) is not int or not 1 <= value <= maximum):
            raise SyncError(f"Invalid {key} returned by Codex")
        result[key] = value
    return result


def make_payload(result, name):
    # Do not accidentally show another model's bucket as the Codex quota.
    buckets = result.get("rateLimitsByLimitId")
    bucket = buckets.get("codex") if isinstance(buckets, dict) else None
    if bucket is None:
        bucket = result.get("rateLimits")
        if not isinstance(bucket, dict) or bucket.get("limitId") not in (None, "codex"):
            raise SyncError("No Codex quota available for this account")
    if not isinstance(bucket, dict):
        raise SyncError("Invalid Codex quota bucket")
    primary = normalize_window(bucket.get("primary"))
    secondary = normalize_window(bucket.get("secondary"))
    if primary is None and secondary is None:
        raise SyncError("This account has no quota windows to display (API-key accounts are unsupported)")
    source = "".join(c if 32 <= ord(c) <= 126 else "?" for c in name)[:32].strip() or "Computer"
    return {"version": 1, "source": source, "primary": primary, "secondary": secondary}


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def push(device, code, payload):
    # Direct LAN traffic only: do not forward the pairing code to an HTTP proxy/redirect.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    request = urllib.request.Request(
        f"http://{device}:8765/codex/usage", data=json.dumps(payload, allow_nan=False).encode(),
        headers={"Content-Type": "application/json", "X-Pairing-Code": code}, method="POST",
    )
    try:
        with opener.open(request, timeout=5) as response:
            if response.status != 200 or json.loads(response.read(128)).get("ok") is not True:
                raise SyncError("Device did not acknowledge the update")
    except urllib.error.HTTPError as error:
        if error.code == 403:
            raise SyncError("Check the device pairing code and select Codex usage") from None
        raise SyncError(f"Device rejected the update (HTTP {error.code})") from None


def find_codex():
    installed = shutil.which("codex")
    if installed:
        return installed
    for app in ("ChatGPT", "Codex"):
        candidate = Path(f"/Applications/{app}.app/Contents/Resources/codex-cli/CodexCLI.app/Contents/MacOS/codex")
        if candidate.is_file():
            return str(candidate)
    return "codex"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", required=True, help="Device IPv4 shown in Settings > Standby display > Connect Codex")
    parser.add_argument("--code", help="Device pairing code (prompted if omitted)")
    parser.add_argument("--codex", default=find_codex(), help="Path to the signed-in Codex CLI")
    parser.add_argument("--name", default=socket.gethostname().split(".")[0], help="Computer name shown on device")
    parser.add_argument("--interval", type=int, default=60, help="Sync interval in seconds, 15–120 (default: 60)")
    parser.add_argument("--once", action="store_true", help="Sync once and exit")
    args = parser.parse_args()
    try:
        ipaddress.IPv4Address(args.device)
    except ValueError:
        parser.error("--device must be an IPv4 address")
    if not 15 <= args.interval <= 120:
        parser.error("--interval must be between 15 and 120 seconds")
    code = (args.code or getpass.getpass("Device pairing code: ")).strip().lower()
    if len(code) != 12 or any(c not in "0123456789abcdef" for c in code):
        parser.error("Pairing code must contain the 12 hexadecimal characters shown on the device")
    server = None
    try:
        while True:
            try:
                if server is None:
                    server = AppServer(args.codex)
                    server.initialize()
                payload = make_payload(server.request("account/rateLimits/read"), args.name)
                push(args.device, code, payload)
                print(f"{time.strftime('%H:%M:%S')} Usage synced to {args.device}", flush=True)
                if args.once:
                    return 0
            except (SyncError, OSError, ValueError, KeyError, TypeError) as error:
                print(f"Sync failed: {error}", file=sys.stderr, flush=True)
                if server:
                    server.close()
                    server = None
                if args.once:
                    return 1
            time.sleep(args.interval)
    except KeyboardInterrupt:
        return 0
    finally:
        if server:
            server.close()


if __name__ == "__main__":
    sys.exit(main())
