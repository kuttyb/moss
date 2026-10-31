#!/usr/bin/env python3
"""Phase 27.5.1 checked call edge, join, pattern, and Char regressions."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
FIXTURES = ROOT / "tests/tooling/fixtures/phase27_5_1"


def invoke(args, *, cwd=ROOT, extra_env=None):
    env = os.environ.copy()
    env["MOSS"] = str(COMPILER)
    if extra_env:
        env.update(extra_env)
    return subprocess.run(args, cwd=cwd, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def require_ok(result, label):
    if result.returncode:
        raise SystemExit(f"{label} failed:\n{result.stdout}{result.stderr}")


def accepted(source):
    checked = invoke([str(COMPILER), "check", str(source), "--json"])
    require_ok(checked, f"check {source.name}")
    if not json.loads(checked.stdout).get("ok"):
        raise SystemExit(f"checker rejected {source}: {checked.stdout}")


def parity(source, work):
    accepted(source)
    fast = invoke([str(COMPILER), "run", "--interp", str(source)])
    require_ok(fast, f"Fast Debug {source.name}")
    rust = work / f"{source.stem}.rs"
    emitted = invoke([str(COMPILER), str(source), "-o", str(rust)])
    require_ok(emitted, f"emit {source.name}")
    binary = work / source.stem
    compiled = invoke(["rustc", "--edition=2021", str(rust), "-o", str(binary)])
    require_ok(compiled, f"rustc {source.name}")
    native = invoke([str(binary)])
    require_ok(native, f"native {source.name}")
    if native.stdout != fast.stdout:
        raise SystemExit(f"Fast Debug/native mismatch for {source.name}: "
                         f"native={native.stdout!r}; fast={fast.stdout!r}")
    return native.stdout


def must_reject(source):
    checked = invoke([str(COMPILER), "check", str(source), "--json"])
    try:
        result = json.loads(checked.stdout)
    except Exception as error:
        raise SystemExit(f"check rejection {source.name} returned invalid JSON: "
                         f"{checked.stdout}{checked.stderr}") from error
    if result.get("ok"):
        raise SystemExit(f"checker unexpectedly accepted {source.name}")


def main():
    work = ROOT / "tmp" / "phase27-5-1-targeted" / str(time.time_ns())
    work.mkdir(parents=True)

    parity(FIXTURES / "call_edges.moss", work)
    for name in ("join_if.moss", "join_match.moss", "join_try.moss",
                 "join_recover_binding.moss", "pattern_local_payload.moss",
                 "pattern_parenthesized.moss", "default_char.moss"):
        parity(FIXTURES / name, work)

    # The imported alias resolves to canonical pkg__Err in the checked pattern.
    package = work / "imported_pattern"
    shutil.copytree(FIXTURES / "imported_pattern", package)
    built = invoke([str(ROOT / "margo"), "build"], cwd=package)
    require_ok(built, "margo build imported pattern")
    native = invoke([str(ROOT / "margo"), "run"], cwd=package)
    fast = invoke([str(ROOT / "margo"), "debug"], cwd=package)
    require_ok(native, "native imported pattern")
    require_ok(fast, "Fast Debug imported pattern")
    if native.stdout != fast.stdout:
        raise SystemExit(f"imported pattern mismatch: {native.stdout!r} vs {fast.stdout!r}")

    must_reject(FIXTURES / "pattern_named_payload_rejected.moss")
    must_reject(FIXTURES / "join_nonfallthrough_rejected.moss")
    must_reject(FIXTURES / "raise_char_rejected.moss")

    count = invoke([str(COMPILER), str(FIXTURES / "call_edges.moss"),
                    "-o", str(work / "count.rs")],
                   extra_env={"MOSS_TEST_COUNT_FACT": "g1"})
    require_ok(count, "ambiguous checked call edge counter")
    hits = re.findall(r"MOSS_TEST_FACT_COUNTER\|g1\|selection=(\w+)\|line=(\d+)\|context=([^|]+)\|callee=([^\s]+)",
                      count.stderr)
    ambiguous = [hit for hit in hits if hit[0] == "ambiguous"]
    unique = [hit for hit in hits if hit[0] == "unique"]
    if not ambiguous or any(hit[1:] != ("6", "main", "identity")
                            for hit in ambiguous) or not unique or any(
                                hit[1:] != ("5", "main", "identity") for hit in unique):
        raise SystemExit(f"unexpected G1 selection counters: {hits!r}\n{count.stderr}")

    dropped_env = os.environ.copy()
    if dropped_env.get("MOSS_TEST_DISABLE_GUARD_HOOKS") == "1":
        dropped_env.pop("MOSS_TEST_DROP_FACT", None)
    else:
        dropped_env["MOSS_TEST_DROP_FACT"] = "g1"
    dropped = subprocess.run([str(COMPILER), str(FIXTURES / "call_edges.moss"),
                              "-o", str(work / "dropped.rs")], cwd=ROOT,
                             env=dropped_env, text=True, capture_output=True)
    if dropped.returncode == 0 or "MOSS_INTERNAL_INVARIANT missing checked call edge" not in dropped.stderr:
        raise SystemExit(f"G1 dropped edge did not fail closed: {dropped.stdout}{dropped.stderr}")
    print("Phase 27.5.1 targeted checked facts and Fast Debug fixtures passed")


if __name__ == "__main__":
    main()
