import json
import os
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.request import urlopen

from bridge import BridgeState, DsmrReaderClient


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

    def test_uses_dsmr_measured_phase_current_when_available(self):
        old_unit = os.environ.get("DSMR_POWER_UNIT")
        os.environ["DSMR_POWER_UNIT"] = "kw"
        try:
            client = DsmrReaderClient()
            result = client._convert({
                "electricity_currently_delivered": 1.0,
                "electricity_currently_returned": 0.0,
                "phase_currently_delivered_l1": 0.5,
                "phase_currently_returned_l1": 0.1,
                "phase_power_current_l1": 2.5,
                "phase_voltage_l1": 230.0,
            })
            # DSMR's measured current wins over deriving it from watts/voltage.
            self.assertEqual(result["active_current_l1_a"], 2.5)
            # Watts still come from the per-phase kWh values.
            self.assertAlmostEqual(result["active_power_l1_w"], 400.0, places=1)
        finally:
            if old_unit is None:
                os.environ.pop("DSMR_POWER_UNIT", None)
            else:
                os.environ["DSMR_POWER_UNIT"] = old_unit

    def test_exporting_phase_current_keeps_negative_sign(self):
        old_unit = os.environ.get("DSMR_POWER_UNIT")
        os.environ["DSMR_POWER_UNIT"] = "kw"
        try:
            client = DsmrReaderClient()
            result = client._convert({
                "electricity_currently_delivered": 0.0,
                "electricity_currently_returned": 1.0,
                "phase_currently_delivered_l3": 0.0,
                "phase_currently_returned_l3": 0.9,
                "phase_power_current_l3": 4,
                "phase_voltage_l3": 230.0,
            })
            self.assertAlmostEqual(result["active_power_l3_w"], -900.0, places=1)
            self.assertEqual(result["active_current_l3_a"], -4)
        finally:
            if old_unit is None:
                os.environ.pop("DSMR_POWER_UNIT", None)
            else:
                os.environ["DSMR_POWER_UNIT"] = old_unit

    def test_staleness_is_based_on_dsmr_reading_timestamp(self):
        client = DsmrReaderClient()
        # Poll just succeeded, but the meter reading itself is 120 s old.
        state = BridgeState(client)
        state._data = {
            "active_power_w": 100.0,
            "dsmr_timestamp": time.time() - 120,
        }
        state._updated_at = time.time()
        _, age, _ = state.data()
        # Age reflects the DSMR reading, not the fresh poll.
        self.assertGreater(age, 100)
        self.assertLess(age, 130)

    def test_reading_timestamp_is_parsed(self):
        client = DsmrReaderClient()
        result = client._convert({
            "timestamp": "2026-09-07T13:43:51+02:00",
            "electricity_currently_delivered": 1.0,
        })
        self.assertIn("dsmr_timestamp", result)

    def test_exposes_tariff_totals_as_homewizard_fields(self):
        client = DsmrReaderClient()
        result = client._convert({
            "electricity_currently_delivered": 1.0,
            "electricity_delivered_1": 15903.486,
            "electricity_delivered_2": 8246.746,
            "electricity_returned_1": 4748.183,
            "electricity_returned_2": 11238.748,
        })
        self.assertEqual(result["total_power_import_t1_kwh"], 15903.486)
        self.assertEqual(result["total_power_import_t2_kwh"], 8246.746)
        self.assertEqual(result["total_power_export_t1_kwh"], 4748.183)
        self.assertEqual(result["total_power_export_t2_kwh"], 11238.748)

    def test_exposes_partial_tariff_totals(self):
        # A single-tariff meter only reports tariff 1; the missing fields are left out.
        client = DsmrReaderClient()
        result = client._convert({
            "electricity_currently_delivered": 1.0,
            "electricity_delivered_1": 1234.567,
        })
        self.assertEqual(result["total_power_import_t1_kwh"], 1234.567)
        self.assertNotIn("total_power_import_t2_kwh", result)
        self.assertNotIn("total_power_export_t1_kwh", result)

    def test_omits_tariff_totals_without_split(self):
        client = DsmrReaderClient()
        result = client._convert({"electricity_currently_delivered": 1.0})
        self.assertNotIn("total_power_import_t1_kwh", result)
        self.assertNotIn("total_power_export_t2_kwh", result)


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
