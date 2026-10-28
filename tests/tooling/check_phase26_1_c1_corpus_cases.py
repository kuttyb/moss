#!/usr/bin/env python3
"""Keep the 12 Phase 26.1 C1 corpus differences target-checked.

Run with the compiler under test as argv[1].
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
MARGO = ROOT / "margo"
CALC = ROOT / "examples/swarm/modules/calc_interpreter/repro/natural_full"
TOOLKIT = ROOT / "examples/swarm/modules/text_toolkit/toolkit"

CALC_FILES = [
    CALC / "tests" / f"{name}_test.moss"
    for name in ("calc", "env", "eval", "lexer", "report", "token")
]
TOOLKIT_FILES = [
    TOOLKIT / path for path in (
        "src/inventory.moss", "src/main.moss", "src/table.moss",
        "tests/inventory_test.moss", "tests/stats_test.moss",
        "tests/table_test.moss")
]


def invoke(args: list[object], cwd: Path, env: dict[str, str]):
    return subprocess.run([str(arg) for arg in args], cwd=cwd, env=env,
                          text=True, capture_output=True, timeout=180)


def file_check(path: Path, env: dict[str, str], expected_code: str | None,
               expected_line: int | None) -> set[str]:
    trace_env = env | {"MOSS_PHASE26_C1_SCOPE_TRACE": "1"}
    result = invoke([COMPILER, "check", path, "--json"], ROOT, trace_env)
    payload = json.loads(result.stdout)
    reachable = {
        line.split("|", 3)[3]
        for line in result.stderr.splitlines()
        if line.startswith("MOSS_C1_SCOPE_ID|") and len(line.split("|", 3)) == 4
    }
    if expected_code is None:
        assert result.returncode == 0 and payload["ok"], (path, payload)
        return reachable
    error = payload.get("error", {})
    assert result.returncode != 0 and not payload["ok"], (path, payload)
    assert error.get("code") == expected_code, (path, error)
    assert error.get("line") == expected_line, (path, error)
    assert error.get("semantic_identity") in reachable, (path, error, reachable)
    return reachable


def package_check(root: Path, env: dict[str, str], code: str,
                  source_suffix: str, line: int) -> tuple[str, str, int]:
    result = invoke([MARGO, "build", "--check"], root, env)
    output = result.stdout + result.stderr
    assert result.returncode != 0, (root, output)
    assert f'"code": "{code}"' in output, (root, output)
    assert source_suffix in output, (root, output)
    assert f'"line": {line}' in output, (root, output)
    return code, source_suffix, line


def main() -> int:
    env = dict(os.environ)
    env["MOSS"] = str(COMPILER)
    env["MARGO_HOME"] = str(ROOT / "tmp/phase26-1-c1-corpus-cases/margo-home")
    for key in ("MOSS_MODULE_PATH", "MOSS_FAST_DEBUG_SOURCE_ROOTS",
                "MOSS_SOURCE_ROOTS"):
        env.pop(key, None)

    for path in CALC_FILES:
        reachable = file_check(path, env, None, None)
        assert not any(identity.startswith("test:tests/parser_test.moss:")
                       for identity in reachable), (path, reachable)
    for path in TOOLKIT_FILES:
        if path.name == "main.moss":
            reachable = file_check(path, env, "UNSUPPORTED_STRING_ORDERING", 127)
            assert any(identity.endswith(":strings:fn:sorted_text")
                       for identity in reachable), (path, reachable)
        else:
            reachable = file_check(path, env, None, None)
            assert not any(identity.endswith(":strings:fn:min_text")
                           for identity in reachable), (path, reachable)

    # Build check must cover the full package targets. The calc diagnostic is
    # in the sibling parser test; toolkit's is in a shared src module.
    package_errors = {
        CALC: package_check(CALC, env, "TEST_ASSERTION_CONFIGURATION_ERROR",
                            "tests/parser_test.moss", 15),
        TOOLKIT: package_check(TOOLKIT, env, "UNSUPPORTED_STRING_ORDERING",
                               "src/strings.moss", 106),
    }

    calc_target = invoke([COMPILER, "test", "--check", "--json"], CALC, env)
    calc_target_error = json.loads(calc_target.stdout)["error"]
    assert calc_target_error["semantic_identity"].startswith(
        "test:tests/parser_test.moss:"), calc_target_error
    toolkit_target = invoke([MARGO, "build", "--check", "--moss-json"],
                            TOOLKIT, env)
    toolkit_target_error = json.loads(toolkit_target.stdout)["error"]
    assert toolkit_target_error["semantic_identity"].endswith(
        ":strings:fn:min_text"), toolkit_target_error

    # Guard the diagnostic origins independently of whichever package error is
    # encountered first during a full target check.
    parser_scope = file_check(CALC / "tests/parser_test.moss", env,
                              "TEST_ASSERTION_CONFIGURATION_ERROR", 15)
    assert calc_target_error["semantic_identity"] in parser_scope, parser_scope
    strings_scope = file_check(TOOLKIT / "src/strings.moss", env,
                               "UNSUPPORTED_STRING_ORDERING", 106)
    assert toolkit_target_error["semantic_identity"] in strings_scope, strings_scope
    for path in CALC_FILES + TOOLKIT_FILES:
        package = next(root for root in package_errors if path.is_relative_to(root))
        code, source, line = package_errors[package]
        print(f"Margo rejects {path.relative_to(ROOT)}: {code} at {source}:{line}")
    print("Phase 26.1 C1 corpus package checks passed (12 diff rows, 2 targets)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
