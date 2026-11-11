#!/usr/bin/env python3
"""Loopback-only ESD contract fixture; no outbound traffic or relay emulation.

Success fields come from static analysis of the supplied ELF. Error text and
fixture credentials are synthetic, not golden recordings of the old server.
"""
import argparse
import hashlib
import hmac
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

ACCESS_KEY = "fixture-access"
SECRET = "fixture-secret"
DEVICE_ID = "fixture-device"
TOKEN = "fixture-token"
UNIFIED_ID = "fixture-unified"
DEVICE_PATH = "/lecam/openapiSig/hls/getDeviceByKey"
RELAY_PATH = "/lookup/openapi/getRelayIPList"
MODES = ("ok", "business-error", "http-error", "malformed", "missing-field", "empty-relay")


def signature(public_key):
    raw = SECRET + "accessKey=" + ACCESS_KEY + "publicKey=" + public_key
    return hashlib.md5(raw.encode("utf-8")).hexdigest()


class EsdMock(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, port=0, device_mode="ok", relay_mode="ok", delay=0.0, relay_port=19091):
        super().__init__(("127.0.0.1", port), Handler)
        self.modes = {DEVICE_PATH: device_mode, RELAY_PATH: relay_mode}
        self.delay = delay
        self.relay_port = relay_port
        self.counts = {DEVICE_PATH: 0, RELAY_PATH: 0}
        self.lock = threading.Lock()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass  # Never log URLs containing credentials.

    def reply(self, status, body):
        data = body if isinstance(body, bytes) else json.dumps(body).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        try:
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass  # Expected in cancellation/deadline tests.

    def do_GET(self):
        self.reply(405, {"failflag": "1", "failmsg": "fixture requires POST"})

    def do_POST(self):
        parsed = urlsplit(self.path)
        if parsed.path not in self.server.modes:
            self.reply(404, {"failflag": "1", "failmsg": "fixture route missing"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 <= length <= 65536:
                raise ValueError("body too large")
            self.rfile.read(length)
            query = parse_qs(parsed.query, keep_blank_values=True, strict_parsing=True)
            if any(len(values) != 1 for values in query.values()):
                raise ValueError("ambiguous query")
            if parsed.path == DEVICE_PATH:
                public_key = query["publicKey"][0]
                valid = bool(public_key) and query["accessKey"] == [ACCESS_KEY]
                valid = valid and hmac.compare_digest(query["signature"][0], signature(public_key))
            else:
                credentials = json.loads(query["jsonObject"][0])
                valid = isinstance(credentials, dict) and credentials.get("deviceid") == DEVICE_ID
                valid = valid and credentials.get("token") == TOKEN
            if not valid:
                raise ValueError("invalid fixture credentials")
        except (KeyError, ValueError, TypeError):
            self.reply(200, {"failflag": "1", "failmsg": "invalid fixture request"})
            return

        with self.server.lock:
            self.server.counts[parsed.path] += 1
        if self.server.delay:
            time.sleep(self.server.delay)
        mode = self.server.modes[parsed.path]
        if mode == "http-error":
            self.reply(503, {"failflag": "1", "failmsg": "fixture unavailable"})
        elif mode == "malformed":
            self.reply(200, b"{not-json")
        elif mode == "business-error":
            self.reply(200, {"failflag": "1", "failmsg": "fixture rejected"})
        elif mode == "missing-field":
            self.reply(200, {"failflag": "0"})
        elif parsed.path == DEVICE_PATH:
            self.reply(200, {"failflag": "0", "data": {
                "deviceId": DEVICE_ID, "token": TOKEN, "unifiedId": UNIFIED_ID}})
        else:
            # This port intentionally has no relay. A successful lookup is not a playable stream.
            relays = [] if mode == "empty-relay" else [{"publicip": "127.0.0.1", "relayport": self.server.relay_port}]
            self.reply(200, {"failflag": "0", "iplist": relays})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=19090)
    parser.add_argument("--device-mode", choices=MODES[:-1], default="ok")
    parser.add_argument("--relay-mode", choices=MODES, default="ok")
    parser.add_argument("--delay", type=float, default=0.0, help="Seconds per successful request")
    args = parser.parse_args()
    if args.delay < 0:
        parser.error("delay must be nonnegative")
    with EsdMock(args.port, args.device_mode, args.relay_mode, args.delay) as server:
        print(f"Synthetic ESD listening on http://127.0.0.1:{server.server_port}; no relay", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
