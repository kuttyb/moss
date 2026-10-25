#!/usr/bin/env python3
"""Phase 25.4 — Repository-Wide Moss Compiler Performance Profiler.

Discovers, measures, and attributes compiler execution time and memory across
all Moss programs in the repository using the optimized C++ compiler (moss-opt).

Features:
- Discovers standalone compilation roots across packages, sdk, projects, examples, torture, and tests.
- Measures process wall time, CPU time (user/sys), peak RSS memory, and per-stage attributed times.
- Solves multi-pass time attribution explaining >= 90-95% of total compilation wall time.
- Supports filtering by category, single file, top N slowest, or all programs.
- Emits sortable CSV, JSON, and terminal report summaries.
- Opt-in Chrome Trace Event JSON generation linked per workload.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ProcessPoolExecutor, as_completed
import csv
import json
import os
from pathlib import Path
import resource
import shutil
import subprocess
import sys
import time
from typing import Any, Dict, List, Optional, Tuple

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_COMPILER = REPO_ROOT / "build/compiler/moss-opt"
RESULTS_DIR = REPO_ROOT / "benchmarks/phase25_4/cpp_baseline/results"
TRACES_DIR = RESULTS_DIR / "traces"


def get_file_lines(path: Path) -> int:
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            return sum(1 for _ in f)
    except Exception:
        return 0


def discover_programs(category_filter: Optional[str] = None) -> List[Dict[str, Any]]:
    """Discover valid compilation roots across the repository grouped by category."""
    programs: List[Dict[str, Any]] = []

    # 1. Group A: Frozen Compiler Torture Suite (54 fixtures)
    torture_dir = REPO_ROOT / "benchmarks/compiler_torture/fixtures"
    if torture_dir.exists():
        for p in sorted(torture_dir.rglob("*.moss")):
            if "modules/mg" in str(p) and p.name != "main.moss":
                continue
            is_negative = "negative" in str(p)
            workload_id = f"torture_{p.stem}"
            if "modules/mg" in str(p):
                workload_id = f"torture_{p.parent.parent.name}"
            programs.append({
                "id": workload_id,
                "category": "torture",
                "path": str(p.relative_to(REPO_ROOT)),
                "abs_path": str(p),
                "is_negative": is_negative,
                "loc": get_file_lines(p),
            })

    # 2. Packages (packages/*/src/main.moss + packages/*/tests/*.moss)
    for pkg_dir in sorted((REPO_ROOT / "packages").glob("*")):
        if not pkg_dir.is_dir():
            continue
        main_moss = pkg_dir / "src/main.moss"
        if main_moss.exists():
            programs.append({
                "id": f"pkg_{pkg_dir.name}",
                "category": "packages",
                "path": str(main_moss.relative_to(REPO_ROOT)),
                "abs_path": str(main_moss),
                "is_negative": False,
                "loc": get_file_lines(main_moss),
            })
        for test_file in sorted((pkg_dir / "tests").glob("*.moss")):
            programs.append({
                "id": f"pkg_{pkg_dir.name}_test_{test_file.stem}",
                "category": "packages",
                "path": str(test_file.relative_to(REPO_ROOT)),
                "abs_path": str(test_file),
                "is_negative": False,
                "loc": get_file_lines(test_file),
            })

    # 3. SDK (sdk/*/src/*.moss + sdk/*/tests/*.moss)
    for sdk_dir in sorted((REPO_ROOT / "sdk").glob("*")):
        if not sdk_dir.is_dir():
            continue
        for src_file in sorted((sdk_dir / "src").glob("*.moss")):
            programs.append({
                "id": f"sdk_{sdk_dir.name}_{src_file.stem}",
                "category": "sdk",
                "path": str(src_file.relative_to(REPO_ROOT)),
                "abs_path": str(src_file),
                "is_negative": False,
                "loc": get_file_lines(src_file),
            })
        for test_file in sorted((sdk_dir / "tests").glob("*.moss")):
            programs.append({
                "id": f"sdk_{sdk_dir.name}_test_{test_file.stem}",
                "category": "sdk",
                "path": str(test_file.relative_to(REPO_ROOT)),
                "abs_path": str(test_file),
                "is_negative": False,
                "loc": get_file_lines(test_file),
            })

    # 4. Projects (projects/*/src/main.moss + tests)
    for proj_dir in sorted((REPO_ROOT / "projects").glob("*")):
        if not proj_dir.is_dir():
            continue
        main_moss = proj_dir / "src/main.moss"
        if main_moss.exists():
            programs.append({
                "id": f"proj_{proj_dir.name}",
                "category": "projects",
                "path": str(main_moss.relative_to(REPO_ROOT)),
                "abs_path": str(main_moss),
                "is_negative": False,
                "loc": get_file_lines(main_moss),
            })
        for test_file in sorted((proj_dir / "tests").glob("*.moss")):
            programs.append({
                "id": f"proj_{proj_dir.name}_test_{test_file.stem}",
                "category": "projects",
                "path": str(test_file.relative_to(REPO_ROOT)),
                "abs_path": str(test_file),
                "is_negative": False,
                "loc": get_file_lines(test_file),
            })

    # 5. Examples (examples/*.moss)
    for ex in sorted((REPO_ROOT / "examples").glob("*.moss")):
        programs.append({
            "id": f"example_{ex.stem}",
            "category": "examples",
            "path": str(ex.relative_to(REPO_ROOT)),
            "abs_path": str(ex),
            "is_negative": False,
            "loc": get_file_lines(ex),
        })

    # 6. Tests (tests/*.moss - standalone regression tests)
    for t in sorted((REPO_ROOT / "tests").glob("*.moss")):
        # Skip import-only helper modules
        if t.name.startswith("_") or "helper" in t.name:
            continue
        programs.append({
            "id": f"test_{t.stem}",
            "category": "tests",
            "path": str(t.relative_to(REPO_ROOT)),
            "abs_path": str(t),
            "is_negative": "negative" in t.name,
            "loc": get_file_lines(t),
        })

    # Filter if requested
    if category_filter:
        cat_lower = category_filter.lower()
        programs = [p for p in programs if p["category"].lower() == cat_lower]

    return programs


def profile_single_program(
    program: Dict[str, Any],
    compiler: Path,
    timeout_s: float = 60.0,
    enable_trace: bool = False,
    trace_dir: Optional[Path] = None,
) -> Dict[str, Any]:
    """Execute Moss semantic check with full profiling instrumentation."""
    file_path = REPO_ROOT / program["path"]
    workload_id = program["id"]
    category = program["category"]
    is_negative = program.get("is_negative", False)

    cmd = [str(compiler), "check", str(file_path), "--json"]
    env = os.environ.copy()
    env["MOSS_PROFILE_COMPILER"] = "1"

    trace_file: Optional[Path] = None
    if enable_trace and trace_dir:
        trace_dir.mkdir(parents=True, exist_ok=True)
        trace_file = trace_dir / f"{workload_id}.json"
        env["MOSS_PROFILE_TRACE"] = str(trace_file)

    start_ns = time.perf_counter_ns()
    outcome = "success"
    exit_code = 0
    stderr_text = ""
    stdout_text = ""
    user_cpu_s = 0.0
    sys_cpu_s = 0.0
    peak_rss_kib = 0

    try:
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
            cwd=REPO_ROOT,
            text=True,
        )
        try:
            # We use os.wait4 to get precise peak RSS and CPU times
            pid, status, rusage = os.wait4(proc.pid, 0)
            exit_code = os.waitstatus_to_exitcode(status)
            stdout_text, stderr_text = proc.communicate()
            user_cpu_s = rusage.ru_utime
            sys_cpu_s = rusage.ru_stime
            peak_rss_kib = rusage.ru_maxrss
        except TimeoutError:
            proc.kill()
            stdout_text, stderr_text = proc.communicate()
            outcome = "timeout"
            exit_code = -1
    except subprocess.TimeoutExpired:
        outcome = "timeout"
        exit_code = -1
    except Exception as exc:
        outcome = "error"
        stderr_text = str(exc)
        exit_code = -1

    duration_ns = time.perf_counter_ns() - start_ns
    wall_time_ms = duration_ns / 1_000_000.0

    if outcome != "timeout":
        if exit_code == 0:
            outcome = "success"
        else:
            outcome = "expected_negative" if is_negative else "failure"

    # Parse stage timers and work counters
    stage_totals: Dict[str, float] = {}
    stage_counts: Dict[str, int] = {}
    counters: Dict[str, int] = {}

    for line in stderr_text.splitlines():
        line = line.strip()
        if line.startswith("MOSS_PROFILE_TOTAL|"):
            parts = line.split("|")
            if len(parts) >= 4:
                stage = parts[1]
                total_ns = int(parts[2])
                calls = int(parts[3])
                stage_totals[stage] = total_ns / 1_000_000.0
                stage_counts[stage] = calls
        elif line.startswith("MOSS_PROFILE|"):
            parts = line.split("|")
            if len(parts) == 3:
                key, val = parts[1], parts[2]
                try:
                    num = int(val)
                    if key in ("functions_analyzed", "types_analyzed", "specialization_requests",
                                "distinct_specialization_keys", "actual_specialization_checks",
                                "checked_specializations", "reused_specialization_results",
                                "inference_rounds", "inference_work_units"):
                        counters[key] = num
                    else:
                        # Fallback accumulation if MOSS_PROFILE_TOTAL is absent
                        if key not in stage_totals:
                            stage_totals[key] = stage_totals.get(key, 0.0) + (num / 1_000_000.0)
                            stage_counts[key] = stage_counts.get(key, 0) + 1
                except ValueError:
                    pass

    # Major operation times (ms)
    semantic_ms = stage_totals.get("semantic_analysis", 0.0)
    type_infer_ms = stage_totals.get("type_inference", 0.0)
    effects_ms = stage_totals.get("effects", 0.0)
    spec_check_ms = stage_totals.get("specialization_checking", 0.0)
    sync_plan_ms = stage_totals.get("synchronization_plan", 0.0)
    parsing_ms = stage_totals.get("parsing", 0.0)
    module_res_ms = stage_totals.get("module_resolution", 0.0)
    decl_ms = stage_totals.get("declaration_and_symbol_analysis", 0.0)
    snapshot_ms = stage_totals.get("semantic_snapshot_generation", 0.0)
    opt_ms = stage_totals.get("functional_optimizer", 0.0)
    rust_gen_ms = stage_totals.get("rust_generation", 0.0)
    discovery_ms = stage_totals.get("project_discovery", 0.0)

    # Attribution calculation:
    # Top-level non-overlapping stages are:
    # If snapshot_ms > 0:
    #   Pass 1 semantic + Pass 1 non-semantic + snapshot_ms
    # Otherwise:
    #   semantic_ms + parsing_ms + module_res_ms + decl_ms + opt_ms + rust_gen_ms + discovery_ms
    if snapshot_ms > 0:
        # Pass 1 semantic is approximately semantic_ms - snapshot's semantic checking
        # But snapshot_ms encompasses Pass 2.
        # Direct non-overlapping top-level components:
        attributed_ms = discovery_ms + parsing_ms + module_res_ms + decl_ms + opt_ms + semantic_ms + rust_gen_ms
        if attributed_ms > wall_time_ms:
            attributed_ms = min(wall_time_ms, semantic_ms + parsing_ms + module_res_ms + decl_ms + opt_ms)
    else:
        attributed_ms = discovery_ms + parsing_ms + module_res_ms + decl_ms + opt_ms + semantic_ms + rust_gen_ms

    attributed_ms = min(wall_time_ms, attributed_ms)
    unattributed_ms = max(0.0, wall_time_ms - attributed_ms)
    attribution_pct = (attributed_ms / wall_time_ms * 100.0) if wall_time_ms > 0 else 100.0

    return {
        "id": workload_id,
        "category": category,
        "path": program["path"],
        "loc": program.get("loc", 0),
        "outcome": outcome,
        "exit_code": exit_code,
        "wall_time_ms": round(wall_time_ms, 2),
        "user_cpu_ms": round(user_cpu_s * 1000.0, 2),
        "sys_cpu_ms": round(sys_cpu_s * 1000.0, 2),
        "peak_rss_kib": peak_rss_kib,
        "attributed_time_ms": round(attributed_ms, 2),
        "unattributed_ms": round(unattributed_ms, 2),
        "attribution_pct": round(attribution_pct, 1),
        "semantic_analysis_ms": round(semantic_ms, 2),
        "type_inference_ms": round(type_infer_ms, 2),
        "effects_ms": round(effects_ms, 2),
        "specialization_checking_ms": round(spec_check_ms, 2),
        "synchronization_plan_ms": round(sync_plan_ms, 2),
        "parsing_ms": round(parsing_ms, 2),
        "module_resolution_ms": round(module_res_ms, 2),
        "declaration_ms": round(decl_ms, 2),
        "snapshot_ms": round(snapshot_ms, 2),
        "functional_optimizer_ms": round(opt_ms, 2),
        "rust_generation_ms": round(rust_gen_ms, 2),
        "trace_file": str(trace_file.relative_to(REPO_ROOT)) if trace_file and trace_file.exists() else None,
        "counters": counters,
    }


def run_profiler(
    programs: List[Dict[str, Any]],
    compiler: Path,
    timeout_s: float = 60.0,
    parallel_jobs: int = 4,
    enable_trace: bool = False,
    trace_dir: Optional[Path] = None,
) -> List[Dict[str, Any]]:
    """Profile a cohort of programs, sequentially or in parallel."""
    results: List[Dict[str, Any]] = []
    total = len(programs)

    print(f"Starting Moss profiling on {total} programs (parallel jobs: {parallel_jobs}, timeout: {timeout_s}s)...")

    if parallel_jobs > 1 and total > 1:
        with ProcessPoolExecutor(max_workers=parallel_jobs) as executor:
            future_to_prog = {
                executor.submit(
                    profile_single_program, prog, compiler, timeout_s, enable_trace, trace_dir
                ): prog
                for prog in programs
            }
            completed = 0
            for future in as_completed(future_to_prog):
                res = future.result()
                results.append(res)
                completed += 1
                if completed % 10 == 0 or completed == total:
                    print(f"  [{completed}/{total}] {res['id']:<35} {res['wall_time_ms']:>8.2f} ms ({res['outcome']})")
    else:
        for idx, prog in enumerate(programs, 1):
            res = profile_single_program(prog, compiler, timeout_s, enable_trace, trace_dir)
            results.append(res)
            print(f"  [{idx}/{total}] {res['id']:<35} {res['wall_time_ms']:>8.2f} ms ({res['outcome']})")

    return results


def print_summary_table(results: List[Dict[str, Any]], top_n: int = 20) -> None:
    """Print readable terminal summary ordered by compilation wall time."""
    sorted_by_wall = sorted(results, key=lambda x: x["wall_time_ms"], reverse=True)

    print("\n" + "=" * 115)
    print(f"TOP {min(top_n, len(sorted_by_wall))} SLOWEST MOSS COMPILATIONS (Ordered by Wall Time Descending)")
    print("=" * 115)
    header = (
        f"{'Workload ID':<32} {'Category':<10} {'Wall (s)':>10} {'Semantic':>10} "
        f"{'TypeInfer':>10} {'Effects':>9} {'RSS (KiB)':>10} {'Attrib %':>9} {'Outcome':<10}"
    )
    print(header)
    print("-" * 115)

    for r in sorted_by_wall[:top_n]:
        wall_s = r["wall_time_ms"] / 1000.0
        sem_s = r["semantic_analysis_ms"] / 1000.0
        ti_s = r["type_inference_ms"] / 1000.0
        eff_s = r["effects_ms"] / 1000.0
        print(
            f"{r['id']:<32} {r['category']:<10} {wall_s:>10.2f} {sem_s:>10.2f} "
            f"{ti_s:>10.2f} {eff_s:>9.2f} {r['peak_rss_kib']:>10} {r['attribution_pct']:>8.1f}% {r['outcome']:<10}"
        )
    print("=" * 115)

    # Top unattributed residuals
    sorted_by_unattributed = sorted(results, key=lambda x: x["unattributed_ms"], reverse=True)
    slow_residuals = [r for r in sorted_by_unattributed if r["unattributed_ms"] > 10.0]
    if slow_residuals:
        print("\n" + "-" * 85)
        print("LARGEST UNATTRIBUTED RESIDUALS (> 10 ms)")
        print("-" * 85)
        print(f"{'Workload ID':<35} {'Wall (ms)':>12} {'Attributed (ms)':>16} {'Residual (ms)':>14} {'Attrib %':>9}")
        print("-" * 85)
        for r in slow_residuals[:10]:
            print(
                f"{r['id']:<35} {r['wall_time_ms']:>12.2f} {r['attributed_time_ms']:>16.2f} "
                f"{r['unattributed_ms']:>14.2f} {r['attribution_pct']:>8.1f}%"
            )
        print("-" * 85)

    # Category summaries
    categories = sorted(set(r["category"] for r in results))
    print("\n" + "-" * 85)
    print("CATEGORY PERFORMANCE SUMMARY")
    print("-" * 85)
    print(f"{'Category':<15} {'Count':>8} {'Total Wall (s)':>16} {'Avg Wall (ms)':>15} {'Mean Attrib %':>15} {'Success':>12}")
    print("-" * 85)
    for cat in categories:
        cat_rows = [r for r in results if r["category"] == cat]
        total_wall_s = sum(r["wall_time_ms"] for r in cat_rows) / 1000.0
        avg_wall_ms = (sum(r["wall_time_ms"] for r in cat_rows) / len(cat_rows)) if cat_rows else 0.0
        mean_attrib = (sum(r["attribution_pct"] for r in cat_rows) / len(cat_rows)) if cat_rows else 0.0
        success_cnt = sum(1 for r in cat_rows if r["outcome"] in ("success", "expected_negative"))
        print(f"{cat:<15} {len(cat_rows):>8} {total_wall_s:>16.2f} {avg_wall_ms:>15.1f} {mean_attrib:>14.1f}% {success_cnt:>7}/{len(cat_rows)}")
    print("-" * 85 + "\n")


def write_csv_report(results: List[Dict[str, Any]], csv_path: Path) -> None:
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "id", "category", "path", "loc", "outcome", "exit_code",
        "wall_time_ms", "user_cpu_ms", "sys_cpu_ms", "peak_rss_kib",
        "attributed_time_ms", "unattributed_ms", "attribution_pct",
        "semantic_analysis_ms", "type_inference_ms", "effects_ms",
        "specialization_checking_ms", "synchronization_plan_ms",
        "parsing_ms", "module_resolution_ms", "declaration_ms",
        "snapshot_ms", "functional_optimizer_ms", "rust_generation_ms",
        "trace_file"
    ]
    with open(csv_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        for r in sorted(results, key=lambda x: x["wall_time_ms"], reverse=True):
            writer.writerow(r)
    print(f"Exported CSV report: {csv_path}")


def write_json_report(results: List[Dict[str, Any]], json_path: Path) -> None:
    json_path.parent.mkdir(parents=True, exist_ok=True)
    report = {
        "report_version": "1.0.0",
        "created_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "total_programs_profiled": len(results),
        "total_wall_time_s": round(sum(r["wall_time_ms"] for r in results) / 1000.0, 2),
        "mean_attribution_pct": round(sum(r["attribution_pct"] for r in results) / len(results), 2) if results else 0.0,
        "results": sorted(results, key=lambda x: x["wall_time_ms"], reverse=True),
    }
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)
    print(f"Exported JSON report: {json_path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Moss Repository-Wide Performance Profiler")
    parser.add_argument("--compiler", default=str(DEFAULT_COMPILER), help="Path to moss-opt executable")
    parser.add_argument("--all", action="store_true", help="Profile all discovered Moss programs across repository")
    parser.add_argument("--category", choices=["torture", "packages", "sdk", "projects", "examples", "tests"],
                        help="Profile a specific category of programs")
    parser.add_argument("--file", help="Profile a specific single Moss source file")
    parser.add_argument("--top", type=int, default=20, help="Number of slowest compilations to display (default: 20)")
    parser.add_argument("--timeout", type=float, default=60.0, help="Per-program timeout in seconds (default: 60)")
    parser.add_argument("--jobs", "-j", type=int, default=4, help="Parallel execution jobs (default: 4)")
    parser.add_argument("--trace", action="store_true", help="Generate Chrome Trace Event JSON into results/traces/")
    parser.add_argument("--csv", default=str(RESULTS_DIR / "moss_inventory_perf.csv"), help="Output CSV path")
    parser.add_argument("--json", default=str(RESULTS_DIR / "moss_inventory_perf.json"), help="Output JSON path")
    args = parser.parse_args()

    compiler = Path(args.compiler).resolve()
    if not compiler.exists():
        sys.exit(f"Compiler executable not found: {compiler}")

    trace_dir = TRACES_DIR if args.trace else None

    # Determine workload list
    if args.file:
        file_path = Path(args.file)
        if not file_path.is_absolute():
            file_path = (REPO_ROOT / file_path).resolve()
        if not file_path.exists():
            sys.exit(f"Specified file not found: {file_path}")
        rel_str = str(file_path.relative_to(REPO_ROOT))
        cat = rel_str.split("/")[0] if "/" in rel_str else "standalone"
        programs = [{
            "id": file_path.stem,
            "category": cat,
            "path": rel_str,
            "abs_path": str(file_path),
            "is_negative": "negative" in str(file_path),
            "loc": get_file_lines(file_path),
        }]
    else:
        programs = discover_programs(category_filter=args.category)
        if not programs:
            sys.exit(f"No programs discovered for category: {args.category}")

    # Run profiling
    jobs = 1 if len(programs) == 1 else args.jobs
    results = run_profiler(
        programs,
        compiler,
        timeout_s=args.timeout,
        parallel_jobs=jobs,
        enable_trace=args.trace,
        trace_dir=trace_dir,
    )

    # Output reports
    print_summary_table(results, top_n=args.top)
    if args.csv:
        write_csv_report(results, Path(args.csv))
    if args.json:
        write_json_report(results, Path(args.json))


if __name__ == "__main__":
    main()
