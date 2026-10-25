#!/usr/bin/env python3
import importlib.util
import socket
import threading
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

MODULE_PATH = Path(__file__).with_name("jdt_client.py")
SPEC = importlib.util.spec_from_file_location("jdt_client", MODULE_PATH)
jdt = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(jdt)


class FakeProcess:
    def __init__(self):
        self.terminated = False
        self.killed = False
        self.waited = False

    def poll(self):
        return 0 if self.terminated or self.killed else None

    def terminate(self):
        self.terminated = True

    def kill(self):
        self.killed = True

    def wait(self, timeout=None):
        self.waited = True


class JdtClientLifecycleTest(unittest.TestCase):
    def test_daemon_probe_requires_moss_protocol_identity(self):
        for response, expected in ((b"OK MOSS_JDT 1\n", True),
                                   (b"ERROR Unknown command: PING\n", False)):
            with socket.socket() as server:
                server.bind(("127.0.0.1", 0))
                server.listen(1)
                port = server.getsockname()[1]

                def serve_once():
                    connection, _ = server.accept()
                    with connection:
                        request = connection.recv(128)
                        self.assertEqual(b"PING\n", request)
                        connection.sendall(response)

                thread = threading.Thread(target=serve_once, daemon=True)
                thread.start()
                self.assertEqual(expected, jdt.is_daemon_running(port))
                thread.join(timeout=1)
                self.assertFalse(thread.is_alive())

    def test_eclipse_is_optional_and_discovered_portably(self):
        with patch.dict(jdt.os.environ, {}, clear=True), \
             patch.object(jdt.shutil, "which", return_value=None), \
             patch.object(jdt.os.path, "isfile", return_value=False):
            self.assertIsNone(jdt.find_eclipse())
        with patch.dict(jdt.os.environ, {"MOSS_JDT_EXECUTABLE": "configured-eclipse"}), \
             patch.object(jdt.shutil, "which", return_value=None), \
             patch.object(jdt.os.path, "isfile", return_value=True), \
             patch.object(jdt.os, "access", return_value=True):
            self.assertEqual("configured-eclipse", jdt.find_eclipse())

    def test_status_does_not_start_daemon(self):
        with patch.object(jdt, "is_daemon_running", return_value=False), \
             patch.object(jdt, "find_eclipse") as find_eclipse:
            self.assertEqual(0, jdt.main(["--status"]))
        find_eclipse.assert_not_called()

    def test_stop_sends_stop_only_for_running_daemon(self):
        with patch.object(jdt, "is_daemon_running", return_value=True), \
             patch.object(jdt, "send_command", return_value=("STOPPING", 1.0)) as send:
            self.assertEqual(0, jdt.main(["--stop"]))
        send.assert_called_once_with("STOP")

    def test_start_failure_terminates_child(self):
        process = FakeProcess()
        with tempfile.TemporaryDirectory(dir=jdt.REPO_ROOT / "tmp") as scratch:
            scratch_path = Path(scratch)
            with patch.object(jdt, "find_eclipse", return_value="fake-eclipse"), \
                 patch.object(jdt, "WORKSPACE_DIR", scratch_path / "workspace"), \
                 patch.object(jdt, "BIN_DIR", scratch_path / "bin"), \
                 patch.object(jdt, "LOG_FILE", scratch_path / "daemon.log"), \
                 patch.object(jdt.subprocess, "Popen", return_value=process), \
                 patch.object(jdt, "is_daemon_running", return_value=False), \
                 patch.object(jdt.time, "sleep"):
                self.assertFalse(jdt.start_daemon())
        self.assertTrue(process.terminated)
        self.assertTrue(process.waited)

    def test_build_falls_back_to_javac_after_daemon_start_failure(self):
        with patch.object(jdt, "is_daemon_running", return_value=False), \
             patch.object(jdt, "find_eclipse", return_value="fake-eclipse"), \
             patch.object(jdt, "start_daemon", return_value=False), \
             patch.object(jdt.subprocess, "call", return_value=0) as call:
            self.assertEqual(0, jdt.main(["--clean"]))
        call.assert_called_once_with([str(jdt.BUILD_SCRIPT), "--clean"])


if __name__ == "__main__":
    unittest.main()
