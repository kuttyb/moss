#!/usr/bin/env python3
"""Ratchet accepted standalone Moss sources against rustc compilation."""
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from phase27_allowlist_paths import missing_fixture_paths, self_test as allowlist_self_test

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
ALLOW = ROOT / "tests/phase27/conformance/accepts_rustc_allowlist.json"


def run(args, cwd=ROOT, env=None):
    return subprocess.run(args, cwd=cwd, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def ratchet(observed, allowed):
    unexpected = set(observed) - allowed
    fixed = allowed - set(observed)
    if unexpected or fixed:
        raise AssertionError(f"unexpected={sorted(unexpected)} fixed_allowlist={sorted(fixed)}")


def self_test():
    allowlist_self_test()
    ratchet({"seed": "fail"}, {"seed"})
    for observed, allowed in (({"new": "fail"}, set()), ({}, {"fixed"})):
        try:
            ratchet(observed, allowed)
        except AssertionError:
            continue
        raise AssertionError("ratchet accepted a new failure or stale allowlist entry")


def rustc_fails(source, work):
    work.mkdir(parents=True, exist_ok=True)
    emitted = work / "emitted.rs"
    rust = run([str(COMPILER), str(source), "-o", str(emitted)])
    if rust.returncode:
        return "emitter:" + (rust.stderr.strip().splitlines()[-1] if rust.stderr else "failed")
    compiled = run(["rustc", "--edition=2021", "--crate-type=lib", str(emitted), "-o", str(work / "libprobe.rlib")])
    return ("rustc:" + (compiled.stderr.strip().splitlines()[-1] if compiled.stderr else "failed")) if compiled.returncode else None


def main():
    self_test()
    allow_data = json.loads(ALLOW.read_text(encoding="utf-8"))
    missing = missing_fixture_paths(ROOT, allow_data["cases"])
    if missing:
        raise SystemExit("allowlist names missing fixtures: " + ", ".join(missing))
    allowed_cases = {entry["id"]: entry for entry in allow_data["cases"]}
    work = ROOT / "tmp" / "phase27-accepts-rustc" / str(time.time_ns())
    work.mkdir(parents=True)
    observed = {}

    # Check every shallow standalone source root. Package trees are exercised
    # through native Margo tests below and are not valid when detached from
    # their package's module graph.
    candidates = []
    seeded_paths = {entry["path"] for entry in allowed_cases.values()}
    tracked = run(["git", "ls-tree", "-r", "--name-only", "HEAD"])
    if tracked.returncode:
        raise SystemExit("could not enumerate tracked Moss sources")
    for relative in tracked.stdout.splitlines():
        if not relative.endswith(".moss") or len(Path(relative).parts) > 3:
            continue
        if relative in seeded_paths:
            continue
        path = ROOT / relative
        if any((parent / "Moss.toml").exists()
               for parent in path.parents if parent != ROOT):
            continue
        candidates.append(path)
    accepted_count = 0
    for number, source in enumerate(sorted(set(candidates))):
        checked = run([str(COMPILER), "check", str(source), "--json"])
        try:
            accepted = json.loads(checked.stdout).get("ok") is True
        except json.JSONDecodeError:
            accepted = False
        if not accepted:
            continue
        accepted_count += 1
        result = rustc_fails(source, work / f"source-{number}")
        if result:
            observed[source.relative_to(ROOT).as_posix()] = result

    for case_id, entry in allowed_cases.items():
        source = ROOT / entry["path"]
        if not source.exists():
            continue  # dormant seeded fixture; its path is still ratcheted below
        checked = run([str(COMPILER), "check", str(source), "--json"])
        try:
            accepted = json.loads(checked.stdout).get("ok") is True
        except json.JSONDecodeError:
            accepted = False
        if not accepted:
            continue
        result = rustc_fails(source, work / ("allow-" + str(len(observed))))
        if result:
            observed[case_id] = result
    active_allowed = {case_id for case_id, entry in allowed_cases.items()
                      if (ROOT / entry["path"]).exists()}
    # Legacy standalone candidate errors are keyed by source path; known seeds by ID.
    observed_ids = {key for key in observed if key in allowed_cases}
    observed_extra = {key: value for key, value in observed.items() if key not in allowed_cases}
    if observed_extra:
        raise SystemExit("accepted sources fail rustc outside the seeded allowlist: " +
                         json.dumps(observed_extra, sort_keys=True))
    ratchet({key: observed[key] for key in observed_ids}, active_allowed)

    # A native margo test project is part of this gate, not just corpus checking.
    package = work / "margo-smoke"
    (package / "src").mkdir(parents=True)
    (package / "tests").mkdir()
    (package / "Moss.toml").write_text('[package]\nname="phase27_gate_smoke"\nversion="0.1.0"\n\n[build]\nsource="src"\n')
    (package / "src/main.moss").write_text("fn main():\n  echo 1\n")
    (package / "tests/smoke.moss").write_text(
        'test "accepted source runs natively":\n  assertEqual(1, 1)\n')
    env = os.environ.copy(); env["MOSS"] = str(COMPILER)
    native = run([str(ROOT / "margo"), "test"], cwd=package, env=env)
    if native.returncode:
        raise SystemExit("native margo test gate failed:\n" + native.stdout + native.stderr)
    print(f"accepts-vs-rustc passed: {accepted_count} accepted standalone roots; "
          f"native margo test passed; active seeds={len(active_allowed)}")


if __name__ == "__main__":
    main()
