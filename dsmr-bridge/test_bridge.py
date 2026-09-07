import json
import os
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.request import urlopen

from bridge import DsmrReaderClient


class TestBridge(unittest.TestCase):
    def test_converts_dsmr_reader_kw_to_homewizard_fields(self):
        old_unit = os.environ.get("DSMR_POWER_UNIT")
        os.environ["DSMR_POWER_UNIT"] = "kw"
        try:
            client = DsmrReaderClient()
            result = client._convert({
                "electricity_currently_delivered": 2.3,
                "electricity_currently_returned": 0.4,
                "phase_currently_delivered_l1": 1.0,
                "phase_currently_returned_l1": 0.1,
                "phase_currently_delivered_l2": 0.8,
                "phase_currently_returned_l2": 0.2,
            })
            self.assertEqual(result["active_power_w"], 1900.0)
            self.assertAlmostEqual(result["active_current_l1_a"], 3.913, places=3)
            self.assertAlmostEqual(result["active_current_l2_a"], 2.609, places=3)
            self.assertNotIn("active_current_l3_a", result)
        finally:
            if old_unit is None:
                os.environ.pop("DSMR_POWER_UNIT", None)
            else:
                os.environ["DSMR_POWER_UNIT"] = old_unit

    def test_unwraps_dsmr_reader_result_list(self):
        self.assertEqual(
            DsmrReaderClient._unwrap({"results": [{"value": 42}]}),
            {"value": 42},
        )


class FakeDsmrHandler(BaseHTTPRequestHandler):
    def do_GET(self):  # noqa: N802
        body = json.dumps({
            "electricity_currently_delivered": 1.2,
            "electricity_currently_returned": 0.2,
        }).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_args):
        pass


if __name__ == "__main__":
    unittest.main()
