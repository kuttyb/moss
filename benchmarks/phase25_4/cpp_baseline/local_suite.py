#!/usr/bin/env python3
"""Interleaved local B1 timing and profiling suite.

Run artifacts are isolated under tmp/compiler-perf/<run-id>. Certified B1
historical reports and results are never overwritten.
"""
from __future__ import annotations

import csv
import datetime as dt
import hashlib
import importlib.util
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
ATTRIBUTION_IDS = {
    "w2_spec_distinct_n032", "w3_spec_repeated_n032",
    "w4_infer_deep_d018", "w5_infer_wide_w100",
    "rw_packages_regex", "rw_sdk_datastructures",
    "rw_projects_rolling_window", "example_checkout",
}
COUNTER_REPORT_KEYS = (
    "functions_analyzed", "types_analyzed", "specialization_requests",
    "distinct_specialization_keys", "checked_specializations",
    "inference_rounds", "inference_work_units", "parse_cache_hits",
    "parse_cache_misses", "concrete_effect_evaluations",
    "specialization_effect_cache_hits", "specialization_effect_computations",
    "effects_fixed_point_rounds",
)
SLOWDOWN_PCT_FLOOR = 5.0
SLOWDOWN_ABS_FLOOR_MS = 20.0
NONOVERLAP_ABS_FLOOR_MS = 2.0
PROFILE_REPS = 1
TIMEOUT_SECONDS = 120
CERTIFIED_BASELINE_SHA = "c66fb76ca0012b8125d70d404adbff13daa1cf4e"


def format_ms(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.2f} ms"


def format_rss(value: int | None) -> str:
    return "n/a" if value is None else f"{value} KiB"


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


def selected_fixtures(repo: Path, include_real_world: bool) -> list[dict]:
    torture, real_world = fixture_rows(repo)
    fixtures = [{**row, "suite_group": "torture"} for row in torture]
    if include_real_world:
        fixtures.extend({**row, "suite_group": "real_world"} for row in real_world)
    else:
        fixtures.extend({**row, "suite_group": "real_world"} for row in real_world
                        if row["id"] in ATTRIBUTION_IDS)
    ids = {row["id"] for row in fixtures}
    missing = ATTRIBUTION_IDS - ids
    if missing:
        raise RuntimeError(f"Attribution fixtures missing from manifests: {sorted(missing)}")
    if len(ids) != len(fixtures):
        raise RuntimeError("Duplicate fixture IDs across benchmark manifests")
    return sorted(fixtures, key=lambda row: row["id"])


def load_runner(repo: Path, output_dir: Path, module_name: str):
    runner_path = repo / "benchmarks/phase25_4/cpp_baseline/run.py"
    spec = importlib.util.spec_from_file_location(module_name, runner_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load B1 runner from {runner_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.RESULTS_DIR = output_dir
    module.REPORT_PATH = output_dir / "REPORT.md"
    module.TRACES_DIR = output_dir / "traces"
    if not module.verify_corpus_integrity():
        raise RuntimeError(f"Corpus integrity validation failed in {repo}")
    return module


def write_rows(path: Path, rows: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("")
        return
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def measure(module, binary: Path, fixture: dict, rep: int, mode: str,
            side: str, order: str) -> dict:
    row = module.run_trial(binary, fixture, rep, TIMEOUT_SECONDS, mode=mode)
    row["side"] = side
    row["measurement_order"] = order
    return row


def require_success(row: dict) -> None:
    if not row["success"]:
        raise RuntimeError(f"{row['mode']} trial failed for {row['side']}/{row['fixture_id']} "
                           f"rep {row['iteration']}: timeout={row['timed_out']} "
                           f"exit={row['exit_code']} diagnostic={row['error_code']}")


def write_trial_checkpoint(out: Path, all_rows: dict[str, dict[str, list[dict]]]) -> None:
    for side, modes in all_rows.items():
        for mode, rows in modes.items():
            write_rows(out / side / f"{mode}_trials.csv", rows)


def summarize(rows: list[dict]) -> dict[str, dict]:
    grouped: dict[str, list[dict]] = {}
    for row in rows:
        if row["mode"] == "timing":
            grouped.setdefault(row["fixture_id"], []).append(row)
    summaries = {}
    for fid, samples in grouped.items():
        wall = [float(row["wall_ms"]) for row in samples]
        cpu = [float(row["cpu_user_ms"]) + float(row["cpu_sys_ms"])
               for row in samples if row["cpu_user_ms"] is not None and row["cpu_sys_ms"] is not None]
        rss = [int(row["peak_rss_kib"]) for row in samples if row["peak_rss_kib"] is not None]
        summaries[fid] = {
            "wall_median_ms": statistics.median(wall),
            "wall_min_ms": min(wall),
            "wall_max_ms": max(wall),
            "wall_samples_ms": wall,
            "cpu_median_ms": statistics.median(cpu) if cpu else None,
            "cpu_samples_ms": cpu,
            "peak_rss_kib_max": max(rss) if rss else None,
            "peak_rss_samples_kib": rss,
        }
    return summaries


def profile_counters(rows: list[dict]) -> dict[str, dict]:
    profiles = {}
    for row in rows:
        if row["mode"] == "profiling":
            profiles[row["fixture_id"]] = {
                "aggregated": json.loads(row["counters_json"]),
                "checker_records": json.loads(row["counter_records_json"]),
            }
    return profiles


def write_phase26_data(out: Path, fixtures: list[dict], baseline_rows: list[dict] | None,
                       candidate_rows: list[dict]) -> None:
    base = summarize(baseline_rows or [])
    cand = summarize(candidate_rows)
    base_profiles = profile_counters(baseline_rows or [])
    candidate_profiles = profile_counters(candidate_rows)
    median_rows = []
    counter_rows = []
    for fixture in fixtures:
        fid = fixture["id"]
        c = cand[fid]
        b = base.get(fid, {})
        median_rows.append({
            "fixture_id": fid,
            "category": fixture["category"],
            "workload": fixture.get("metadata", {}).get("workload", fixture["category"]),
            "source_sha256": fixture["sha256"],
            "repetitions": len(c["wall_samples_ms"]),
            "baseline_wall_samples_ms": json.dumps(b.get("wall_samples_ms", [])),
            "baseline_wall_median_ms": b.get("wall_median_ms", ""),
            "baseline_wall_min_ms": b.get("wall_min_ms", ""),
            "baseline_wall_max_ms": b.get("wall_max_ms", ""),
            "candidate_wall_samples_ms": json.dumps(c["wall_samples_ms"]),
            "candidate_wall_median_ms": c["wall_median_ms"],
            "candidate_wall_min_ms": c["wall_min_ms"],
            "candidate_wall_max_ms": c["wall_max_ms"],
            "baseline_cpu_median_ms": b.get("cpu_median_ms", ""),
            "candidate_cpu_median_ms": c["cpu_median_ms"],
            "baseline_peak_rss_kib_max": b.get("peak_rss_kib_max", ""),
            "candidate_peak_rss_kib_max": c["peak_rss_kib_max"],
        })
        bp = base_profiles.get(fid, {})
        cp = candidate_profiles.get(fid, {})
        counter_rows.append({
            "fixture_id": fid,
            "source_sha256": fixture["sha256"],
            "baseline_counter_totals_json": json.dumps(bp.get("aggregated", {}), sort_keys=True),
            "baseline_checker_counter_records_json": json.dumps(bp.get("checker_records", []), sort_keys=True),
            "candidate_counter_totals_json": json.dumps(cp.get("aggregated", {}), sort_keys=True),
            "candidate_checker_counter_records_json": json.dumps(cp.get("checker_records", []), sort_keys=True),
        })
    write_rows(out / "phase26_candidate_medians.csv", median_rows)
    write_rows(out / "phase26_work_counters.csv", counter_rows)


def report_counters(profiles: dict[str, dict], fixture_ids: list[str]) -> list[str]:
    lines = ["", "## Profiling work counters", "",
             "One separate profiling sample per fixture. This table shows candidate totals. Counter totals aggregate across checker instances; the CSV includes baseline and candidate plus each checker's individual records. Expected-negative fixtures can fail before any checker counters are emitted; those entries are n/a.", "",
             "| Fixture | " + " | ".join(COUNTER_REPORT_KEYS) + " |",
             "|---|" + "---:|" * len(COUNTER_REPORT_KEYS)]
    for fid in fixture_ids:
        profile = profiles.get(fid, {})
        counters = profile.get("aggregated", {})
        vals = (" | ".join(str(counters.get(key, 0)) for key in COUNTER_REPORT_KEYS)
                if profile.get("checker_records")
                else " | ".join("n/a" for _ in COUNTER_REPORT_KEYS))
        lines.append(f"| `{fid}` | {vals} |")
    lines += ["", "Full per-fixture, per-checker records: `phase26_work_counters.csv`.", ""]
    return lines


def render_report(out: Path, baseline_label: str | None, baseline_sha: str | None,
                  candidate_sha: str, candidate_source_sha: str,
                  candidate_binary_sha: str,
                  base_rows: list[dict] | None, candidate_rows: list[dict],
                  fixtures: list[dict], include_real_world: bool, timing_reps: int,
                  host_metadata: dict) -> None:
    base = summarize(base_rows or [])
    cand = summarize(candidate_rows)
    candidate_profiles = profile_counters(candidate_rows)
    fixture_ids = [fixture["id"] for fixture in fixtures]
    load_before = host_metadata.get("load_average_before_timing")
    load_after = host_metadata.get("load_average_after_profiling")
    load_text = (f"before timing `{load_before}`; after profiling `{load_after}`"
                 if load_before is not None else "unavailable")
    lines = [
        "# Local compiler performance run", "",
        f"- Host: `{host_metadata['host']}`; `{host_metadata['platform']}`; CPU `{host_metadata['cpu']}` ({host_metadata['logical_cpus']} logical CPUs).",
        f"- Load average (1, 5, 15 min): {load_text}.",
        f"- Candidate benchmark checkout SHA: `{candidate_sha}`",
        f"- Candidate compiler source SHA: `{candidate_source_sha}`",
        f"- Candidate binary SHA256: `{candidate_binary_sha}`",
        f"- Certification mode: `{host_metadata.get('certification_mode', False)}`; working tree dirty at start: `{host_metadata.get('working_tree_dirty', 'unknown')}`.",
        f"- Timing repetitions: {timing_reps}, untraced and interleaved per fixture; order reverses across repetitions.",
        f"- Profiling repetitions: {PROFILE_REPS} separate traced sample per fixture; profiling data is not used in timing summaries.",
        f"- Fixtures: {len(fixtures)} (54 torture plus selected curated workloads; 77 when all are enabled).",
        "- Each fixture's profile counters preserve separate Checker-instance records in `phase26_work_counters.csv`.",
        "- Complete timing medians, per-fixture CPU medians, peak RSS, ranges, and samples: `phase26_candidate_medians.csv`.",
        "- CPU is GNU `/usr/bin/time` user+system time at centisecond resolution; `0.00 ms` means below 10 ms, not zero CPU.",
        "",
    ]
    if base_rows is None:
        lines += ["No baseline ref was supplied; these are current-checkout measurements only.", "",
                  "## Eight attribution workloads", "",
                  "| Fixture | Candidate wall median | CPU median | Peak RSS max |", "|---|---:|---:|---:|"]
        for fid in sorted(ATTRIBUTION_IDS):
            c = cand[fid]
            lines.append(f"| `{fid}` | {c['wall_median_ms']:.2f} ms | {format_ms(c['cpu_median_ms'])} | {format_rss(c['peak_rss_kib_max'])} |")
        lines += ["", "## Small-fixture latency", "", "| Fixture | Wall median | CPU median | Peak RSS max |", "|---|---:|---:|---:|"]
        for fid in ("rw_projects_rolling_window", "example_checkout"):
            c = cand[fid]
            lines.append(f"| `{fid}` | {c['wall_median_ms']:.2f} ms | {format_ms(c['cpu_median_ms'])} | {format_rss(c['peak_rss_kib_max'])} |")
        lines += ["", "## All fixture medians and resource use", "",
                  "| Fixture | Wall median | CPU median | Peak RSS max |", "|---|---:|---:|---:|"]
        for fid in fixture_ids:
            c = cand[fid]
            lines.append(f"| `{fid}` | {c['wall_median_ms']:.2f} ms | {format_ms(c['cpu_median_ms'])} | {format_rss(c['peak_rss_kib_max'])} |")
    else:
        base_total = sum(value["wall_median_ms"] for value in base.values())
        candidate_total = sum(value["wall_median_ms"] for value in cand.values())
        pct = (candidate_total / base_total - 1) * 100 if base_total else 0.0
        lines += [f"- Baseline: `{baseline_label}` resolved to `{baseline_sha}`, built on this host.", "",
                  "## Aggregate timing", "",
                  "Sum of per-fixture wall-time medians; the percentages compare equivalent timing-only trials.", "",
                  "| Baseline total | Candidate total | Change |", "|---:|---:|---:|"]
        lines.append(f"| {base_total / 1000:.3f} s | {candidate_total / 1000:.3f} s | {pct:+.1f}% |")
        lines += ["", "## Eight attribution workloads", "",
                  "| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |", "|---|---:|---:|---:|---:|---:|---:|---:|"]
        for fid in sorted(ATTRIBUTION_IDS):
            b, c = base[fid], cand[fid]
            delta = c["wall_median_ms"] / b["wall_median_ms"] - 1 if b["wall_median_ms"] else 0.0
            lines.append(f"| `{fid}` | {b['wall_median_ms']:.2f} ms | {c['wall_median_ms']:.2f} ms | {delta:+.1%} | {format_ms(b['cpu_median_ms'])} | {format_ms(c['cpu_median_ms'])} | {format_rss(b['peak_rss_kib_max'])} | {format_rss(c['peak_rss_kib_max'])} |")
        lines += ["", "## All fixture medians and resource use", "",
                  "| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |", "|---|---:|---:|---:|---:|---:|---:|---:|"]
        for fid in fixture_ids:
            b, c = base[fid], cand[fid]
            delta = c["wall_median_ms"] / b["wall_median_ms"] - 1 if b["wall_median_ms"] else 0.0
            lines.append(f"| `{fid}` | {b['wall_median_ms']:.2f} ms | {c['wall_median_ms']:.2f} ms | {delta:+.1%} | {format_ms(b['cpu_median_ms'])} | {format_ms(c['cpu_median_ms'])} | {format_rss(b['peak_rss_kib_max'])} | {format_rss(c['peak_rss_kib_max'])} |")
        lines += ["", "## Small-fixture latency", "",
                  "| Fixture | Baseline wall median | Candidate wall median | Change | Baseline CPU median | Candidate CPU median | Baseline peak RSS max | Candidate peak RSS max |", "|---|---:|---:|---:|---:|---:|---:|---:|"]
        for fid in ("rw_projects_rolling_window", "example_checkout"):
            b, c = base[fid], cand[fid]
            delta = c["wall_median_ms"] / b["wall_median_ms"] - 1 if b["wall_median_ms"] else 0.0
            lines.append(f"| `{fid}` | {b['wall_median_ms']:.2f} ms | {c['wall_median_ms']:.2f} ms | {delta:+.1%} | {format_ms(b['cpu_median_ms'])} | {format_ms(c['cpu_median_ms'])} | {format_rss(b['peak_rss_kib_max'])} | {format_rss(c['peak_rss_kib_max'])} |")

        slow = []
        noisy = []
        for fid in fixture_ids:
            b, c = base[fid], cand[fid]
            before, after = b["wall_median_ms"], c["wall_median_ms"]
            delta = after - before
            pct = (delta / before * 100) if before else 0.0
            if pct > SLOWDOWN_PCT_FLOOR:
                ranges_disjoint = (len(c["wall_samples_ms"]) >= 2 and len(b["wall_samples_ms"]) >= 2
                                   and c["wall_min_ms"] > b["wall_max_ms"])
                disjoint = ranges_disjoint and delta >= NONOVERLAP_ABS_FLOOR_MS
                item = (fid, b, c, delta, pct, disjoint)
                if delta > SLOWDOWN_ABS_FLOOR_MS or disjoint:
                    slow.append(item)
                else:
                    noisy.append(item)
        lines += ["", "## Slowdowns beyond the noise floor", "",
                  f"Alert rule: candidate median > baseline median by {SLOWDOWN_PCT_FLOOR:.0f}% and either >{SLOWDOWN_ABS_FLOOR_MS:.0f} ms absolute or sample ranges do not overlap with an increase >{NONOVERLAP_ABS_FLOOR_MS:.0f} ms (range comparison requires at least two trials per side).", ""]
        if slow:
            lines += ["| Fixture | Baseline median [min–max] | Candidate median [min–max] | Increase | Change | CPU median | Peak RSS max |", "|---|---:|---:|---:|---:|---:|---:|"]
            for fid, b, c, delta, pct, disjoint in sorted(slow, key=lambda item: item[4], reverse=True):
                lines.append(f"| `{fid}` | {b['wall_median_ms']:.2f} [{b['wall_min_ms']:.2f}–{b['wall_max_ms']:.2f}] ms | {c['wall_median_ms']:.2f} [{c['wall_min_ms']:.2f}–{c['wall_max_ms']:.2f}] ms | +{delta:.2f} ms | +{pct:.1f}% | {format_ms(c['cpu_median_ms'])} | {format_rss(c['peak_rss_kib_max'])} |")
        else:
            lines.append("None.")
        lines += ["", "## Above 5% but below the noise floor", ""]
        if noisy:
            lines += ["| Fixture | Baseline median | Candidate median | Increase | Change |", "|---|---:|---:|---:|---:|"]
            for fid, b, c, delta, pct, _ in sorted(noisy, key=lambda item: item[4], reverse=True):
                lines.append(f"| `{fid}` | {b['wall_median_ms']:.2f} ms | {c['wall_median_ms']:.2f} ms | +{delta:.2f} ms | +{pct:.1f}% |")
        else:
            lines.append("None.")
    lines += report_counters(candidate_profiles, fixture_ids)
    (out / "LOCAL_REPORT.md").write_text("\n".join(lines))


def write_metadata(out: Path, data: dict) -> None:
    (out / "metadata.json").write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")


def main() -> int:
    task_tmp = ROOT / "tmp"
    task_tmp.mkdir(parents=True, exist_ok=True)
    os.environ["TMPDIR"] = str(task_tmp)
    baseline_ref = os.environ.get("MOSS_PERF_BASELINE", "").strip()
    include_real_world = os.environ.get("MOSS_PERF_INCLUDE_REAL_WORLD", "").lower() in {"1", "true", "yes"}
    certify = os.environ.get("MOSS_PERF_CERTIFY", "").lower() in {"1", "true", "yes"}
    timing_reps = int(os.environ.get("MOSS_PERF_REPS", "3"))
    if timing_reps < 1:
        raise RuntimeError("MOSS_PERF_REPS must be at least 1")

    run_id = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    out = ROOT / "tmp/compiler-perf" / run_id
    out.mkdir(parents=True, exist_ok=False)
    candidate_binary = ROOT / "build/compiler/moss-opt"
    if not candidate_binary.is_file():
        raise RuntimeError("Optimized compiler is missing; run `make moss-opt` first")
    candidate_sha = run(["git", "rev-parse", "HEAD"])
    working_tree_status = run(["git", "status", "--porcelain=v1", "--untracked-files=normal"])
    working_tree_dirty = bool(working_tree_status)
    origin_main_sha = run(["git", "rev-parse", "origin/main"])
    if certify:
        if working_tree_dirty:
            raise RuntimeError("MOSS_PERF_CERTIFY=1 requires a clean working tree; status:\n" + working_tree_status)
        changed_compiler_sources = run(["git", "diff", "--name-only", "origin/main",
                                        candidate_sha, "--", "src"])
        if changed_compiler_sources:
            raise RuntimeError("MOSS_PERF_CERTIFY=1 requires compiler sources identical to origin/main; changed paths:\n" + changed_compiler_sources)
    candidate_source_sha = origin_main_sha if certify else candidate_sha
    candidate_binary_sha = hashlib.sha256(candidate_binary.read_bytes()).hexdigest()
    fixtures = selected_fixtures(ROOT, include_real_world)
    if certify:
        if not baseline_ref:
            raise RuntimeError("MOSS_PERF_CERTIFY=1 requires MOSS_PERF_BASELINE=c66fb76c")
        if timing_reps != 3:
            raise RuntimeError("MOSS_PERF_CERTIFY=1 requires exactly 3 timing repetitions")
        if not include_real_world or len(fixtures) != 77:
            raise RuntimeError("MOSS_PERF_CERTIFY=1 requires the full 77-fixture corpus; set MOSS_PERF_INCLUDE_REAL_WORLD=1")
    fixture_ids = [row["id"] for row in fixtures]
    print(f"Preparing {len(fixtures)} fixtures, {timing_reps} timing repetitions, {PROFILE_REPS} profiling sample")

    worktree = out / "baseline-checkout"
    baseline_sha = None
    baseline_binary = None
    if baseline_ref:
        baseline_sha = run(["git", "rev-parse", f"{baseline_ref}^{{commit}}"])
        if certify and baseline_sha != CERTIFIED_BASELINE_SHA:
            raise RuntimeError(f"MOSS_PERF_CERTIFY=1 requires baseline {CERTIFIED_BASELINE_SHA}, got {baseline_sha}")
        run(["git", "worktree", "add", "--detach", str(worktree), baseline_sha])
    try:
        if baseline_ref:
            run(["make", f"--jobs={os.cpu_count() or 1}", "moss-opt"], cwd=worktree,
                log=out / "baseline-build.log")
            baseline_binary = worktree / "build/compiler/moss-opt"
            # Historical commits may carry stale benchmark directory hashes.
            # Apply the candidate's manifests in this temporary checkout so its
            # verifier checks the exact same fixture bytes for both compilers.
            # Any baseline fixture drift still fails integrity verification.
            for relative in (
                "benchmarks/compiler_torture/manifest.json",
                "benchmarks/compiler_torture/expected/validation_manifest.json",
                "benchmarks/real_world/manifest.json",
            ):
                shutil.copy2(ROOT / relative, worktree / relative)
            # Preserve profiling output and resource samples from the local runner.
            shutil.copy2(RUNNER, worktree / "benchmarks/phase25_4/cpp_baseline/run.py")

        modules = {"candidate": load_runner(ROOT, out / "candidate", "phase26_b1_candidate")}
        if baseline_ref:
            modules["baseline"] = load_runner(worktree, out / "baseline", "phase26_b1_baseline")
        candidate_hashes = fixture_hashes(ROOT)
        if baseline_ref:
            baseline_fixtures = selected_fixtures(worktree, include_real_world)
            if [row["id"] for row in baseline_fixtures] != fixture_ids:
                raise RuntimeError("Baseline and candidate fixture IDs differ")
            if fixture_hashes(worktree) != candidate_hashes:
                raise RuntimeError("Baseline and candidate fixture hashes differ")

        baseline_binary_sha = (hashlib.sha256(baseline_binary.read_bytes()).hexdigest()
                               if baseline_binary else None)
        modules["candidate"].TRACES_DIR.mkdir(parents=True, exist_ok=True)
        if baseline_ref:
            modules["baseline"].TRACES_DIR.mkdir(parents=True, exist_ok=True)
        all_rows = {side: {"timing": [], "profiling": []} for side in modules}
        order_counts = {"baseline_first": 0, "candidate_first": 0}

        started = dt.datetime.now(dt.timezone.utc).isoformat()
        loads_before = os.getloadavg() if hasattr(os, "getloadavg") else None
        toolchain = run(["clang++", "--version"]).splitlines()[0]
        metadata = {
            "status": "running",
            "run_id": run_id,
            "started_utc": started,
            "candidate_git_sha": candidate_sha,
            "candidate_compiler_source_sha": candidate_source_sha,
            "origin_main_sha": origin_main_sha,
            "certification_mode": certify,
            "working_tree_dirty": working_tree_dirty,
            "working_tree_status": working_tree_status.splitlines(),
            "baseline_ref": baseline_ref or None,
            "baseline_git_sha": baseline_sha,
            "candidate_binary_sha256": candidate_binary_sha,
            "baseline_binary_sha256": baseline_binary_sha,
            "host": platform.node(),
            "platform": platform.platform(),
            "cpu": platform.processor() or platform.machine(),
            "logical_cpus": os.cpu_count(),
            "load_average_before_timing": loads_before,
            "toolchain": toolchain,
            "fixture_count": len(fixtures),
            "fixture_sha256": {row["id"]: row["sha256"] for row in fixtures},
            "include_real_world": include_real_world,
            "timing_repetitions": timing_reps,
            "profiling_repetitions": PROFILE_REPS,
            "timeout_seconds": TIMEOUT_SECONDS,
            "cpu_time_resolution_ms": 10,
            "order_strategy": "adjacent baseline/candidate per fixture; flip first side by (repetition + fixture-position) parity",
            "slowdown_rule": "median >5% and (>20 ms absolute or non-overlapping sample ranges with >2 ms absolute increase)",
            "timing_mode": "untraced",
            "profile_mode": "separate traced run; excluded from timing/CPU/RSS summaries",
        }
        write_metadata(out, metadata)
        side_binaries = {"candidate": candidate_binary}
        if baseline_ref:
            side_binaries["baseline"] = baseline_binary

        # Adjacent paired trials reduce drift; reverse both fixture order and
        # which compiler goes first on alternating positions/repetitions.
        for rep in range(1, timing_reps + 1):
            run_fixtures = fixtures if rep % 2 else list(reversed(fixtures))
            print(f"\n=== Interleaved timing repetition {rep}/{timing_reps} ===")
            for position, fixture in enumerate(run_fixtures):
                if baseline_ref:
                    baseline_first = (rep + position) % 2 == 0
                    order = ["baseline", "candidate"] if baseline_first else ["candidate", "baseline"]
                    order_counts["baseline_first" if baseline_first else "candidate_first"] += 1
                else:
                    order = ["candidate"]
                for side in order:
                    row = measure(modules[side], side_binaries[side], fixture, rep,
                                  "timing", side, ">".join(order))
                    all_rows[side]["timing"].append(row)
                    print(f"[timing {rep}/{timing_reps}] {fixture['id']:<40} {side:<9} {row['wall_ms']:9.2f} ms")
                    write_trial_checkpoint(out, all_rows)
                    require_success(row)

        profile_fixtures = fixtures
        print(f"\n=== Separate profiling pass ({PROFILE_REPS} sample) ===")
        for position, fixture in enumerate(profile_fixtures):
            if baseline_ref:
                baseline_first = position % 2 == 0
                order = ["baseline", "candidate"] if baseline_first else ["candidate", "baseline"]
            else:
                order = ["candidate"]
            for side in order:
                row = measure(modules[side], side_binaries[side], fixture, 1,
                              "profiling", side, ">".join(order))
                all_rows[side]["profiling"].append(row)
                print(f"[profiling] {fixture['id']:<40} {side:<9} counters={len(json.loads(row['counter_records_json']))}")
                write_trial_checkpoint(out, all_rows)
                require_success(row)

        candidate_rows = all_rows["candidate"]["timing"] + all_rows["candidate"]["profiling"]
        baseline_rows = (all_rows["baseline"]["timing"] + all_rows["baseline"]["profiling"]
                         if baseline_ref else None)
        for side, rows in (("candidate", candidate_rows), ("baseline", baseline_rows or [])):
            if side == "baseline" and not baseline_ref:
                continue
            side_stats = summarize(rows)
            if set(side_stats) != set(fixture_ids):
                raise RuntimeError(f"Incomplete {side} fixture timing set")
            for fid in fixture_ids:
                timing_count = sum(row["fixture_id"] == fid and row["mode"] == "timing" for row in rows)
                profile_count = sum(row["fixture_id"] == fid and row["mode"] == "profiling" for row in rows)
                if timing_count != timing_reps or profile_count != PROFILE_REPS:
                    raise RuntimeError(f"Incomplete {side}/{fid}: timing={timing_count}, profiling={profile_count}")
        expected_profiles = len(fixtures) * (2 if baseline_ref else 1)
        actual_profiles = sum(len(all_rows[side]["profiling"]) for side in all_rows)
        if actual_profiles != expected_profiles:
            raise RuntimeError(f"Expected {expected_profiles} profiling trials, got {actual_profiles}")
        write_phase26_data(out, fixtures, baseline_rows, candidate_rows)
        load_values_after = os.getloadavg() if hasattr(os, "getloadavg") else None
        metadata.update({
            "finished_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
            "load_average_after_profiling": load_values_after,
            "pair_order_counts": order_counts,
            "status": "complete",
        })
        write_metadata(out, metadata)
        render_report(out, baseline_ref or None, baseline_sha, candidate_sha,
                      candidate_source_sha, candidate_binary_sha, baseline_rows, candidate_rows, fixtures,
                      include_real_world, timing_reps, metadata)
    finally:
        if baseline_ref and worktree.exists():
            run(["git", "worktree", "remove", "--force", str(worktree)])

    print(f"Report: {out / 'LOCAL_REPORT.md'}")
    print(f"Candidate medians: {out / 'phase26_candidate_medians.csv'}")
    print(f"Per-fixture work counters: {out / 'phase26_work_counters.csv'}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, ValueError) as exc:
        print(f"compiler-perf: {exc}", file=sys.stderr)
        raise SystemExit(1)
