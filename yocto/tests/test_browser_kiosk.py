"""Exercise startup readiness against a real HTTP server, without a display."""
import http.server
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import threading
import time
import unittest


WRAPPER = (Path(__file__).resolve().parents[1] / "meta-project" /
           "recipes-graphics/browser-kiosk/files/browser-kiosk")


@unittest.skipUnless(shutil.which("curl"), "curl is required")
class BrowserKioskTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="browser-kiosk-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.launch = self.root / "launch.json"
        # Replace only the display-dependent command; readiness uses real curl.
        stub = self.root / "dbus-run-session"
        stub.write_text(
            "#!/usr/bin/env python3\n"
            "import json, os, pathlib, sys\n"
            "pathlib.Path(os.environ['TEST_LAUNCH']).write_text(json.dumps({\n"
            " 'args': sys.argv[1:],\n"
            " 'retry': os.environ.get('COG_EXIT_ON_LOAD_FAILURE')}))\n"
            "sys.exit(23)\n")
        stub.chmod(0o755)
        self.ready = threading.Event()
        self.requested = threading.Event()
        ready, requested = self.ready, self.requested

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                requested.set()
                if not ready.is_set():
                    self.send_response(503)
                elif self.path == "/redirect":
                    self.send_response(302)
                    self.send_header("Location", "/app")
                else:
                    self.send_response(200)
                self.send_header("Content-Length", "0")
                self.end_headers()

            def log_message(self, *args):
                pass

        self.server = http.server.ThreadingHTTPServer(
            ("127.0.0.1", 0), Handler, bind_and_activate=False)
        self.server.server_bind()
        self.addCleanup(self.server.server_close)
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}/redirect"

    def start_server(self):
        self.server.server_activate()
        thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        thread.start()
        self.addCleanup(thread.join)
        self.addCleanup(self.server.shutdown)

    def start_wrapper(self):
        env = dict(os.environ, KIOSK_URL=self.url,
                   TEST_LAUNCH=str(self.launch),
                   PATH=f"{self.root}:{os.environ['PATH']}",
                   http_proxy="http://127.0.0.1:1", no_proxy="")
        process = subprocess.Popen(
            ["sh", str(WRAPPER)], env=env, start_new_session=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

        def stop():
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
            process.communicate(timeout=5)

        self.addCleanup(stop)
        return process

    def assert_launched(self, process):
        _, stderr = process.communicate(timeout=8)
        # dbus-run-session/Cog's exit status reaches systemd for restart.
        self.assertEqual(process.returncode, 23, stderr)
        result = json.loads(self.launch.read_text())
        self.assertEqual(result["retry"], "1")
        self.assertEqual(result["args"], [
            "--", "cog", "--platform=wl", "--webprocess-failure=exit", self.url])

    def test_waits_for_http_readiness_then_follows_redirect(self):
        self.start_server()
        process = self.start_wrapper()
        self.assertTrue(self.requested.wait(timeout=5))
        time.sleep(0.1)
        self.assertFalse(self.launch.exists(), "launched against HTTP 503")
        self.assertIsNone(process.poll())
        self.ready.set()
        self.assert_launched(process)

    def test_connection_refused_then_server_starts(self):
        process = self.start_wrapper()
        time.sleep(0.3)
        self.assertFalse(self.launch.exists())
        self.assertIsNone(process.poll())
        self.ready.set()
        self.start_server()
        self.assert_launched(process)

    def test_unavailable_server_keeps_waiting(self):
        self.start_server()
        process = self.start_wrapper()
        self.assertTrue(self.requested.wait(timeout=5))
        time.sleep(2.2)
        self.assertFalse(self.launch.exists())
        self.assertIsNone(process.poll())


if __name__ == "__main__":
    unittest.main()
