#!/usr/bin/env python3
"""G3-08: execute the sparse-map snapshot complexity and ownership probe."""

from __future__ import annotations

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[3]
RUNTIME = ROOT / "runtime/moss_runtime/src/lib.rs"
SCRATCH = ROOT / "tmp/phase25-g3-map-snapshot"
TEST_NAME = "map::tests::sparse_map_snapshot_scans_and_clones_only_linear_live_storage"


def main() -> None:
    SCRATCH.mkdir(parents=True, exist_ok=True)
    executable = SCRATCH / "runtime_tests"
    build = subprocess.run(["rustc", "--edition=2021", "--test", str(RUNTIME),
                            "-o", str(executable)], cwd=ROOT, text=True,
                           capture_output=True, check=False)
    assert build.returncode == 0, build.stdout + build.stderr
    test = subprocess.run([str(executable), "--exact", TEST_NAME], cwd=ROOT,
                          text=True, capture_output=True, check=False)
    assert test.returncode == 0, test.stdout + test.stderr
    assert "running 1 test" in test.stdout, test.stdout
    assert f"test {TEST_NAME} ... ok" in test.stdout, test.stdout
    assert "test result: ok. 1 passed; 0 failed;" in test.stdout, test.stdout
    print("G3-08 sparse Map snapshot live-storage bound and detached clone PASS")


if __name__ == "__main__":
    main()
