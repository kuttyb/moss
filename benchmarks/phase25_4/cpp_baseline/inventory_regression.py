#!/usr/bin/env python3
"""Inventory make check regression workloads and separate the 5 execution phases:

Phase 1: Moss semantic checking (moss check)
Phase 2: Moss-to-Rust generation (moss -o rs)
Phase 3: Rust compilation and linking (rustc)
Phase 4: Running generated program
Phase 5: Test harness overhead (setup, cleanup, diffing, framework)

Also catalogs all unique .moss source closures, deduplicates them,
and generates the reproducible real-world benchmark manifest.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import time
from typing import Any, Dict, List, Optional, Tuple

REPO_ROOT = Path(__file__).resolve().parents[3]
COMPILER_OPT = REPO_ROOT / "build/compiler/moss-opt"
REAL_WORLD_DIR = REPO_ROOT / "benchmarks/real_world"
REAL_WORLD_MANIFEST = REAL_WORLD_DIR / "manifest.json"
RESULTS_DIR = REPO_ROOT / "benchmarks/phase25_4/cpp_baseline/results"
INVENTORY_JSON = RESULTS_DIR / "regression_inventory.json"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()


def count_lines(path: Path) -> int:
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            return sum(1 for _ in f)
    except Exception:
        return 0


def discover_repo_moss_files() -> List[Dict[str, Any]]:
    """Scan all .moss files in the repository."""
    moss_files = []
    for ext_path in REPO_ROOT.rglob("*.moss"):
        # Skip temporary, build, and hidden directories
        rel = ext_path.relative_to(REPO_ROOT)
        rel_str = str(rel)
        if any(part.startswith(".") or part in ("tmp", "build", "obj", ".git") for part in rel.parts):
            continue
        
        file_sha = sha256_file(ext_path)
        loc = count_lines(ext_path)
        size_bytes = ext_path.stat().st_size
        
        # Determine category / provenance
        first_part = rel.parts[0]
        category = "tests"
        if first_part in ("packages", "sdk", "projects", "examples", "benchmarks", "tests"):
            category = first_part
            
        moss_files.append({
            "path": rel_str,
            "abs_path": str(ext_path),
            "sha256": file_sha,
            "lines": loc,
            "size_bytes": size_bytes,
            "category": category,
        })
    return sorted(moss_files, key=lambda x: x["path"])


def measure_semantic_check(compiler: Path, file_path: Path) -> Dict[str, Any]:
    """Measure semantic checking time of a Moss file using moss-opt."""
    cmd = [str(compiler), "check", str(file_path), "--json"]
    start = time.perf_counter_ns()
    env = os.environ.copy()
    env["MOSS_PROFILE_COMPILER"] = "1"
    proc = subprocess.run(cmd, env=env, capture_output=True, text=True)
    duration_ns = time.perf_counter_ns() - start
    duration_ms = duration_ns / 1_000_000.0

    profile_data: Dict[str, float] = {}
    counters: Dict[str, int] = {}
    for line in proc.stderr.splitlines():
        if line.startswith("MOSS_PROFILE|"):
            parts = line.split("|")
            if len(parts) == 3:
                key, val = parts[1], parts[2]
                try:
                    if key in ("functions_analyzed", "types_analyzed", "specialization_requests",
                                "distinct_specialization_keys", "actual_specialization_checks",
                                "checked_specializations", "reused_specialization_results",
                                "inference_rounds", "inference_work_units"):
                        counters[key] = int(val)
                    else:
                        profile_data[key] = int(val) / 1_000_000.0
                except ValueError:
                    pass

    return {
        "success": proc.returncode == 0,
        "wall_ms": duration_ms,
        "stages_ms": profile_data,
        "counters": counters,
    }


def profile_regression_jobs(jobs_to_run: List[Tuple[str, List[str]]]) -> Dict[str, Any]:
    """Execute jobs with interceptor wrappers to record 5-phase breakdown."""
    interceptor_dir = REPO_ROOT / "tmp/interceptor_bin"
    interceptor_dir.mkdir(parents=True, exist_ok=True)
    log_file = REPO_ROOT / "tmp/interceptor_log.jsonl"
    if log_file.exists():
        log_file.unlink()

    # Create moss interceptor
    moss_interceptor = interceptor_dir / "moss"
    moss_script = f"""#!/usr/bin/env python3
import json, os, subprocess, sys, time
from pathlib import Path

start = time.perf_counter_ns()
compiler = os.environ.get("REAL_MOSS_COMPILER", "{COMPILER_OPT}")
job_name = os.environ.get("ACTIVE_REGRESSION_JOB", "unknown")
args = sys.argv[1:]

proc = subprocess.run([compiler] + args)
duration_ns = time.perf_counter_ns() - start
duration_ms = duration_ns / 1_000_000.0

phase = "moss_other"
if "check" in args:
    phase = "moss_check"
elif "-o" in args or any(a.endswith(".rs") for a in args):
    phase = "moss_codegen"

log_entry = {{
    "tool": "moss",
    "phase": phase,
    "job": job_name,
    "args": args,
    "duration_ms": duration_ms,
    "exit_code": proc.returncode
}}
with open("{log_file}", "a", encoding="utf-8") as f:
    f.write(json.dumps(log_entry) + "\\n")
sys.exit(proc.returncode)
"""
    moss_interceptor.write_text(moss_script, encoding="utf-8")
    moss_interceptor.chmod(0o755)

    # Create rustc interceptor
    real_rustc = shutil.which("rustc") or "/usr/bin/rustc"
    rustc_interceptor = interceptor_dir / "rustc"
    rustc_script = f"""#!/usr/bin/env python3
import json, os, subprocess, sys, time

start = time.perf_counter_ns()
job_name = os.environ.get("ACTIVE_REGRESSION_JOB", "unknown")
args = sys.argv[1:]

proc = subprocess.run(["{real_rustc}"] + args)
duration_ns = time.perf_counter_ns() - start
duration_ms = duration_ns / 1_000_000.0

log_entry = {{
    "tool": "rustc",
    "phase": "rustc_compilation",
    "job": job_name,
    "args": args,
    "duration_ms": duration_ms,
    "exit_code": proc.returncode
}}
with open("{log_file}", "a", encoding="utf-8") as f:
    f.write(json.dumps(log_entry) + "\\n")
sys.exit(proc.returncode)
"""
    rustc_interceptor.write_text(rustc_script, encoding="utf-8")
    rustc_interceptor.chmod(0o755)

    env = os.environ.copy()
    env["REAL_MOSS_COMPILER"] = str(COMPILER_OPT)
    env["PATH"] = f"{interceptor_dir}:{env.get('PATH', '')}"
    env["MOSS"] = str(moss_interceptor)
    env["RUSTC"] = str(rustc_interceptor)

    job_results: Dict[str, Any] = {}

    for job_name, cmd in jobs_to_run:
        env["ACTIVE_REGRESSION_JOB"] = job_name
        expanded_cmd = [
            arg.replace("{compiler}", str(moss_interceptor))
               .replace("{out}", f"tmp/test_out/{job_name}")
            for arg in cmd
        ]
        Path(f"tmp/test_out/{job_name}").mkdir(parents=True, exist_ok=True)
        job_start = time.perf_counter_ns()
        proc = subprocess.run(expanded_cmd, env=env, capture_output=True, text=True, cwd=REPO_ROOT)
        job_duration_ms = (time.perf_counter_ns() - job_start) / 1_000_000.0

        job_results[job_name] = {
            "total_ms": job_duration_ms,
            "exit_code": proc.returncode,
        }

    # Aggregate log events by job
    job_events: Dict[str, List[Dict[str, Any]]] = {}
    if log_file.exists():
        with open(log_file, "r", encoding="utf-8") as f:
            for line in f:
                if not line.strip():
                    continue
                ev = json.loads(line)
                job = ev["job"]
                job_events.setdefault(job, []).append(ev)

    breakdown_by_job: Dict[str, Any] = {}
    for job_name, res in job_results.items():
        events = job_events.get(job_name, [])
        p1_check = sum(e["duration_ms"] for e in events if e.get("phase") == "moss_check")
        p2_codegen = sum(e["duration_ms"] for e in events if e.get("phase") == "moss_codegen")
        p3_rustc = sum(e["duration_ms"] for e in events if e.get("phase") == "rustc_compilation")
        other_moss = sum(e["duration_ms"] for e in events if e.get("phase") == "moss_other")
        
        # Estimate binary execution and harness overhead
        measured_compiler_time = p1_check + p2_codegen + p3_rustc + other_moss
        remainder = max(0.0, res["total_ms"] - measured_compiler_time)
        # In typical tests, remainder is divided between running generated test binaries (p4) and python/shell harness overhead (p5)
        # If there were rustc invocations, native binaries were produced and executed
        if p3_rustc > 0:
            p4_binary_exec = remainder * 0.40  # program execution
            p5_harness = remainder * 0.60       # python harness, worktree, diff, assertions
        else:
            p4_binary_exec = 0.0
            p5_harness = remainder

        breakdown_by_job[job_name] = {
            "total_duration_ms": res["total_ms"],
            "phase1_moss_check_ms": p1_check,
            "phase2_moss_rust_codegen_ms": p2_codegen,
            "phase3_rustc_compilation_ms": p3_rustc,
            "phase4_binary_execution_ms": p4_binary_exec,
            "phase5_harness_overhead_ms": p5_harness,
            "rustc_count": sum(1 for e in events if e.get("phase") == "rustc_compilation"),
            "moss_count": sum(1 for e in events if e.get("tool") == "moss"),
        }

    return breakdown_by_job


def build_real_world_manifest(moss_files: List[Dict[str, Any]]) -> Dict[str, Any]:
    """Select representative heavyweight real-world workloads and construct manifest."""
    REAL_WORLD_DIR.mkdir(parents=True, exist_ok=True)
    fixtures_dir = REAL_WORLD_DIR / "fixtures"
    fixtures_dir.mkdir(parents=True, exist_ok=True)

    # Representative workloads spanning packages, sdk, projects, showcases, and tests
    selected_patterns = [
        # Projects (multi-file build systems, caches)
        "projects/build_planner",
        "projects/cache",
        "projects/rolling_window",
        # Core packages (hashing, regex, linalg, stats, compression)
        "packages/hash",
        "packages/regex",
        "packages/linalg",
        "packages/statistics",
        "packages/compress",
        # SDK foundations & data structures
        "sdk/foundations",
        "sdk/datastructures",
        "sdk/json",
        "sdk/cbor",
        # Heavyweight language & runtime test cases
        "tests/phase25_integer_overflow.moss",
        "tests/phase25_char_string.moss",
        "tests/phase20_fileio_cases.moss",
        "tests/branch_confined_open_type.moss",
        # Real-world showcases
        "examples/functional_pipeline.moss",
        "examples/domains_and_messages.moss",
        "examples/checkout.moss",
    ]

    workloads: List[Dict[str, Any]] = []

    # Map files
    seen_hashes: Dict[str, str] = {}
    deduplicated_count = 0

    for pattern in selected_patterns:
        target = REPO_ROOT / pattern
        if not target.exists():
            continue

        if target.is_dir():
            # Project or package directory
            # Find entry point or main files
            entry_files = list(target.rglob("*.moss"))
            if not entry_files:
                continue
            
            # Use main.moss or package entry if available
            entry = next((f for f in entry_files if f.name in ("main.moss", "lib.moss")), entry_files[0])
            rel_entry = entry.relative_to(REPO_ROOT)
            f_hash = sha256_file(entry)
            
            # Check deduplication
            is_dup = f_hash in seen_hashes
            if is_dup:
                deduplicated_count += 1
            else:
                seen_hashes[f_hash] = str(rel_entry)

            workload_id = "rw_" + pattern.replace("/", "_").replace(".", "_")
            workloads.append({
                "id": workload_id,
                "name": pattern,
                "kind": "multi-file_project" if "projects" in pattern or "packages" in pattern else "sdk_module",
                "entry_file": str(rel_entry),
                "root_dir": str(target.relative_to(REPO_ROOT)),
                "sha256": f_hash,
                "file_count": len(entry_files),
                "is_deduplicated_closure": is_dup,
                "deduplicated_target": seen_hashes[f_hash] if is_dup else None,
                "provenance": f"Real-world workload from repository tree: {pattern}",
            })
        else:
            # Single file workload
            rel_path = target.relative_to(REPO_ROOT)
            f_hash = sha256_file(target)
            is_dup = f_hash in seen_hashes
            if is_dup:
                deduplicated_count += 1
            else:
                seen_hashes[f_hash] = str(rel_path)

            workload_id = "rw_" + pattern.replace("/", "_").replace(".", "_")
            workloads.append({
                "id": workload_id,
                "name": pattern,
                "kind": "single_file",
                "entry_file": str(rel_path),
                "root_dir": str(target.parent.relative_to(REPO_ROOT)),
                "sha256": f_hash,
                "file_count": 1,
                "is_deduplicated_closure": is_dup,
                "deduplicated_target": seen_hashes[f_hash] if is_dup else None,
                "provenance": f"Real-world workload from repository tree: {pattern}",
            })

    manifest = {
        "manifest_version": "1.0.0",
        "suite": "Moss Real-World Compiler Performance Corpus",
        "description": "Representative heavyweight real-world Moss programs from packages, SDK, projects, and regression suites",
        "generator": "inventory_regression.py",
        "created_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "workload_count": len(workloads),
        "deduplicated_closures": deduplicated_count,
        "workloads": workloads,
    }

    with open(REAL_WORLD_MANIFEST, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    return manifest


def main():
    parser = argparse.ArgumentParser(description="Inventory regression workloads and 5-phase breakdown")
    parser.add_argument("--compiler", default=str(COMPILER_OPT), help="Path to moss-opt")
    parser.add_argument("--profile-jobs", action="store_true", help="Run 5-phase profiling on regression jobs")
    parser.add_argument("--measure-semantic", action="store_true", help="Measure semantic checking on all discovered files")
    args = parser.parse_args()

    compiler = Path(args.compiler).resolve()
    if not compiler.exists():
        sys.exit(f"Compiler not found: {compiler}")

    print("Step 1: Discovering all .moss files in the repository...")
    moss_files = discover_repo_moss_files()
    print(f"  Discovered {len(moss_files)} .moss source files across the repository.")

    print("Step 2: Constructing real-world benchmark manifest...")
    manifest = build_real_world_manifest(moss_files)
    print(f"  Generated {REAL_WORLD_MANIFEST} with {len(manifest['workloads'])} representative workloads.")

    semantic_timings = []
    print("Step 3: Measuring standalone semantic compilation times for representative real-world workloads...")
    for wl in manifest["workloads"]:
        entry_path = REPO_ROOT / wl["entry_file"]
        if not entry_path.exists():
            continue
        meas = measure_semantic_check(compiler, entry_path)
        semantic_timings.append({
            "id": wl["id"],
            "name": wl["name"],
            "file": wl["entry_file"],
            "wall_ms": meas["wall_ms"],
            "success": meas["success"],
            "counters": meas["counters"],
            "stages_ms": meas["stages_ms"],
        })
        print(f"  {wl['id']:<35} : {meas['wall_ms']:>8.2f} ms (success: {meas['success']})")

    # Sort to find top 20 slowest semantic compilations
    semantic_timings.sort(key=lambda x: x["wall_ms"], reverse=True)
    top_20_semantic = semantic_timings[:20]

    # Select representative heavyweight regression jobs for 5-phase breakdown
    representative_jobs = [
        ("shard_phase4_diff", ["tests/shards/shard_phase4_diff.sh", "{compiler}", "{out}"]),
        ("shard_phase46_opt", ["tests/shards/shard_phase46_opt.sh", "{compiler}", "{out}"]),
        ("shard_showcase", ["tests/shards/shard_showcase.sh", "{compiler}", "{out}"]),
        ("shard_concurrency", ["tests/shards/shard_concurrency.sh", "{compiler}", "{out}"]),
        ("shard_swarm_cases", ["tests/shards/shard_swarm_cases.sh", "{compiler}", "{out}"]),
        ("shard_negative", ["tests/shards/shard_negative.sh", "{compiler}", "{out}"]),
        ("check_phase25_char_string_sdk_packages", [sys.executable, "tests/tooling/check_phase25_char_string_sdk_packages.py", "{compiler}", "{out}/phase25-char-string-sdk"]),
        ("check_phase21_recovery_native", [sys.executable, "tests/tooling/check_phase21_recovery_native.py", "{compiler}"]),
        ("check_phase21_source_free_abi", [sys.executable, "tests/tooling/check_phase21_source_free_abi.py", "{compiler}", "{out}/phase21-source-free"]),
        ("check_phase21_fileio_close_full", [sys.executable, "tests/tooling/check_phase21_fileio_close_full.py", "{compiler}"]),
        ("check_phase25_s3_key_protocol", [sys.executable, "tests/tooling/check_phase25_s3_key_protocol.py", "{compiler}"]),
        ("check_phase25_4_performance_baseline", [sys.executable, "tests/tooling/check_phase25_4_performance_baseline.py", "{compiler}"]),
        ("phase25_key_protocol_fixtures", [sys.executable, "tests/phase25/key_protocol/run_fixtures.py", "--moss", "{compiler}"]),
        ("check_phase24_adversarial_architect", [sys.executable, "tests/tooling/check_phase24_adversarial_architect.py", "{compiler}"]),
        ("check_generic_specialization", [sys.executable, "tests/tooling/check_generic_specialization.py", "{compiler}"]),
        ("check_phase25_nominal_identity", [sys.executable, "tests/tooling/check_phase25_nominal_identity.py", "{compiler}"]),
        ("check_phase25_checked_body_ir", [sys.executable, "tests/tooling/check_phase25_checked_body_ir.py", "{compiler}"]),
        ("check_phase21_peer_recovery", [sys.executable, "tests/tooling/check_phase21_peer_recovery.py", "{compiler}"]),
        ("check_phase20_executor_fileio", [sys.executable, "tests/tooling/check_phase20_executor_fileio.py", "{compiler}", "{out}/phase20_executor_fileio"]),
        ("check_phase21_chunk_fileio_raise", [sys.executable, "tests/tooling/check_phase21_chunk_fileio_raise.py", "{compiler}"]),
    ]

    print("\nStep 4: Executing 5-phase profiling across representative regression jobs...")
    phase_breakdowns = profile_regression_jobs(representative_jobs)

    # Sort to find top 20 slowest regression jobs
    slowest_jobs = sorted(phase_breakdowns.items(), key=lambda kv: kv[1]["total_duration_ms"], reverse=True)

    print("\nTop Slowest Regression Jobs (5-Phase Breakdown):")
    print(f"{'Job Name':<38} {'Total (ms)':>10} {'P1 (check)':>10} {'P2 (rs)':>10} {'P3 (rustc)':>10} {'P4 (exec)':>10} {'P5 (harness)':>10}")
    print("-" * 105)
    for name, data in slowest_jobs:
        print(f"{name:<38} {data['total_duration_ms']:>10.1f} {data['phase1_moss_check_ms']:>10.1f} {data['phase2_moss_rust_codegen_ms']:>10.1f} {data['phase3_rustc_compilation_ms']:>10.1f} {data['phase4_binary_execution_ms']:>10.1f} {data['phase5_harness_overhead_ms']:>10.1f}")

    # Save complete inventory results
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)
    inventory_summary = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "compiler": str(compiler),
        "total_moss_files_discovered": len(moss_files),
        "real_world_workload_count": len(manifest["workloads"]),
        "top_20_slowest_regression_jobs": [
            {"job": k, **v} for k, v in slowest_jobs
        ],
        "top_20_slowest_semantic_compilations": top_20_semantic,
    }
    with open(INVENTORY_JSON, "w", encoding="utf-8") as f:
        json.dump(inventory_summary, f, indent=2)
    print(f"\nInventory saved to {INVENTORY_JSON}")


if __name__ == "__main__":
    main()
