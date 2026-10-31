#!/usr/bin/env python3
"""Compare baseline/candidate Moss verdicts and candidate off/shadow behavior."""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess


def tracked_sources(revision: str, cwd: Path) -> list[str]:
    paths = subprocess.check_output(
        ["git", "ls-tree", "-r", "--name-only", revision], cwd=cwd,
        text=True).splitlines()
    return [path for path in paths if path.endswith(".moss")]


def invoke(compiler: Path, mode: str, root: Path, source: str,
           timeout: int) -> dict:
    env = os.environ.copy()
    env["MOSS_EST"] = mode
    try:
        result = subprocess.run(
            [str(compiler), "check", source, "--json"], cwd=root, env=env,
            text=True, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        return {"kind": "timeout", "timeout_seconds": timeout,
                "stdout": exc.stdout or "", "stderr": exc.stderr or ""}
    try:
        response = json.loads(result.stdout)
    except json.JSONDecodeError:
        return {"kind": "no_json", "returncode": result.returncode,
                "stdout": result.stdout, "stderr": result.stderr}
    return {"kind": "json", "returncode": result.returncode,
            "response": response}


def signature(observation: dict) -> dict:
    if observation["kind"] != "json":
        return {key: observation.get(key) for key in
                ("kind", "returncode", "timeout_seconds", "stdout", "stderr")}
    response = observation["response"]
    return {"kind": "json", "returncode": observation["returncode"],
            "ok": response.get("ok"), "error": response.get("error")}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("candidate_revision")
    parser.add_argument("candidate_root", type=Path)
    parser.add_argument("baseline_compiler", type=Path)
    parser.add_argument("candidate_compiler", type=Path)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 4, 8))
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    root = args.candidate_root.resolve()
    baseline = args.baseline_compiler.resolve()
    candidate = args.candidate_compiler.resolve()
    sources = tracked_sources(args.candidate_revision, root)
    tasks = [(source, label, compiler, mode) for source in sources
             for label, compiler, mode in (
                 ("baseline_off", baseline, "off"),
                 ("candidate_off", candidate, "off"),
                 ("candidate_shadow", candidate, "shadow"))]

    def run(task):
        source, label, compiler, mode = task
        return source, label, invoke(compiler, mode, root, source, args.timeout)

    by_source: dict[str, dict] = {source: {} for source in sources}
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for source, label, result in pool.map(run, tasks):
            by_source[source][label] = result

    baseline_diffs, shadow_diffs, incomplete = [], [], []
    accepted = {"baseline_off": [], "candidate_off": [],
                "candidate_shadow": []}
    for source in sources:
        row = by_source[source]
        b, c, s = (signature(row[name]) for name in
                   ("baseline_off", "candidate_off", "candidate_shadow"))
        for label, result in zip(("baseline_off", "candidate_off",
                                  "candidate_shadow"), (b, c, s)):
            if result.get("kind") == "json" and result.get("ok") is True:
                accepted[label].append(source)
        if row["baseline_off"]["kind"] != "json" or row["candidate_off"]["kind"] != "json" or row["candidate_shadow"]["kind"] != "json":
            incomplete.append({"source": source,
                               "baseline_off": row["baseline_off"],
                               "candidate_off": row["candidate_off"],
                               "candidate_shadow": row["candidate_shadow"]})
        if b != c:
            baseline_diffs.append({"source": source, "baseline_off": b,
                                   "candidate_off": c})
        if c != s:
            shadow_diffs.append({"source": source, "candidate_off": c,
                                 "candidate_shadow": s})
    report = {
        "candidate_revision": args.candidate_revision,
        "baseline_compiler": str(baseline),
        "baseline_compiler_sha256": hashlib.sha256(baseline.read_bytes()).hexdigest(),
        "candidate_compiler": str(candidate),
        "candidate_compiler_sha256": hashlib.sha256(candidate.read_bytes()).hexdigest(),
        "source_count": len(sources),
        "comparison_contract": {
            "source_tree": "tracked candidate revision, same candidate-root path for all compilers",
            "baseline_mode": "MOSS_EST=off",
            "candidate_modes": ["MOSS_EST=off", "MOSS_EST=shadow"],
            "diagnostic_comparison": "full structured error object and verdict; JSON responses preserve identical absolute source paths",
            "timeout_seconds": args.timeout,
        },
        "baseline_candidate_off_differences": baseline_diffs,
        "candidate_off_shadow_differences": shadow_diffs,
        "accepted_sources": accepted,
        "incomplete_runs": incomplete,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"sources={len(sources)} baseline-candidate-off-diffs={len(baseline_diffs)} "
          f"candidate-off-shadow-diffs={len(shadow_diffs)} incomplete={len(incomplete)}")
    print(f"report={args.out}")
    return 1 if baseline_diffs or shadow_diffs or incomplete else 0


if __name__ == "__main__":
    raise SystemExit(main())
