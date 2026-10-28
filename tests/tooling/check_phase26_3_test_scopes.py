#!/usr/bin/env python3
"""Exercise approved test-file scopes in native, Fast Debug, and moss check."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
MARGO = ROOT / "margo"

CASES = {
    "f1": {
        "src/main.moss": "module fixture\nfn source_value() -> Int:\n  return 42\n",
        "tests/a.moss": """module fixture
fn twice(value: Int) -> Int:
  return value * 2
test "same file helper reused":
  assertEqual(twice(21), source_value())
  assertEqual(twice(2), 4)
""",
    },
    "f2": {
        "src/main.moss": "module fixture\nfn source_value() -> Int:\n  return 42\n",
        "tests/a.moss": "module fixture\nfn hidden_helper() -> Int:\n  return 42\n",
        "tests/b.moss": """module fixture
test "cross file helper rejected":
  assertEqual(hidden_helper(), source_value())
""",
    },
    "f3": {
        "src/main.moss": "module fixture\nfn source_value() -> Int:\n  return 42\n",
        "tests/a.moss": """module fixture
fn setup() -> Int:
  return 20
test "first local setup":
  assertEqual(setup(), 20)
""",
        "tests/b.moss": """module fixture
fn setup() -> Int:
  return 22
test "second local setup":
  assertEqual(setup(), source_value() - 20)
""",
    },
    "f4": {
        "src/main.moss": "module fixture\nfn source_value() -> Int:\n  return 42\n",
        "tests/support/support.moss": """module test_support
export fn shared_answer() -> Int:
  return 42
""",
        "tests/a.moss": """module test_a
import test_support
test "shared module first consumer":
  assertEqual(test_support.shared_answer(), 42)
""",
        "tests/b.moss": """module test_b
import test_support
test "shared module second consumer":
  assertEqual(test_support.shared_answer(), 42)
""",
    },
    "f5": {
        "src/main.moss": """module fixture
fn private_source_value() -> Int:
  return 42
""",
        "tests/a.moss": """module fixture
test "white box source access":
  assertEqual(private_source_value(), 42)
""",
    },
    "f6": {
        "src/main.moss": """module fixture
fn collision() -> Int:
  return 1
""",
        "tests/a.moss": """module fixture
fn collision() -> Int:
  return 2
test "source collision rejected":
  assertEqual(collision(), 2)
""",
    },
}


def invoke(command: list[str], cwd: Path, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          timeout=180)


def json_result(proc: subprocess.CompletedProcess[str]) -> dict:
    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError as error:
        raise AssertionError(f"expected JSON output, got {proc.stdout!r} {proc.stderr!r}") from error


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="phase26-3-scopes-", dir=ROOT / "tmp") as raw:
        base = Path(raw)
        for case, files in CASES.items():
            package = base / case
            for relative, content in files.items():
                path = package / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8")
            (package / "Moss.toml").write_text(
                f'[package]\nname = "phase26-3-{case}"\nversion = "0.1.0"\n'
                '[build]\nsource = "src"\n', encoding="utf-8")
            env = os.environ.copy()
            env["MOSS"] = str(MOSS)
            env.setdefault("CARGO_TARGET_DIR", str(base / "cargo-target"))
            expected_failure = case in {"f2", "f6"}
            for mode, command in (
                ("native", [str(MARGO), "test", "--json"]),
                ("margo-interp", [str(MARGO), "test", "--interp", "--json"]),
                ("moss-interp", [str(MOSS), "test", "--interp", "--json", str(package)]),
            ):
                proc = invoke(command, package, env)
                data = json_result(proc)
                if expected_failure:
                    expected_code = "UNKNOWN_SYMBOL_OR_TYPE" if case == "f2" else "DUPLICATE_SYMBOL"
                    assert proc.returncode != 0 and not data.get("ok"), (case, mode, proc.returncode, data)
                    error = data["error"]
                    assert error["code"] == expected_code, (case, mode, error)
                    if case == "f2":
                        assert "declared in tests/a.moss; move it into a test module and import it" in error["message"], (mode, error)
                else:
                    assert proc.returncode == 0 and data.get("ok"), (case, mode, proc.returncode, data)
                    summary = data["result"]["summary"]
                    assert summary["failed"] == 0, (case, mode, summary)
                    assert summary["total"] == sum(
                        len(re.findall(r'^\s*test\s+"', content, re.MULTILINE))
                        for relative, content in files.items()
                        if relative.startswith(("src/", "tests/"))
                    ), (case, mode, summary)
            if case == "f2":
                proc = invoke([str(MOSS), "check", str(package / "tests/b.moss"), "--json"], package, env)
                data = json_result(proc)
                assert proc.returncode != 0 and data["error"]["code"] == "UNKNOWN_SYMBOL_OR_TYPE"
                assert "declared in tests/a.moss; move it into a test module and import it" in data["error"]["message"]
            if case == "f4":
                build = invoke([str(MARGO), "build"], package, env)
                assert build.returncode == 0, (build.stdout, build.stderr)
                assert not list((package / "build/debug").glob("*test_support.mossi")), "test module leaked into package interfaces"
            print(f"{case}: native and --interp assertions passed")


if __name__ == "__main__":
    main()
