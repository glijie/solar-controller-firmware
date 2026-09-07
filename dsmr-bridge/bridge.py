#!/usr/bin/env python3
"""Expose DSMR Reader readings through the HomeWizard P1 API."""

from __future__ import annotations

import json
import logging
import os
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any


LOG = logging.getLogger("dsmr-homewizard-bridge")


def number(value: Any) -> float | None:
    if value is None or isinstance(value, bool):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def get_path(value: Any, path: str) -> Any:
    for part in path.split("."):
        if isinstance(value, dict):
            value = value.get(part)
        else:
            return None
    return value


def first_number(value: Any, paths: list[str]) -> float | None:
    for path in paths:
        result = number(get_path(value, path))
        if result is not None:
            return result
    return None


def env_number(name: str, default: float) -> float:
    value = number(os.getenv(name))
    return default if value is None else value


class DsmrReaderClient:
    def __init__(self) -> None:
        self.url = os.getenv(
            "DSMR_READER_URL",
            "http://127.0.0.1:7777/api/v2/datalogger/dsmrreading?limit=1&ordering=-timestamp",
        )
        self.api_key = os.getenv("DSMR_READER_API_KEY", "")
        self.timeout = env_number("DSMR_READER_TIMEOUT_S", 5.0)
        self.voltage = env_number("PHASE_VOLTAGE_V", 230.0)
        self.power_unit = os.getenv("DSMR_POWER_UNIT", "kw").lower()

    def read(self) -> dict[str, Any]:
        request = urllib.request.Request(self.url, headers={"Accept": "application/json"})
        if self.api_key:
            request.add_header("X-AUTHKEY", self.api_key)
        with urllib.request.urlopen(request, timeout=self.timeout) as response:
            payload = json.loads(response.read().decode("utf-8"))
        return self._convert(self._unwrap(payload))

    @staticmethod
    def _unwrap(payload: Any) -> dict[str, Any]:
        if isinstance(payload, list):
            if not payload or not isinstance(payload[0], dict):
                raise ValueError("DSMR Reader returned an empty reading list")
            return payload[0]
        if not isinstance(payload, dict):
            raise ValueError("DSMR Reader response is not a JSON object")
        for key in ("results", "data", "reading"):
            nested = payload.get(key)
            if isinstance(nested, list) and nested and isinstance(nested[0], dict):
                return nested[0]
            if isinstance(nested, dict):
                return nested
        return payload

    def _watts(self, value: float) -> float:
        return value * 1000.0 if self.power_unit in ("kw", "kwh") else value

    def _convert(self, reading: dict[str, Any]) -> dict[str, Any]:
        delivered = first_number(reading, [
            "electricity_currently_delivered",
            "currently_delivered",
            "active_power_import_kw",
        ])
        returned = first_number(reading, [
            "electricity_currently_returned",
            "currently_returned",
            "active_power_export_kw",
        ])
        if delivered is None and returned is None:
            direct_power = first_number(reading, ["active_power_w", "active_power"])
            if direct_power is None:
                raise ValueError("No DSMR power fields found in response")
            active_power = direct_power
        else:
            active_power = self._watts(delivered or 0.0) - self._watts(returned or 0.0)

        result: dict[str, Any] = {
            "active_power_w": round(active_power, 3),
            "meter_model": "DSMR Reader bridge",
            "smr_version": 50,
            "unique_id": os.getenv("BRIDGE_UNIQUE_ID", "dsmr-reader-bridge"),
            "bridge_timestamp": int(time.time()),
        }
        for phase in ("l1", "l2", "l3"):
            voltage = first_number(reading, [f"phase_voltage_{phase}"]) or self.voltage
            phase_delivered = first_number(reading, [
                f"phase_currently_delivered_{phase}",
                f"currently_delivered_{phase}",
            ])
            phase_returned = first_number(reading, [
                f"phase_currently_returned_{phase}",
                f"currently_returned_{phase}",
            ])
            direct_current = first_number(reading, [
                f"active_current_{phase}_a",
                f"phase_current_{phase}_a",
            ])
            direct_watts = first_number(reading, [f"active_power_{phase}_w"])

            if phase_delivered is not None or phase_returned is not None:
                # DSMR Reader reports per-phase net kW; current is derived using
                # the reported phase voltage so the value matches a real P1 meter.
                watts = self._watts(phase_delivered or 0.0) - self._watts(phase_returned or 0.0)
                current = watts / voltage if voltage else None
            elif direct_watts is not None:
                watts = direct_watts
                current = watts / voltage if voltage else None
            elif direct_current is not None:
                current = direct_current
                watts = current * voltage if voltage else None
            else:
                continue

            if watts is not None:
                result[f"active_power_{phase}_w"] = round(watts, 3)
            if current is not None:
                result[f"active_current_{phase}_a"] = round(current, 3)
            if voltage is not None:
                result[f"active_voltage_{phase}_v"] = round(voltage, 1)

        return result


class BridgeState:
    def __init__(self, client: DsmrReaderClient) -> None:
        self.client = client
        self.stale_after = env_number("STALE_AFTER_S", 30.0)
        self._lock = threading.Lock()
        self._data: dict[str, Any] | None = None
        self._updated_at = 0.0
        self._error: str | None = None

    def refresh(self) -> None:
        try:
            data = self.client.read()
            with self._lock:
                self._data = data
                self._updated_at = time.time()
                self._error = None
            LOG.info("Updated active power: %.1f W", data["active_power_w"])
        except Exception as error:  # noqa: BLE001 - bridge must remain available
            with self._lock:
                self._error = str(error)
            LOG.warning("DSMR Reader update failed: %s", error)

    def data(self) -> tuple[dict[str, Any] | None, float, str | None]:
        with self._lock:
            age = time.time() - self._updated_at if self._updated_at else float("inf")
            return self._data, age, self._error


class Handler(BaseHTTPRequestHandler):
    state: BridgeState

    def do_GET(self) -> None:  # noqa: N802
        data, age, error = self.state.data()
        if self.path.split("?", 1)[0] == "/health":
            self._json({
                "ok": data is not None and age <= self.state.stale_after,
                "age_s": None if data is None else round(age, 3),
                "error": error,
            }, 200 if data is not None and age <= self.state.stale_after else 503)
            return
        if self.path.split("?", 1)[0] != "/api/v1/data":
            self._json({"error": "not found"}, 404)
            return
        if data is None or age > self.state.stale_after:
            self._json({"error": error or "no fresh DSMR Reader data", "age_s": age}, 503)
            return
        self._json(data, 200)

    def _json(self, value: dict[str, Any], status: int) -> None:
        body = json.dumps(value, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format: str, *args: Any) -> None:
        LOG.info("%s - %s", self.address_string(), format % args)


def main() -> None:
    logging.basicConfig(level=os.getenv("LOG_LEVEL", "INFO"))
    client = DsmrReaderClient()
    state = BridgeState(client)
    poll_interval = env_number("POLL_INTERVAL_S", 5.0)

    def refresh_loop() -> None:
        while True:
            state.refresh()
            time.sleep(poll_interval)

    threading.Thread(target=refresh_loop, name="dsmr-refresh", daemon=True).start()
    Handler.state = state
    host = os.getenv("BRIDGE_HOST", "0.0.0.0")
    port = int(os.getenv("BRIDGE_PORT", "80"))
    server = ThreadingHTTPServer((host, port), Handler)
    LOG.info("Serving HomeWizard P1 API on http://%s:%s/api/v1/data", host, port)
    server.serve_forever()


if __name__ == "__main__":
    main()
