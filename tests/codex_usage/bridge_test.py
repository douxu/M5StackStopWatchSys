import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("sync", root / "scripts/sync_codex_usage.py")
sync = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sync)

WINDOW = {"usedPercent": 37, "windowDurationMins": 300, "resetsAt": 1794000000}
BUCKET = {"limitId": "codex", "primary": WINDOW, "secondary": None}


class BridgeTest(unittest.TestCase):
    def test_multi_bucket_precedence_and_whitelist(self):
        response = {"rateLimits": {"limitId": "other"}, "rateLimitsByLimitId": {"codex": BUCKET}, "secret": "never-forward"}
        payload = sync.make_payload(response, "Mac\n电脑")
        self.assertEqual(payload["source"], "Mac???")
        self.assertEqual(payload["primary"], WINDOW)
        self.assertIsNone(payload["secondary"])
        self.assertEqual(set(payload), {"version", "source", "primary", "secondary"})

    def test_legacy_and_secondary_only(self):
        payload = sync.make_payload({"rateLimits": {"secondary": WINDOW}}, "pc")
        self.assertIsNone(payload["primary"])
        self.assertEqual(payload["secondary"], WINDOW)
        self.assertEqual(sync.make_payload({"rateLimits": BUCKET}, "pc")["primary"], WINDOW)

    def test_missing_or_wrong_bucket_is_not_zero(self):
        for response in ({}, {"rateLimits": {}}, {"rateLimits": {"limitId": "other", "primary": WINDOW}},
                         {"rateLimitsByLimitId": {"codex": []}}):
            with self.assertRaises(sync.SyncError):
                sync.make_payload(response, "pc")

    def test_invalid_windows(self):
        for percent in (-1, 101, True, "1", None, float("nan"), float("inf")):
            with self.assertRaises(sync.SyncError):
                sync.normalize_window({**WINDOW, "usedPercent": percent})
        for duration in (0, -1, 1.5, True, "300"):
            with self.assertRaises(sync.SyncError):
                sync.normalize_window({**WINDOW, "windowDurationMins": duration})

    def test_post_and_pairing_failure(self):
        class Response(io.BytesIO):
            status = 200
        with patch.object(sync.urllib.request, "build_opener") as factory:
            factory.return_value.open.return_value = Response(b'{"ok":true}')
            sync.push("192.168.1.42", "012345abcdef", {"version": 1})
            request = factory.return_value.open.call_args.args[0]
            self.assertEqual(request.full_url, "http://192.168.1.42:8765/codex/usage")
            self.assertEqual(request.get_method(), "POST")
            self.assertEqual(request.get_header("X-pairing-code"), "012345abcdef")
            factory.return_value.open.side_effect = urllib.error.HTTPError(request.full_url, 403, "Forbidden", {}, None)
            with self.assertRaisesRegex(sync.SyncError, "pairing"):
                sync.push("192.168.1.42", "012345abcdef", {})
        self.assertIsNone(sync.NoRedirect().redirect_request(None, None, 302, "", {}, "http://other"))

    def test_stdio_initialize_notifications_errors_and_timeout(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = Path(directory) / "codex"
            fake.write_text(f"#!{sys.executable}\n" + '''import json, sys, time
initialized = False
for line in sys.stdin:
    request = json.loads(line)
    method = request.get("method")
    if method == "initialized":
        initialized = True
        continue
    if method == "slow":
        time.sleep(5)
        continue
    if method == "initialize":
        assert request["params"]["clientInfo"]["name"] == "m5stopwatch_usage"
        result = {}
    elif method == "account/rateLimits/read":
        assert initialized
        print(json.dumps({"method":"account/rateLimits/updated", "params":{}}), flush=True)
        result = {"rateLimits":{"primary":{"usedPercent":37,"windowDurationMins":300,"resetsAt":1794000000}}}
    else:
        print(json.dumps({"id":request["id"],"error":{"code":-1}}), flush=True)
        continue
    print(json.dumps({"id":request["id"],"result":result}), flush=True)
''')
            fake.chmod(0o755)
            server = sync.AppServer(str(fake))
            try:
                server.initialize()
                payload = sync.make_payload(server.request("account/rateLimits/read"), "pc")
                self.assertEqual(payload["primary"], WINDOW)
                with self.assertRaises(sync.SyncError):
                    server.request("unknown")
                with self.assertRaisesRegex(sync.SyncError, "timed out"):
                    server.request("slow", timeout=0.05)
            finally:
                server.close()
            self.assertIsNotNone(server.process.poll())


if __name__ == "__main__":
    unittest.main()
