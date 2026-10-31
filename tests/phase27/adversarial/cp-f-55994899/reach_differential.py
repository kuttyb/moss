#!/usr/bin/env python3
"""Compare Phase 27.2 self off/shadow reach with 27.2-off/27.3-shadow."""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def trace(compiler: Path, mode: str, corpus: Path, source: str, timeout: int) -> dict:
    env = os.environ.copy()
    env["MOSS_EST"] = mode
    env["MOSS_PHASE26_C1_SCOPE_TRACE"] = "1"
    try:
        result = subprocess.run([str(compiler), "check", source, "--json"],
                                cwd=corpus, env=env, text=True,
                                capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired as exc:
        return {"kind": "timeout", "timeout_seconds": timeout,
                "stdout": exc.stdout or "", "stderr": exc.stderr or ""}
    try:
        response = json.loads(result.stdout)
        error = response.get("error") or {}
        verdict = (response.get("ok"), error.get("code"), error.get("line"))
    except (json.JSONDecodeError, AttributeError):
        verdict = ("NOJSON", result.returncode, None)
        response = None
    edges, checked = set(), set()
    prefix = str(corpus) + "/"
    complete = False
    for line in result.stderr.splitlines():
        if line.startswith("MOSS_C1_SCOPE_TRACE_COMPLETE|"):
            complete = True
        elif line.startswith("MOSS_C1_SCOPE_EDGE|"):
            _, owner, kind, target = line.split("|", 3)
            edges.add((owner.replace(prefix, ""), kind, target))
        elif line.startswith("MOSS_C1_SCOPE_ID|"):
            _, kind, source_file, identity = line.split("|", 3)
            checked.add((kind, source_file.replace(prefix, ""), identity))
    return {"kind": "json" if response is not None else "no_json",
            "returncode": result.returncode, "verdict": verdict,
            "trace_complete": complete, "edges": sorted(edges),
            "checked": sorted(checked), "stderr_tail": result.stderr[-4000:]}


def compare(source: str, off: dict, shadow: dict) -> dict:
    return {"source": source,
            "off": off, "shadow": shadow,
            "edge_missing": sorted(set(map(tuple, off.get("edges", []))) -
                                    set(map(tuple, shadow.get("edges", [])))),
            "edge_added": sorted(set(map(tuple, shadow.get("edges", []))) -
                                 set(map(tuple, off.get("edges", [])))),
            "checked_missing": sorted(set(map(tuple, off.get("checked", []))) -
                                       set(map(tuple, shadow.get("checked", [])))),
            "checked_added": sorted(set(map(tuple, shadow.get("checked", []))) -
                                    set(map(tuple, off.get("checked", [])))),
            "verdict_diff": off.get("verdict") != shadow.get("verdict"),
            "trace_diff": off.get("trace_complete") != shadow.get("trace_complete")}


def is_failure(row: dict) -> bool:
    return bool(row["edge_missing"] or row["edge_added"] or row["checked_missing"] or
                row["checked_added"] or row["verdict_diff"] or row["trace_diff"] or
                row["off"].get("kind") != "json" or row["shadow"].get("kind") != "json")


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
    baseline, candidate = args.baseline_compiler.resolve(), args.candidate_compiler.resolve()
    listing = subprocess.check_output(
        ["git", "ls-tree", "-r", "--name-only", args.candidate_revision],
        cwd=root, text=True).splitlines()
    sources = sorted(path for path in listing if path.endswith(".moss"))
    inputs = sorted(path for path in listing if path.endswith(".moss") or
                    (Path(path).name in ("Moss.toml", "Moss.lock") and
                     not path.startswith(("docs/phase27/adversarial/",
                                          "tests/phase27/adversarial/cp-",
                                          "tests/phase27/adversarial/perf-"))))
    corpus = root / "tmp" / "phase27-3" / "cp-f-55994899" / "reach-corpus"
    shutil.rmtree(corpus, ignore_errors=True)
    corpus.mkdir(parents=True)
    for relative in inputs:
        source_path = root / relative
        if not source_path.is_file():
            raise SystemExit(f"candidate corpus input is missing from the frozen worktree: {relative}")
        target_path = corpus / relative
        target_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_path, target_path)
    tasks = [(source, label, compiler, mode) for source in sources for label, compiler, mode in (
        ("baseline_off", baseline, "off"),
        ("baseline_shadow", baseline, "shadow"),
        ("candidate_shadow", candidate, "shadow"))]

    def run(task):
        source, label, compiler, mode = task
        return source, label, trace(compiler, mode, corpus, source, args.timeout)

    observed: dict[str, dict] = {source: {} for source in sources}
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for source, label, result in pool.map(run, tasks):
            observed[source][label] = result
    baseline_rows, candidate_rows = [], []
    delta_rows, inherited_rows = [], []
    incomplete = []
    for source in sources:
        row = observed[source]
        base = compare(source, row["baseline_off"], row["baseline_shadow"])
        cand = compare(source, row["baseline_off"], row["candidate_shadow"])
        baseline_rows.append(base)
        candidate_rows.append(cand)
        if any(row[label].get("kind") != "json" for label in
               ("baseline_off", "baseline_shadow", "candidate_shadow")):
            incomplete.append({"source": source, **row})
        if is_failure(cand) and not is_failure(base):
            delta_rows.append(cand)
        elif is_failure(base) and is_failure(cand):
            inherited_rows.append({"source": source,
                                   "baseline_failure": is_failure(base),
                                   "candidate_failure": is_failure(cand),
                                   "candidate_row_equals_baseline_row": base == cand,
                                   "baseline_row": base, "candidate_row": cand})
        elif is_failure(base) and not is_failure(cand):
            delta_rows.append({"source": source, "classification": "candidate shadow fixed baseline self delta",
                               "baseline_row": base, "candidate_row": cand})
    baseline_failures = [row for row in baseline_rows if is_failure(row)]
    candidate_failures = [row for row in candidate_rows if is_failure(row)]
    report = {
        "candidate_revision": args.candidate_revision,
        "candidate_source_count": len(sources),
        "baseline_compiler_sha256": hashlib.sha256(baseline.read_bytes()).hexdigest(),
        "candidate_compiler_sha256": hashlib.sha256(candidate.read_bytes()).hexdigest(),
        "comparison_contract": {
            "corpus": "tracked candidate `.moss` sources plus Moss.toml/Moss.lock copied from the frozen worktree; review docs/evidence excluded",
            "baseline_self": "phase27-2-m1 compiler off vs shadow",
            "candidate_differential": "phase27-2-m1 compiler off vs frozen candidate shadow",
            "reach_edge_key": ["owner source", "kind", "target identity"],
            "checked_declaration_key": ["kind", "relative source", "declaration identity"],
            "trace_flag": "MOSS_PHASE26_C1_SCOPE_TRACE=1",
            "timeout_seconds": args.timeout,
        },
        "baseline_self_failure_row_count": len(baseline_failures),
        "candidate_differential_failure_row_count": len(candidate_failures),
        "candidate_only_delta_rows": delta_rows,
        "inherited_rows": inherited_rows,
        "incomplete_runs": incomplete,
        "baseline_self_rows": baseline_rows,
        "candidate_differential_rows": candidate_rows,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"sources={len(sources)} baseline-self-rows={len(baseline_failures)} "
          f"candidate-rows={len(candidate_failures)} candidate-only-deltas={len(delta_rows)} "
          f"incomplete={len(incomplete)}")
    print(f"report={args.out}")
    return 1 if delta_rows or incomplete else 0


if __name__ == "__main__":
    raise SystemExit(main())
