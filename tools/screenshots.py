#!/usr/bin/env python3
"""README screenshots of the app, rendered with mocked API data in headless Chrome.

Usage: tools/screenshots.py [--out docs/img] [--scene live-running] [--keep-server]

Serves firmware/web/app.html through home-idf's render_page.py with a mock of /api/session, /api/status,
/api/events and /api/history, then shoots each scene at 390x844 with a device pixel ratio of 2 (iPhone size,
the same as the other controllers' READMEs). Animations are switched off so the images are reproducible.
"""
import argparse
import http.server
import json
import math
import pathlib
import subprocess
import sys
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
WEB = ROOT / "firmware" / "web"
HOME_IDF = ROOT.parent / "home-idf"
sys.path.insert(0, str(HOME_IDF / "tools"))
import render_page  # noqa: E402

CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
NAME = "fh-controller"
WIDTH, HEIGHT, SCALE = 390, 844, 2          # iPhone viewport and the pixel ratio of the saved image
MIN_VIEWPORT = 500                          # headless Chrome refuses a narrower window, so the page is zoomed instead
ZOOM = MIN_VIEWPORT / WIDTH

SETTINGS = {
    "start_c": 30.0, "stop_c": 25.0, "critical_c": 45.0,
    "start_min_c": 20.0, "start_max_c": 60.0, "stop_min_c": 10.0,
    "hysteresis_min_c": 2.0, "critical_margin_min_c": 5.0, "critical_max_c": 80.0,
    "maintenance_interval_s": 7 * 86400, "maintenance_run_s": 60, "manual_max_s": 12 * 3600,
}

SYSTEM = {
    "version": "2.0.0", "idf": "v5.4.2", "bootloader_idf": "v5.4.2", "partition": "ota_0",
    "pending_verify": False, "reset_reason": "software restart", "uptime_s": 41 * 3600 + 1500,
    "up_since": "2026-09-16 04:12", "time_synced": True, "ota_running": False,
    "heap_kb": {"free": 148, "min": 131},
    "wifi": {"connected": True, "rssi": -58, "channel": 6, "bssid": "aa:bb:cc:dd:ee:ff"},
}


def status(pump, temp, today, since_boot):
    return {"pump": pump, "temp": temp, "today": today, "since_boot": since_boot,
            "settings": SETTINGS, "system": SYSTEM}


def pump(on, reason, state_s, manual="none", manual_left_s=0, idle_s=0):
    return {"on": on, "reason": reason, "since": 0, "state_s": state_s, "manual": manual,
            "manual_left_s": manual_left_s, "maintenance": reason == "maintenance",
            "idle_s": 0 if on else idle_s, "control_alive": True}


def temp(c, age_s=3, failures=0, sensor_failed=False, overheat=False):
    return {"c": c, "age_s": age_s, "failures": failures, "sensor_failed": sensor_failed, "overheat": overheat}


def events(now):
    """Newest first, the shape /api/events returns."""
    def e(minutes_ago, type_, on, reason, prev_reason="none", temp_c=None, detail=""):
        return {"ts": now - minutes_ago * 60, "type": type_, "on": on, "reason": reason,
                "prev_reason": prev_reason, "temp_c": temp_c, "detail": detail}
    return [
        e(43, "pump", True, "auto_hot", "off", 30.2),
        e(96, "pump", False, "off", "auto_hot", 24.8),
        e(151, "pump", True, "auto_hot", "off", 30.1),
        e(212, "pump", False, "off", "manual_on", 27.4, "back to auto"),
        e(272, "pump", True, "manual_on", "off", 26.9, "1 h"),
        e(388, "pump", False, "off", "auto_hot", 24.9),
        e(454, "pump", True, "auto_hot", "off", 30.3),
        e(705, "pump", False, "off", "auto_hot", 24.7),
        e(769, "pump", True, "auto_hot", "off", 30.0),
        e(1122, "pump", False, "off", "maintenance", 21.6),
        e(1123, "pump", True, "maintenance", "off", 21.5),
    ]


def history(now):
    """24 h of one-minute samples, oldest first: a boiler that fires in uneven bursts plus the hysteresis."""
    n = 24 * 60
    bursts = [(20, 50), (150, 45), (300, 55), (430, 40), (560, 60), (700, 50),   # a quiet midday follows
              (980, 55), (1100, 45), (1230, 60), (1350, 40)]
    heating = [False] * n
    for begin, length in bursts:
        for i in range(begin, min(n, begin + length)):
            heating[i] = True

    t, p, c, on = [], [], 20.5, False
    for i in range(n):
        target, rate = (37.5, 0.10) if heating[i] else (17.5, 0.04)
        c += (target - c) * rate + 0.05 * math.sin(i / 11.0)
        if on and c < SETTINGS["stop_c"]:
            on = False
        elif not on and c > SETTINGS["start_c"]:
            on = True
        t.append(round(c * 10))
        p.append("1" if on else "0")
    return {"period_s": 60, "newest": now, "newest_age_s": 8, "t": t, "p": "".join(p)}


NOW = int(time.time())

SCENES = {
    "live-running": {"tab": "live", "status": status(
        pump(True, "auto_hot", 43 * 60), temp(34.2),
        {"runtime_s": 3 * 3600 + 25 * 60, "starts": 4}, {"runtime_s": 9 * 3600, "starts": 11})},
    "live-manual": {"tab": "live", "status": status(
        pump(True, "manual_on", 12 * 60, manual="start", manual_left_s=47 * 60), temp(27.6),
        {"runtime_s": 2 * 3600 + 10 * 60, "starts": 3}, {"runtime_s": 8 * 3600, "starts": 10})},
    "live-idle": {"tab": "live", "status": status(
        pump(False, "off", 26 * 60, idle_s=26 * 60), temp(23.4),
        {"runtime_s": 3 * 3600 + 25 * 60, "starts": 4}, {"runtime_s": 9 * 3600, "starts": 11})},
    "history": {"tab": "history", "status": status(
        pump(True, "auto_hot", 43 * 60), temp(34.2),
        {"runtime_s": 3 * 3600 + 25 * 60, "starts": 4}, {"runtime_s": 9 * 3600, "starts": 11})},
    "settings": {"tab": "settings", "status": status(
        pump(False, "off", 26 * 60, idle_s=26 * 60), temp(23.4),
        {"runtime_s": 3 * 3600 + 25 * 60, "starts": 4}, {"runtime_s": 9 * 3600, "starts": 11})},
}

README_SCENES = ["live-running", "live-idle", "history", "settings"]

INJECT = """
<style>
  html { zoom: @ZOOM@; }                       /* headless Chrome clamps the window to 500 px: zoom back to a phone */
  *, *::before, *::after { animation: none !important; transition: none !important; }
</style>
<script>
(() => {
  // The page polls /api/status every 3 s; under Chrome's virtual clock that loop never lets the shot happen.
  const real = window.setTimeout;
  window.setTimeout = (fn, ms, ...rest) => (ms >= 2000 ? 0 : real(fn, ms, ...rest));
  const tab = new URLSearchParams(location.search).get("tab") || "live";
  if (tab !== "live") real(() => document.querySelector(`[data-tab="${tab}"]`).click(), 400);
})();
</script>
"""

current = "live-running"


class Mock(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def send(self, body, content_type):
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def json(self, obj):
        self.send(json.dumps(obj).encode(), "application/json")

    def do_GET(self):
        global current
        path, _, query = self.path.partition("?")
        params = dict(kv.split("=", 1) for kv in query.split("&") if "=" in kv)
        if path == "/":
            current = params.get("scene", current)
            page = render_page.render((WEB / "app.html").read_text("utf-8"), NAME)
            inject = INJECT.replace("@ZOOM@", f"{ZOOM:.5f}")
            self.send(page.replace("</body>", inject + "</body>").encode(), "text/html; charset=utf-8")
        elif path == "/api/session":
            self.json({"authenticated": True, "csrf": "mock", "version": SYSTEM["version"]})
        elif path == "/api/status":
            self.json(SCENES[current]["status"])
        elif path == "/api/events":
            self.json({"events": events(NOW)})
        elif path == "/api/history":
            self.json(history(NOW))
        elif path == "/apple-touch-icon.png":
            self.send((WEB / "apple-touch-icon.png").read_bytes(), "image/png")
        elif path == "/manifest.webmanifest":
            self.send((WEB / "manifest.webmanifest").read_bytes(), "application/manifest+json")
        else:
            self.send_error(404)

    def do_POST(self):
        self.json({})


def shoot(port, scene, out):
    """Chrome writes the file and then hangs on this machine, so it is killed once the image is there."""
    url = f"http://127.0.0.1:{port}/?scene={scene}&tab={SCENES[scene]['tab']}"
    window = (MIN_VIEWPORT, round(HEIGHT * ZOOM))
    raw = out.with_suffix(".raw.png")
    raw.unlink(missing_ok=True)
    with tempfile.TemporaryDirectory() as profile:
        chrome = subprocess.Popen(
            [CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars", f"--user-data-dir={profile}",
             f"--window-size={window[0]},{window[1]}", f"--force-device-scale-factor={SCALE}",
             "--virtual-time-budget=4000", f"--screenshot={raw}", url],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline, size = time.time() + 90, -1
        while time.time() < deadline:
            time.sleep(0.5)
            now = raw.stat().st_size if raw.exists() else -1
            if now > 0 and now == size:      # the file stopped growing: the shot is complete
                break
            size = now
            if chrome.poll() is not None:
                break
        chrome.kill()
        chrome.wait()
    if not raw.exists():
        raise SystemExit(f"{scene}: Chrome produced no image")
    subprocess.run(["sips", "-z", str(HEIGHT * SCALE), str(WIDTH * SCALE), str(raw), "--out", str(out)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    raw.unlink()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "docs" / "img")
    ap.add_argument("--scene", action="append", choices=sorted(SCENES),
                    help=f"default: the README set ({', '.join(README_SCENES)})")
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--keep-server", action="store_true", help="leave the mock server running for a browser")
    args = ap.parse_args()

    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Mock)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    if args.keep_server:
        print(f"mock on http://127.0.0.1:{args.port}/?scene=live-running  (ctrl-c to stop)")
        try:
            while True:
                time.sleep(3600)
        except KeyboardInterrupt:
            return

    args.out.mkdir(parents=True, exist_ok=True)
    for scene in args.scene or README_SCENES:
        out = args.out / f"app-{scene}.png"
        shoot(args.port, scene, out)
        print(f"{out.relative_to(ROOT)}  {out.stat().st_size // 1024} KB")
    server.shutdown()


if __name__ == "__main__":
    main()
