"""Tests of fh-ingest. Parser tests need nothing; database tests run the real schema on an embedded PostgreSQL:

  uv run --python 3.12 --with pgserver --with 'psycopg[binary]' --with paho-mqtt --with ../hc-data/python \
      python -m unittest server/ingest/test_fh_ingest.py

All values are synthetic.
"""
import json
import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from fh_ingest import parse, store_message          # noqa: E402

try:
    import pgserver
    import psycopg
except ImportError:
    pgserver = psycopg = None

SCHEMA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "db", "schema.sql")
DEV = "98cdac4e7554"


def state(on=False, c=21.9, age=1, **kw):
    s = {"pump": {"on": on, "reason": "auto_warm" if on else "auto_cold", "since": 1791885622, "state_s": 60,
                  "manual": "none", "manual_left_s": 0, "maintenance": False, "idle_s": 0, "control_alive": True},
         "temp": {"c": c, "age_s": age, "failures": 0, "sensor_failed": False, "overheat": False},
         "today": {"runtime_s": 3600, "starts": 4},
         "since_boot": {"runtime_s": 3600, "starts": 4},
         "settings": {"start_c": 30, "stop_c": 25, "critical_c": 45},
         "system": {"version": "2.2.0"}}
    s.update(kw)
    return json.dumps(s).encode()


class ParseTest(unittest.TestCase):
    def test_state(self):
        kind, f = parse(f"floorheating/{DEV}/state", state(on=True, c=31.2))
        self.assertEqual(kind, "state")
        self.assertEqual((f["device"], f["temp_c"], f["pump_on"], f["reason"], f["start_c"], f["starts_today"]),
                         (DEV, 31.2, True, "auto_warm", 30.0, 4))

    def test_no_reading(self):
        s = json.loads(state())
        s["temp"]["c"] = None
        s["temp"]["age_s"] = -1
        _, f = parse(f"floorheating/{DEV}/state", json.dumps(s).encode())
        self.assertIsNone(f["temp_c"])
        self.assertIsNone(f["temp_age_s"])

    def test_status(self):
        self.assertEqual(parse(f"floorheating/{DEV}/status", b"offline"),
                         ("status", {"device": DEV, "online": False}))

    def test_rejects(self):
        for topic, payload in [(f"floorheating/{DEV}/other", state()), ("floorheating/XYZ/state", state()),
                               (f"gate/{DEV}/state", state()), (f"floorheating/{DEV}/state", b"[]"),
                               (f"floorheating/{DEV}/state", b"{}"), (f"floorheating/{DEV}/status", b"maybe")]:
            with self.assertRaises(ValueError, msg=topic):
                parse(topic, payload)
        s = json.loads(state())
        s["temp"]["c"] = 300
        with self.assertRaises(ValueError):
            parse(f"floorheating/{DEV}/state", json.dumps(s).encode())


@unittest.skipIf(pgserver is None, "pgserver/psycopg not installed")
class DbTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        cls.srv = pgserver.get_server(cls.tmp, cleanup_mode="stop")
        cls.srv.psql("CREATE ROLE fh_ingest; CREATE ROLE fh_read;")
        cls.db = psycopg.connect(cls.srv.get_uri(), autocommit=True)
        cls.db.execute(open(SCHEMA).read())
        cls.db.execute(open(SCHEMA).read())        # idempotent

    @classmethod
    def tearDownClass(cls):
        cls.db.close()
        cls.srv.cleanup()

    def test_store(self):
        now = time.time()
        with self.db.transaction():
            store_message(self.db, *parse(f"floorheating/{DEV}/state", state(on=True, c=30.4)), now=now)
            store_message(self.db, *parse(f"floorheating/{DEV}/state", state(c=99.0 / 3)), now=now - 600,
                          retained=True)
            store_message(self.db, *parse(f"floorheating/{DEV}/status", b"online"))
        rows = self.db.execute("SELECT temp_c, pump_on, reason FROM sample WHERE device = %s", (DEV,)).fetchall()
        self.assertEqual(rows, [(30.4, True, "auto_warm")])
        st = self.db.execute("SELECT online, last_sample_ts IS NOT NULL FROM device_status").fetchone()
        self.assertEqual(st, (True, True))


if __name__ == "__main__":
    unittest.main()
