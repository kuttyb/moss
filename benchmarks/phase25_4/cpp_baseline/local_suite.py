#!/usr/bin/env python3
"""Run a repeatable local B1 timing suite, optionally against a Git baseline.

Artifacts are isolated under tmp/compiler-perf/<run-id>; certified B1 results
in benchmarks/phase25_4/cpp_baseline/results are never overwritten.
"""
from __future__ import annotations

import csv
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
RUNNER = ROOT / "benchmarks/phase25_4/cpp_baseline/run.py"
TORTURE_MANIFEST = ROOT / "benchmarks/compiler_torture/manifest.json"
REAL_WORLD_MANIFEST = ROOT / "benchmarks/real_world/manifest.json"
ATTRIBUTION_IDS = {
    "w2_spec_distinct_n032", "w3_spec_repeated_n032",
    "w4_infer_deep_d018", "w5_infer_wide_w100",
    "rw_packages_regex", "rw_sdk_datastructures",
    "rw_projects_rolling_window", "example_checkout",
}


def run(cmd: list[str], *, cwd: Path = ROOT, log: Path | None = None) -> str:
    result = subprocess.run(cmd, cwd=cwd, text=True, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT)
    if log:
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(result.stdout)
    if result.returncode:
        tail = "\n".join(result.stdout.splitlines()[-35:])
        raise RuntimeError(f"Command failed ({result.returncode}): {' '.join(cmd)}\n{tail}")
    return result.stdout.strip()


def fixture_rows(repo: Path) -> tuple[list[dict], list[dict]]:
    torture = json.loads((repo / "benchmarks/compiler_torture/manifest.json").read_text())["fixtures"]
    real_world = json.loads((repo / "benchmarks/real_world/manifest.json").read_text())["workloads"]
    return torture, real_world


def fixture_hashes(repo: Path) -> dict[str, str]:
    torture, real_world = fixture_rows(repo)
    return {row["id"]: row["sha256"] for row in [*torture, *real_world]}


def select_fixture_ids(repo: Path, include_real_world: bool) -> list[str]:
    torture, real_world = fixture_rows(repo)
    selected = {row["id"] for row in torture}
    selected.update(row["id"] for row in real_world if row["id"] in ATTRIBUTION_IDS)
    if include_real_world:
        selected.update(row["id"] for row in real_world)
    missing = ATTRIBUTION_IDS - selected
    if missing:
        raise RuntimeError(f"Attribution fixtures missing from manifests: {sorted(missing)}")
    return sorted(selected)


def run_suite(repo: Path, moss_bin: Path, out: Path, fixture_ids: list[str], reps: int) -> None:
    cmd = [sys.executable, str(repo / "benchmarks/phase25_4/cpp_baseline/run.py"),
           "--moss", str(moss_bin), "--suite", "all", "--mode", "timing",
           "--reps", str(reps), "--fixture-ids", ",".join(fixture_ids),
           "--results-dir", str(out)]
    log = out.parent / f"{out.name}.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w") as log_handle:
        proc = subprocess.Popen(cmd, cwd=repo, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, bufsize=1)
        assert proc.stdout is not None
        for line in proc.stdout:
            print(line, end="", flush=True)
            log_handle.write(line)
        return_code = proc.wait()
    if return_code:
        tail = "\n".join(log.read_text().splitlines()[-35:])
        raise RuntimeError(f"Benchmark command failed ({return_code}): {' '.join(cmd)}\n{tail}")
    results_path = out / "results.csv"
    if not results_path.is_file():
        raise RuntimeError(f"Benchmark runner did not produce {results_path}")
    with results_path.open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    successes = [row for row in rows if row.get("success") == "True"]
    expected_trials = len(fixture_ids) * reps
    if len(rows) != expected_trials or len(successes) != expected_trials:
        failures = [f"{row.get('fixture_id')} rep {row.get('iteration')} ({row.get('success')})"
                    for row in rows if row.get("success") != "True"]
        raise RuntimeError(f"Incomplete/failed suite: expected {expected_trials} successful trials, "
                           f"got {len(successes)}. Failures: {', '.join(failures[:12])}")


def medians(results_path: Path) -> dict[str, float]:
    by_id: dict[str, list[float]] = {}
    with results_path.open(newline="") as handle:
        for row in csv.DictReader(handle):
            if row.get("mode") == "timing" and row.get("success") == "True":
                by_id.setdefault(row["fixture_id"], []).append(float(row["wall_ms"]))
    return {fid: statistics.median(values) for fid, values in by_id.items()}


def render_report(out: Path, baseline_label: str | None, baseline_sha: str | None,
                  candidate_sha: str, base: dict[str, float] | None,
                  current: dict[str, float], include_real_world: bool, reps: int) -> None:
    lines = [
        "# Local compiler performance run", "",
        f"- Host: `{platform.platform()}` ({os.cpu_count() or 'unknown'} logical CPUs)",
        f"- Candidate Git SHA: `{candidate_sha}`",
        f"- Candidate binary SHA256: `{hashlib.sha256((ROOT / 'build/compiler/moss-opt').read_bytes()).hexdigest()}`",
        f"- Repetitions: {reps} untraced timing run(s) per fixture; order reverses on alternate repetitions",
        f"- Fixtures: {len(current)} (full torture suite + eight attribution workloads"
        + (" + remaining curated real-world workloads" if include_real_world else "") + ")",
        "- Raw CSV, metadata, and logs are in this run directory.", "",
    ]
    if base is None:
        lines += ["No baseline ref was supplied; this run records current-checkout measurements only.", "",
                  "## Small-fixture check latency", "",
                  "| Fixture | Candidate median |", "|---|---:|"]
        for fid in ("rw_projects_rolling_window", "example_checkout"):
            if fid in current:
                lines.append(f"| `{fid}` | {current[fid]:.2f} ms |")
        lines.append("")
    else:
        lines += [f"- Baseline: `{baseline_label}` (`{baseline_sha}`), built and measured on this host.", "",
                  "## Aggregate timing", "",
                  "Total is the sum of per-fixture medians (seconds).", "",
                  "| Baseline total | Candidate total | Change |", "|---:|---:|---:|"]
        base_total = sum(base.values())
        current_total = sum(current.values())
        change = (current_total / base_total - 1) * 100 if base_total else 0.0
        lines.append(f"| {base_total / 1000:.3f} s | {current_total / 1000:.3f} s | {change:+.1f}% |")
        lines += ["", "## Eight attribution workloads", "",
                  "| Fixture | Baseline median | Candidate median | Change |", "|---|---:|---:|---:|"]
        for fid in sorted(ATTRIBUTION_IDS):
            if fid not in current or fid not in base:
                lines.append(f"| `{fid}` | missing | missing | n/a |")
                continue
            delta = current[fid] / base[fid] - 1 if base[fid] else 0.0
            lines.append(f"| `{fid}` | {base[fid]:.2f} ms | {current[fid]:.2f} ms | {delta:+.1%} |")
        lines += ["", "## Small-fixture check latency", "",
                  "| Fixture | Baseline median | Candidate median | Change |", "|---|---:|---:|---:|"]
        for fid in ("rw_projects_rolling_window", "example_checkout"):
            before, after = base.get(fid), current.get(fid)
            if before is None or after is None:
                lines.append(f"| `{fid}` | missing | missing | n/a |")
                continue
            delta = after / before - 1 if before else 0.0
            lines.append(f"| `{fid}` | {before:.2f} ms | {after:.2f} ms | {delta:+.1%} |")
        slower = [(fid, base[fid], current[fid]) for fid in current.keys() & base.keys()
                  if base[fid] > 0 and current[fid] > base[fid] * 1.05]
        lines += ["", "## Fixtures slower by more than 5%", ""]
        if slower:
            lines += ["| Fixture | Baseline | Candidate | Increase | Change |", "|---|---:|---:|---:|---:|"]
            for fid, before, after in sorted(slower, key=lambda item: item[2] / item[1], reverse=True):
                lines.append(f"| `{fid}` | {before:.2f} ms | {after:.2f} ms | +{after-before:.2f} ms | {(after/before-1):+.1%} |")
        else:
            lines.append("None.")
        lines.append("")
    (out / "LOCAL_REPORT.md").write_text("\n".join(lines))


def main() -> int:
    baseline_ref = os.environ.get("MOSS_PERF_BASELINE", "").strip()
    include_real_world = os.environ.get("MOSS_PERF_INCLUDE_REAL_WORLD", "").lower() in {"1", "true", "yes"}
    reps = int(os.environ.get("MOSS_PERF_REPS", "3"))
    if reps < 1:
        raise RuntimeError("MOSS_PERF_REPS must be at least 1")
    run_id = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    out = ROOT / "tmp/compiler-perf" / run_id
    out.mkdir(parents=True, exist_ok=False)
    fixture_ids = select_fixture_ids(ROOT, include_real_world)
    candidate_bin = ROOT / "build/compiler/moss-opt"
    if not candidate_bin.is_file():
        raise RuntimeError("Optimized compiler is missing; run `make moss-opt` first")
    candidate_sha = run(["git", "rev-parse", "HEAD"])
    print(f"Running {len(fixture_ids)} fixtures × {reps} timing repetitions")
    worktree = out / "baseline-checkout"
    if baseline_ref:
        base_sha = run(["git", "rev-parse", f"{baseline_ref}^{{commit}}"])
        run(["git", "worktree", "add", "--detach", str(worktree), base_sha])
        try:
            cores = str(os.cpu_count() or 1)
            run(["make", f"--jobs={cores}", "moss-opt"], cwd=worktree,
                log=out / "baseline-build.log")
            # The selected historical revision may predate the output-directory
            # and fixture-filter options. Reuse this driver version while
            # keeping REPO_ROOT relative to the baseline worktree.
            baseline_runner = worktree / "benchmarks/phase25_4/cpp_baseline/run.py"
            shutil.copy2(RUNNER, baseline_runner)
            base_ids = select_fixture_ids(worktree, include_real_world)
            if base_ids != fixture_ids or fixture_hashes(worktree) != fixture_hashes(ROOT):
                raise RuntimeError("Baseline and candidate benchmark fixture manifests differ")
            run_suite(worktree, worktree / "build/compiler/moss-opt",
                      out / "baseline", base_ids, reps)
            run_suite(ROOT, candidate_bin, out / "candidate", fixture_ids, reps)
            candidate_medians = medians(out / "candidate/results.csv")
            baseline_metadata = json.loads((out / "baseline/metadata.json").read_text())
            candidate_metadata = json.loads((out / "candidate/metadata.json").read_text())
            if (baseline_metadata.get("platform"), baseline_metadata.get("cpus")) != \
                    (candidate_metadata.get("platform"), candidate_metadata.get("cpus")):
                raise RuntimeError("Baseline and candidate benchmark metadata show different hosts")
            base_medians = medians(out / "baseline/results.csv")
            if base_medians.keys() != candidate_medians.keys():
                raise RuntimeError("Baseline and candidate produced different fixture result sets")
            render_report(out, baseline_ref, base_sha, candidate_sha, base_medians,
                          candidate_medians, include_real_world, reps)
        finally:
            run(["git", "worktree", "remove", "--force", str(worktree)])
    else:
        run_suite(ROOT, candidate_bin, out / "candidate", fixture_ids, reps)
        candidate_medians = medians(out / "candidate/results.csv")
        render_report(out, None, None, candidate_sha, None, candidate_medians,
                      include_real_world, reps)

    print(f"Report: {out / 'LOCAL_REPORT.md'}")
    if not baseline_ref:
        print("No before/after comparison: set MOSS_PERF_BASELINE=<git-ref> to compare.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, ValueError) as exc:
        print(f"compiler-perf: {exc}", file=sys.stderr)
        raise SystemExit(1)
