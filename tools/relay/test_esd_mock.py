#!/usr/bin/env python3
"""Exercise fixture wire behavior, not production compatibility."""
import concurrent.futures
import contextlib
import hashlib
import json
import threading
import unittest
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import ProxyHandler, Request, build_opener

from esd_mock import EsdMock, DEVICE_PATH, RELAY_PATH


@contextlib.contextmanager
def running(**kwargs):
    with EsdMock(**kwargs) as server:
        worker = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.01})
        worker.start()
        try:
            yield server
        finally:
            server.shutdown()
            worker.join(timeout=2)


def request(server, path, query, method="POST", timeout=2):
    url = f"http://127.0.0.1:{server.server_port}{path}?{urlencode(query)}"
    req = Request(url, data=b"{}" if method == "POST" else None, method=method)
    # Ignore process proxy settings: fixtures must stay on loopback.
    try:
        response = build_opener(ProxyHandler({})).open(req, timeout=timeout)
    except HTTPError as error:
        response = error
    with response:
        return response.status, response.read()


def device_query(public_key="fixture-camera"):
    # Independent client signing, using the recovered concatenation order.
    digest = hashlib.md5(("fixture-secretaccessKey=fixture-accesspublicKey=" + public_key).encode()).hexdigest()
    return {"accessKey": "fixture-access", "publicKey": public_key, "signature": digest}


def relay_query(deviceid="fixture-device"):
    return {"jsonObject": json.dumps({"deviceid": deviceid, "token": "fixture-token"})}


class EsdFixtureTest(unittest.TestCase):
    def test_two_stage_wire_contract(self):
        with running() as server:
            status, body = request(server, DEVICE_PATH, device_query())
            data = json.loads(body)
            self.assertEqual(status, 200)
            self.assertEqual(data["failflag"], "0")
            self.assertEqual(data["data"]["unifiedId"], "fixture-unified")
            query = {"jsonObject": json.dumps({"deviceid": data["data"]["deviceId"], "token": data["data"]["token"]})}
            status, body = request(server, RELAY_PATH, query)
            self.assertEqual(status, 200)
            self.assertEqual(json.loads(body)["iplist"][0], {"publicip": "127.0.0.1", "relayport": 19091})
            self.assertEqual(list(server.counts.values()), [1, 1])

    def test_query_escaping(self):
        with running() as server:
            _, body = request(server, DEVICE_PATH, device_query("摄像头+ /&=key"))
            self.assertEqual(json.loads(body)["failflag"], "0")

    def test_wrong_signature_and_missing_public_key(self):
        with running() as server:
            for query in ({**device_query(), "signature": "wrong"}, {"accessKey": "fixture-access"}):
                with self.subTest(query=query):
                    status, body = request(server, DEVICE_PATH, query)
                    self.assertEqual(status, 200)
                    self.assertEqual(json.loads(body)["failflag"], "1")
            self.assertEqual(server.counts[DEVICE_PATH], 0)

    def test_relay_requires_lowercase_deviceid_and_matching_token(self):
        with running() as server:
            for credentials in ({"deviceId": "fixture-device", "token": "fixture-token"},
                                {"deviceid": "fixture-device", "token": "wrong"}, []):
                with self.subTest(credentials=credentials):
                    _, body = request(server, RELAY_PATH, {"jsonObject": json.dumps(credentials)})
                    self.assertEqual(json.loads(body)["failflag"], "1")

    def test_fault_modes_on_both_stages(self):
        with running() as server:
            for path, query in ((DEVICE_PATH, device_query()), (RELAY_PATH, relay_query())):
                for mode in ("http-error", "malformed", "business-error", "missing-field"):
                    with self.subTest(path=path, mode=mode):
                        server.modes[path] = mode
                        status, body = request(server, path, query)
                        if mode == "http-error":
                            self.assertEqual(status, 503)
                        elif mode == "malformed":
                            with self.assertRaises(ValueError):
                                json.loads(body)
                        else:
                            self.assertEqual(json.loads(body)["failflag"], "1" if mode == "business-error" else "0")
                            self.assertNotIn("data" if path == DEVICE_PATH else "iplist", json.loads(body))

    def test_empty_relay_list(self):
        with running(relay_mode="empty-relay") as server:
            _, body = request(server, RELAY_PATH, relay_query())
            self.assertEqual(json.loads(body)["iplist"], [])

    def test_deadline(self):
        with running(delay=0.15) as server:
            with self.assertRaises(TimeoutError):
                request(server, DEVICE_PATH, device_query(), timeout=0.02)

    def test_http_method_and_unknown_path(self):
        with running() as server:
            self.assertEqual(request(server, DEVICE_PATH, device_query(), method="GET")[0], 405)
            self.assertEqual(request(server, "/missing", {})[0], 404)

    def test_concurrent_requests(self):
        with running() as server, concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(lambda _: request(server, DEVICE_PATH, device_query()), range(12)))
            self.assertTrue(all(status == 200 and json.loads(body)["failflag"] == "0" for status, body in results))
            self.assertEqual(server.counts[DEVICE_PATH], 12)


if __name__ == "__main__":
    unittest.main()
