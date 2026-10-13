#!/usr/bin/env python3
"""MQTT -> PostgreSQL ingest for floor-heating-controller.

  floorheating/<mac>/state    the device's status document (GET /admin/status), every 60 s and on a pump change
                              -> sample (stamped on receipt), device_status.last_sample_ts
  floorheating/<mac>/status   retained online / offline (LWT) -> device_status.online

A retained state delivered on (re)subscription is old: it is not stored as a sample.
"""
import json
import logging
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from hcdata import validate as v                              # noqa: E402
from hcdata.ingest import Ingest, setup_logging               # noqa: E402

WORD = r"[a-z][a-z0-9_]{0,31}"
COLUMNS = ("temp_c", "temp_age_s", "pump_on", "reason", "manual", "maintenance", "overheat", "sensor_failed",
           "start_c", "stop_c", "critical_c", "runtime_today_s", "starts_today")

log = logging.getLogger("fh-ingest")


def _object(data, key):
    o = data.get(key)
    if not isinstance(o, dict):
        raise ValueError(f"{key} not an object")
    return o


def parse(topic, payload):
    """Returns (kind, fields) or raises ValueError."""
    parts = topic.split("/")
    if len(parts) != 3 or parts[0] != "floorheating":
        raise ValueError("unexpected topic")
    _, device, kind = parts
    v.device_id(device)
    if kind == "status":
        status = payload.decode("utf-8", "replace")
        if status not in ("online", "offline"):
            raise ValueError("bad status")
        return "status", {"device": device, "online": status == "online"}
    if kind != "state":
        raise ValueError("unexpected topic")
    data = json.loads(payload)
    if not isinstance(data, dict):
        raise ValueError("payload not an object")
    pump, temp, settings, today = (_object(data, k) for k in ("pump", "temp", "settings", "today"))
    age = v.integer(temp, "age_s", lo=-1, hi=10**9)
    return "state", {
        "device": device,
        "temp_c": v.number(temp, "c", lo=-55, hi=125),
        "temp_age_s": None if age < 0 else age,
        "pump_on": v.boolean(pump, "on"),
        "reason": v.string(pump, "reason", required=True, pattern=WORD),
        "manual": v.string(pump, "manual", required=True, pattern=WORD),
        "maintenance": v.boolean(pump, "maintenance"),
        "overheat": v.boolean(temp, "overheat"),
        "sensor_failed": v.boolean(temp, "sensor_failed"),
        "start_c": v.number(settings, "start_c", lo=0, hi=100),
        "stop_c": v.number(settings, "stop_c", lo=0, hi=100),
        "critical_c": v.number(settings, "critical_c", lo=0, hi=100),
        "runtime_today_s": v.integer(today, "runtime_s", hi=86400 * 2, required=False),
        "starts_today": v.integer(today, "starts", hi=100000, required=False),
    }


def store_message(db, kind, f, now=None, retained=False):
    if kind == "status":
        db.execute(
            "INSERT INTO device_status (device, online, status_ts) VALUES (%s, %s, now()) "
            "ON CONFLICT (device) DO UPDATE SET online = EXCLUDED.online, status_ts = EXCLUDED.status_ts",
            (f["device"], f["online"]))
        return
    if retained:
        return                          # replayed on subscription: its time is unknown
    ts = time.time() if now is None else now
    cols = ", ".join(COLUMNS)
    db.execute(
        f"INSERT INTO sample (device, ts, {cols}) VALUES (%s, to_timestamp(%s), {', '.join(['%s'] * len(COLUMNS))}) "
        "ON CONFLICT (device, ts) DO NOTHING",
        (f["device"], ts, *(f[c] for c in COLUMNS)))
    db.execute(
        "INSERT INTO device_status (device, last_sample_ts) VALUES (%s, to_timestamp(%s)) "
        "ON CONFLICT (device) DO UPDATE SET last_sample_ts = greatest(device_status.last_sample_ts, "
        "EXCLUDED.last_sample_ts)",
        (f["device"], ts))


class FhIngest(Ingest):
    client_id = "fh-ingest"
    username = "fh-ingest"
    password_env = "MQTT_INGEST_PASS"
    subscriptions = ("floorheating/+/state", "floorheating/+/status")

    def _on_message(self, client, userdata, msg):
        self._retained = bool(msg.retain)
        super()._on_message(client, userdata, msg)

    def handle(self, topic, payload, db):
        kind, f = parse(topic, payload)
        store_message(db, kind, f, retained=self._retained)


if __name__ == "__main__":
    setup_logging()
    FhIngest().run()
