#!/usr/bin/env python3
"""Check checker authority for None returned from an Option method."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/phase28/r7_010/team_a/return_none_fact.moss"
SHADOWED_FIXTURE = ROOT / "tests/phase28/r7_010/team_a/return_shadowed_none.moss"
OVERLOADED_FIXTURE = ROOT / "tests/phase28/r7_010/team_a/return_none_overloads.moss"


def run(command: list[str], env: dict[str, str]):
    return subprocess.run(command, cwd=ROOT, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          check=False)


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_phase28_return_none_authority.py COMPILER")
    compiler = str(Path(sys.argv[1]).resolve())
    base_env = os.environ.copy()
    env = {**base_env, "MOSS_TEST_DUMP_PHASE28_AUTHORITY_FACTS": "1",
           "MOSS_TEST_VALIDATE_PHASE28_AUTHORITY_FACTS": "1"}
    checked = run([compiler, "check", "--json", str(FIXTURE)], env)
    response = json.loads(checked.stdout)
    if checked.returncode or not response.get("ok"):
        raise AssertionError(f"None return checker rejected fixture: {response!r}\n"
                             f"{checked.stderr}")
    if "PHASE28_AUTHORITY_FACTS" not in checked.stderr or "return=2" not in checked.stderr:
        raise AssertionError(f"expected both checked return facts, got {checked.stderr!r}")

    for hook in ("MOSS_TEST_DROP_RETURN_VALUE_FACT",
                 "MOSS_TEST_PERTURB_RETURN_VALUE_FACT",
                 "MOSS_TEST_PERTURB_RETURN_VALUE_TYPE_ORIGIN"):
        failed = run([compiler, "check", "--json", str(FIXTURE)],
                     {**env, hook: "1"})
        result = json.loads(failed.stdout)
        if result.get("ok") or result.get("error", {}).get("code") != \
                "MOSS_INTERNAL_OR_IO_ERROR":
            raise AssertionError(f"{hook} did not fail closed: {result!r}\n"
                                 f"{failed.stderr}")

    for hook in ("MOSS_TEST_DROP_DECLARED_NONE_METHOD_RETURN_EST_FACT",
                 "MOSS_TEST_PERTURB_DECLARED_NONE_METHOD_RETURN_EST_FACT"):
        failed = run([compiler, "check", "--json", str(FIXTURE)],
                     {**env, hook: "1",
                      "MOSS_TEST_SELECT_DECLARED_NONE_METHOD_RETURN":
                          "Counter.next"})
        result = json.loads(failed.stdout)
        if result.get("ok") or result.get("error", {}).get("code") not in {
                "MOSS_INTERNAL_INVARIANT", "MOSS_INTERNAL_OR_IO_ERROR"}:
            raise AssertionError(f"{hook} did not fail closed: {result!r}\n"
                                 f"{failed.stderr}")

    shadowed = run([compiler, "check", "--json", str(SHADOWED_FIXTURE)], env)
    shadow_response = json.loads(shadowed.stdout)
    if shadowed.returncode or not shadow_response.get("ok"):
        raise AssertionError(f"shadowed None binding was rejected: {shadow_response!r}\n"
                             f"{shadowed.stderr}")
    shadow_rows = re.findall(r"PHASE28_RETURN_FACT ([^\n]+)", shadowed.stderr)
    if not any(re.search(r"binding=\S+", row) and "type_origin=0" in row
               for row in shadow_rows):
        raise AssertionError(f"shadowed None did not use checked binding authority: "
                             f"{shadow_rows!r}")

    overloaded = run([compiler, "check", "--json", str(OVERLOADED_FIXTURE)], env)
    overloaded_response = json.loads(overloaded.stdout)
    if overloaded.returncode or not overloaded_response.get("ok"):
        raise AssertionError(f"overloaded None methods were rejected: "
                             f"{overloaded_response!r}\n{overloaded.stderr}")
    overloaded_rows = re.findall(
        r"PHASE28_RETURN_FACT ([^\n]*context=method:Choice\.next[^\n]+)",
        overloaded.stderr)
    int_rows = [row for row in overloaded_rows if "type=option[int]" in row]
    string_rows = [row for row in overloaded_rows if "type=option[string]" in row]
    if len(int_rows) != 1 or len(string_rows) != 1:
        raise AssertionError(f"overloads did not retain their exact return types: "
                             f"{overloaded_rows!r}")
    def est_context(row: str) -> str:
        match = re.search(r"est_context=(\S+)", row)
        return match.group(1) if match else ""
    int_context, string_context = est_context(int_rows[0]), est_context(string_rows[0])
    if not int_context.startswith("method-return-v1:") or \
            not string_context.startswith("method-return-v1:") or \
            int_context == string_context:
        raise AssertionError(f"overload ReturnValue contexts are missing or collide: "
                             f"{int_context!r}, {string_context!r}")

    for hook, value in (
            ("MOSS_TEST_DROP_RETURN_VALUE_METHOD_EST_FACT", "(string)"),
            ("MOSS_TEST_PERTURB_RETURN_VALUE_METHOD_CONTEXT", "(string)"),
            ("MOSS_TEST_PERTURB_RETURN_VALUE_METHOD_EST_FACT", "(string)")):
        failed = run([compiler, "check", "--json", str(OVERLOADED_FIXTURE)],
                     {**env, hook: value})
        result = json.loads(failed.stdout)
        if result.get("ok") or result.get("error", {}).get("code") not in {
                "MOSS_INTERNAL_INVARIANT", "MOSS_INTERNAL_OR_IO_ERROR"}:
            raise AssertionError(f"{hook} did not fail closed for one overload: "
                                 f"{result!r}\n{failed.stderr}")

    interpreted_overloads = run(
        [compiler, "run", "--interp", str(OVERLOADED_FIXTURE)], base_env)
    if interpreted_overloads.returncode or interpreted_overloads.stdout != \
            "empty-int\nempty-string\n":
        raise AssertionError("overloaded None Fast Debug mismatch: "
                             f"{interpreted_overloads.returncode}, "
                             f"{interpreted_overloads.stdout!r}, "
                             f"{interpreted_overloads.stderr!r}")
    interpreted_shadow = run([compiler, "run", "--interp", str(SHADOWED_FIXTURE)],
                             base_env)
    if interpreted_shadow.returncode or interpreted_shadow.stdout != "7\n":
        raise AssertionError("shadowed None runtime mismatch: "
                             f"{interpreted_shadow.returncode}, "
                             f"{interpreted_shadow.stdout!r}, "
                             f"{interpreted_shadow.stderr!r}")

    native_project = ROOT / "tmp" / "phase28-return-none-native"
    shutil.rmtree(native_project, ignore_errors=True)
    (native_project / "src").mkdir(parents=True)
    (native_project / "Moss.toml").write_text(
        '[package]\nname = "phase28_return_none_native"\nversion = "0.1.0"\n'
        '\n[build]\nsource = "src"\n', encoding="utf-8")
    (native_project / "src" / "main.moss").write_text(
        FIXTURE.read_text(encoding="utf-8"), encoding="utf-8")
    native_env = {**base_env, "MOSS": compiler}
    native = subprocess.run([str(ROOT / "margo"), "run", "--json"],
                            cwd=native_project, env=native_env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            check=False)
    if native.returncode or native.stdout != "empty\n":
        raise AssertionError("declared None native method output mismatch: "
                             f"{native.returncode}, {native.stdout!r}, "
                             f"{native.stderr!r}")
    shutil.rmtree(native_project / "build", ignore_errors=True)
    wrong_context_env = {
        **native_env,
        "MOSS_TEST_PERTURB_DECLARED_NONE_METHOD_RETURN_EST_CONTEXT":
            "Counter.next",
    }
    wrong_context = subprocess.run(
        [str(ROOT / "margo"), "run", "--json"], cwd=native_project,
        env=wrong_context_env, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False)
    wrong_context_output = wrong_context.stdout + wrong_context.stderr
    if wrong_context.returncode == 0 or "MOSS_INTERNAL_INVARIANT" not in \
            wrong_context_output:
        raise AssertionError("native method ReturnValue wrong-context mutation did not fail closed: "
                             f"{wrong_context.returncode}, {wrong_context.stdout!r}, "
                             f"{wrong_context.stderr!r}")
    print("Phase 28 None return-value authority: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
