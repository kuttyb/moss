#!/usr/bin/env python3
"""Focused regression tests for the optional repository profiling helper.

Run: python3 -m unittest discover -s benchmarks/phase25_4 -p 'test_profile_moss.py'
No Moss compiler binary is needed; the test uses isolated fake subprocesses.
"""
from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("profile_moss", HERE / "profile_moss.py")
assert spec and spec.loader
profile_moss = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile_moss)


class ProfileMossProcessTests(unittest.TestCase):
    PROGRAM = {"id": "test_hi_probe", "category": "tests",
               "path": "examples/hi.moss", "loc": 1, "is_negative": False}

    def fake_compiler(self, folder: Path, body: str) -> Path:
        script = folder / "fake_moss"
        script.write_text("#!/usr/bin/env python3\n" + body + "\n", encoding="utf-8")
        script.chmod(0o755)
        return script

    @unittest.skipUnless(hasattr(__import__("os"), "wait4"), "requires Unix wait4")
    def test_large_stderr_does_not_deadlock(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = self.fake_compiler(Path(directory),
                "import sys\nsys.stderr.write('x' * 262144)\nsys.stderr.flush()\n"
                "sys.stderr.write('\\nMOSS_PROFILE_TOTAL|semantic_analysis|5000000|1\\n')")
            result = profile_moss.profile_single_program(self.PROGRAM, executable, timeout_s=3)
            self.assertEqual(result["outcome"], "success")
            self.assertEqual(result["exit_code"], 0)
            self.assertEqual(result["semantic_analysis_ms"], 5.0)

    @unittest.skipUnless(hasattr(__import__("os"), "wait4"), "requires Unix wait4")
    def test_timeout_terminates_process(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = self.fake_compiler(Path(directory), "import time\ntime.sleep(10)")
            result = profile_moss.profile_single_program(self.PROGRAM, executable, timeout_s=0.2)
            self.assertEqual(result["outcome"], "timeout")
            self.assertEqual(result["exit_code"], -1)
            self.assertLess(result["wall_time_ms"], 5000)

    @unittest.skipUnless(hasattr(__import__("os"), "wait4"), "requires Unix wait4")
    def test_nonzero_exit_is_failure_not_profiler_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = self.fake_compiler(Path(directory), "raise SystemExit(7)")
            result = profile_moss.profile_single_program(self.PROGRAM, executable, timeout_s=3)
            self.assertEqual(result["outcome"], "failure")
            self.assertEqual(result["exit_code"], 7)


if __name__ == "__main__":
    unittest.main()
