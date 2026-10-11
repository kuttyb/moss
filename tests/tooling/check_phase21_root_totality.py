#!/usr/bin/env python3
"""Every synchronous or submitted Root handles its complete typed raise set."""

import json
from pathlib import Path
import shutil
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1] if len(sys.argv) > 1 else repo / "moss").resolve()
fixtures = repo / "tests/tooling/fixtures"
out = repo / "tmp/phase21-root-totality"
out.mkdir(parents=True, exist_ok=True)
rustc = shutil.which("rustc")
assert rustc, "rustc is required for Root totality native tests"


def run(*args):
    return subprocess.run(
        [str(arg) for arg in args], cwd=repo, text=True,
        capture_output=True, timeout=120)


for name, ingress in (
    ("phase21_executor_unhandled_raise", "executor.invoke Root"),
    ("phase21_sync_root_lexical_recover", "synchronous Root message"),
    ("phase21_sync_root_grouped_recover", "synchronous Root message"),
):
    source = fixtures / f"{name}.moss"
    checked = run(compiler, "check", source, "--json")
    assert checked.returncode != 0, (name, checked.stdout, checked.stderr)
    error = json.loads(checked.stdout)["error"]
    assert error["code"] == "ROOT_UNHANDLED_RAISE", error
    assert ingress in error["message"], error
    assert "enum:WorkError:Failed" in error["message"], error
    lowered = run(compiler, source, "-o", out / f"{name}.rs")
    assert lowered.returncode != 0, (name, lowered.stdout, lowered.stderr)

for name, expected in (
    ("phase21_executor_root_total", "2\n"),
    ("phase21_sync_root_total", "7\n"),
):
    source = fixtures / f"{name}.moss"
    checked = run(compiler, "check", source, "--json")
    assert checked.returncode == 0, (name, checked.stdout, checked.stderr)
    for profile, moss_flag, rust_flag in (
        ("debug", "-O0", "-Copt-level=0"),
        ("optimized", "-Oshared-memory", "-Copt-level=3"),
    ):
        rust = out / f"{name}-{profile}.rs"
        executable = out / f"{name}-{profile}"
        lowered = run(compiler, source, moss_flag, "-o", rust)
        assert lowered.returncode == 0, (name, lowered.stdout, lowered.stderr)
        compiled = run(rustc, "--edition=2021", "-Dwarnings", rust_flag,
                       rust, "-o", executable)
        assert compiled.returncode == 0, (name, compiled.stdout, compiled.stderr)
        executed = run(executable)
        assert executed.returncode == 0, (name, executed.stdout, executed.stderr)
        assert executed.stdout == expected, (name, profile, executed.stdout)
    interpreted = run(compiler, "run", "--interp", source)
    assert interpreted.returncode == 0, (name, interpreted.stdout,
                                         interpreted.stderr)
    assert interpreted.stdout == expected, (name, interpreted.stdout)

print("Phase 21 synchronous and Executor Root totality checks passed")
