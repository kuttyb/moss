#!/usr/bin/env python3
"""Self-test per-check timeouts and process-tree cleanup."""
from __future__ import annotations

import os
from pathlib import Path
import shutil
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run_parallel  # noqa: E402


def process_is_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    stat_path = Path(f"/proc/{pid}/stat")
    if stat_path.exists():
        return stat_path.read_text().split()[2] != "Z"
    return True


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    scratch = root / "tmp" / "run-parallel-timeout-self-test"
    shutil.rmtree(scratch, ignore_errors=True)
    scratch.mkdir(parents=True)
    pid_file = scratch / "descendant.pid"
    child_code = "\n".join((
        "import subprocess, sys, time",
        "child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(60)'])",
        "open(sys.argv[1], 'w').write(str(child.pid))",
        "for i in range(25): print(f'output-line-{{i:02d}}', flush=True)",
        "time.sleep(60)",
    ))
    job = run_parallel.Job(
        name="timeout-self-test-sleeps-forever",
        cmd_template=[sys.executable, "-c", child_code, str(pid_file)],
        timeout_seconds=0.5,
    )
    result = run_parallel.run_single_job(
        job, Path(sys.executable), Path(sys.executable), scratch)
    tail = run_parallel.result_tail(result, 20)
    assert result.returncode == 124, result
    assert "timeout-self-test-sleeps-forever" in "\n".join(tail), tail
    assert "exceeded its 0.5s timeout" in "\n".join(tail), tail
    assert "output-line-24" in tail, tail
    child_pid = int(pid_file.read_text())
    deadline = time.monotonic() + 2.0
    while process_is_alive(child_pid) and time.monotonic() < deadline:
        time.sleep(0.02)
    assert not process_is_alive(child_pid), f"descendant {child_pid} survived timeout"
    print("run_parallel timeout self-test passed: named failure, 20-line tail, process tree killed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
