#!/usr/bin/env python3
"""Reproducible, isolated build measurements for the Phase 25.1 compiler.

All source inputs are exported from --revision into a disposable copy under
<repo>/tmp. The checked-out worktree is never touched by benchmark actions.
No compiler cache wrapper is used; ccache/sccache are explicitly disabled.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import threading
import tarfile
import time
from datetime import datetime, timezone
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
REVISION_DEFAULT = "2420439cf1cbaa08c8cd45d0d77b436ea4fb43f0"
RUNS_DEFAULT = 5
PROFILES = {
    "O0": {"target": "moss-dev", "binary": "build/compiler/moss-dev", "flags": "-std=c++17 -Wall -Wextra -pedantic -O0"},
    "O2": {"target": "moss-opt", "binary": "build/compiler/moss-opt", "flags": "-std=c++17 -Wall -Wextra -pedantic -O2"},
}
TIME_BIN = Path("/usr/bin/time")
TIME_FORMAT = "WALL=%e\nUSER=%U\nSYS=%S\nRSS_KB=%M\nEXIT=%x"
COMPILER_COMMANDS = {"g++", "cc1plus", "cc1", "moss-opt", "moss-dev", "clang++",
                     "clang", "lto1", "collect2", "ld", "ld.lld"}


def run(cmd: list[str], cwd: Path, env: dict[str, str], check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, env=env, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=check)


def clean_env() -> dict[str, str]:
    env = os.environ.copy()
    for key in ("CCACHE_DIR", "SCCACHE_DIR", "RUSTC_WRAPPER", "CXX", "CXXFLAGS", "MAKEFLAGS"):
        env.pop(key, None)
    env["CCACHE_DISABLE"] = "1"
    env["SCCACHE_DISABLE"] = "1"
    env["TMPDIR"] = str(REPO / "tmp")
    env["LC_ALL"] = "C"
    return env


def compiler_snapshot(except_pgid: int | None = None) -> list[dict[str, object]]:
    try:
        output = subprocess.run(["ps", "-eo", "pid,ppid,pgid,pcpu,comm,args", "--no-headers"],
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                check=True).stdout
    except (FileNotFoundError, subprocess.CalledProcessError):
        return [{"error": "ps unavailable"}]
    records: list[dict[str, object]] = []
    for line in output.splitlines():
        parts = line.strip().split(None, 5)
        if len(parts) != 6:
            continue
        pid, ppid, pgid, cpu, comm, args = parts
        if comm in COMPILER_COMMANDS and (except_pgid is None or int(pgid) != except_pgid):
            records.append({"pid": int(pid), "ppid": int(ppid), "pgid": int(pgid),
                            "cpu_percent": float(cpu), "command": comm, "args": args})
    return records


def machine_info(revision: str) -> dict[str, object]:
    gxx = run(["g++", "--version"], REPO, clean_env()).stdout.splitlines()[0]
    uname = platform.uname()
    try:
        cpu = run(["lscpu", "-J"], REPO, clean_env()).stdout
        cpu_info = json.loads(cpu)
    except (FileNotFoundError, subprocess.CalledProcessError, json.JSONDecodeError):
        cpu_info = None
    try:
        processes = run(["ps", "-eo", "pid,pcpu,comm", "--sort=-pcpu"], REPO, clean_env()).stdout.splitlines()[:9]
    except (FileNotFoundError, subprocess.CalledProcessError):
        processes = None
    return {
        "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        "revision": revision,
        "compiler": gxx,
        "compiler_path": shutil.which("g++"),
        "platform": platform.platform(),
        "uname": {"system": uname.system, "release": uname.release,
                  "machine": uname.machine, "processor": uname.processor},
        "cpu_count_online": os.cpu_count(),
        "load_average_before": os.getloadavg() if hasattr(os, "getloadavg") else None,
        "top_processes_before": processes,
        "compiler_processes_before": compiler_snapshot(),
        "cpu_details_lscpu": cpu_info,
        "parallelism": 1,
        "cache_policy": "CCACHE_DISABLE=1 and SCCACHE_DISABLE=1; compiler invoked directly as g++",
        "time_tool": str(TIME_BIN),
        "profiles": PROFILES,
    }


def export_tree(destination: Path, revision: str, env: dict[str, str]) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(["git", "archive", "--format=tar", revision], cwd=REPO,
                            env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors="replace"))
    import io
    with tarfile.open(fileobj=io.BytesIO(result.stdout), mode="r:") as archive:
        archive.extractall(destination)


def timed(command: list[str], cwd: Path, env: dict[str, str], log_path: Path) -> dict[str, object]:
    time_path = cwd / "tmp" / "time.txt"
    time_path.parent.mkdir(parents=True, exist_ok=True)
    wrapped = [str(TIME_BIN), "-f", TIME_FORMAT, "-o", str(time_path), "--", *command]
    started_utc = datetime.now(timezone.utc).isoformat()
    load_before = os.getloadavg() if hasattr(os, "getloadavg") else None
    compiler_before = compiler_snapshot()
    started = time.perf_counter()
    proc = subprocess.Popen(wrapped, cwd=cwd, env=env, text=True, start_new_session=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    external_during: dict[int, dict[str, object]] = {}
    pgid = proc.pid
    while proc.poll() is None:
        for record in compiler_snapshot(except_pgid=pgid):
            if "pid" in record:
                external_during[int(record["pid"])] = record
        time.sleep(1.0)
    stdout, stderr = proc.communicate()
    elapsed = time.perf_counter() - started
    compiler_after = compiler_snapshot()
    load_after = os.getloadavg() if hasattr(os, "getloadavg") else None
    ended_utc = datetime.now(timezone.utc).isoformat()
    metrics = {
        "wall_seconds_observed": elapsed,
        "exit_code": proc.returncode,
        "sample_started_utc": started_utc,
        "sample_ended_utc": ended_utc,
        "load_average_before": load_before,
        "load_average_after": load_after,
        "compiler_processes_before": compiler_before,
        "external_compiler_processes_during": list(external_during.values()),
        "compiler_processes_after": compiler_after,
    }
    metrics["contaminated"] = bool(compiler_before or external_during or compiler_after)
    metrics["usable"] = not metrics["contaminated"]
    if time_path.exists():
        for line in time_path.read_text().splitlines():
            key, _, value = line.partition("=")
            if key == "RSS_KB":
                metrics["max_rss_kb"] = int(value)
            elif key in ("WALL", "USER", "SYS"):
                metrics[{"WALL": "time_wall_seconds", "USER": "user_cpu_seconds", "SYS": "system_cpu_seconds"}[key]] = float(value)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(stdout + stderr)
    if proc.returncode:
        raise RuntimeError(f"command failed ({proc.returncode}): {' '.join(command)}\n{stderr[-4000:]}")
    return metrics


def newer(path: Path) -> None:
    stamp = time.time_ns() + 2_000_000_000
    os.utime(path, ns=(stamp, stamp))


def write_raw(raw: Path, record: dict[str, object]) -> None:
    with raw.open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(record, sort_keys=True) + "\n")


def portable_command(command: list[str], cwd: Path) -> list[str]:
    prefix = str(cwd) + os.sep
    return [arg.replace(prefix, "") if prefix in arg else arg for arg in command]


def sample(profile: str, scenario: str, index: int, runs: int, revision: str,
           command: list[str], cwd: Path, env: dict[str, str], raw: Path,
           logs: Path, prepare=None, cleanup=None,
           extra: dict[str, object] | None = None) -> None:
    attempt = 0
    while True:
        attempt += 1
        if prepare:
            prepare()
        progress(profile, scenario, index, runs)
        suffix = f"_{index}" if attempt == 1 else f"_{index}_attempt{attempt}"
        log_path = logs / f"{profile}_{scenario}{suffix}.log"
        metrics = timed(command, cwd, env, log_path)
        if cleanup:
            cleanup()
        write_raw(raw, {"revision": revision, "profile": profile, "scenario": scenario,
                        "run": index, "attempt": attempt, "command": portable_command(command, cwd),
                        "log_path": str(log_path.relative_to(REPO)),
                        **(extra or {}), **metrics})
        if metrics["usable"]:
            return
        print(f"{profile} {scenario} run {index} attempt {attempt} contaminated; retrying", flush=True)
        if attempt >= 20:
            raise RuntimeError(f"could not obtain an uncontaminated sample: {profile} {scenario} {index}")


def progress(profile: str, scenario: str, index: int, runs: int) -> None:
    print(f"{profile} {scenario} run {index}/{runs}", flush=True)


def make_cmd(profile: str, target: str) -> list[str]:
    return ["make", "--jobs=1", target]


def build_scenario(profile: str, scenario: str, runs: int, revision: str,
                   work: Path, raw: Path, logs: Path, env: dict[str, str]) -> None:
    settings = PROFILES[profile]
    target = settings["target"]
    binary = settings["binary"]
    if scenario == "clean_build":
        run(["make", "clean"], work, env)
        for index in range(1, runs + 1):
            sample(profile, scenario, index, runs, revision, make_cmd(profile, target),
                   work, env, raw, logs, prepare=lambda: run(["make", "clean"], work, env))
    elif scenario == "noop_build":
        run(["make", "clean"], work, env)
        run(make_cmd(profile, target), work, env)
        for index in range(1, runs + 1):
            sample(profile, scenario, index, runs, revision, make_cmd(profile, target),
                   work, env, raw, logs)
    elif scenario in ("implementation_edit", "common_header_edit", "isolated_header_edit"):
        run(["make", "clean"], work, env)
        run(make_cmd(profile, target), work, env)
        source_by_scenario = {
            "implementation_edit": work / "src/moss.cpp",
            "common_header_edit": work / "src/ast.hpp",
            "isolated_header_edit": work / "src/rust_float_display.hpp",
        }
        changed = source_by_scenario[scenario]
        original = changed.read_bytes()
        extension = (b"\n// phase25.2 benchmark-only invalidation sample\n"
                     if changed.suffix == ".cpp"
                     else b"\n// phase25.2 benchmark-only invalidation sample\n")

        def edit_copy() -> None:
            changed.write_bytes(original + extension)
            newer(changed)

        def restore_copy() -> None:
            changed.write_bytes(original)
            newer(changed)

        for index in range(1, runs + 1):
            sample(profile, scenario, index, runs, revision, make_cmd(profile, target),
                   work, env, raw, logs, prepare=edit_copy, cleanup=restore_copy,
                   extra={"changed_path": str(changed.relative_to(work)),
                          "change_method": "append then remove a harmless benchmark comment in isolated copy"})
    else:
        raise ValueError(scenario)


def split_compile_link(profile: str, runs: int, revision: str, work: Path,
                       raw: Path, logs: Path, env: dict[str, str]) -> None:
    flags = PROFILES[profile]["flags"].split()
    obj = work / "build/compiler/phase25_2_baseline.o"
    exe = work / "build/compiler/phase25_2_baseline_link"
    obj.parent.mkdir(parents=True, exist_ok=True)
    for scenario in ("compile_only", "link_only"):
        for index in range(1, runs + 1):
            if scenario == "compile_only":
                cmd = ["g++", *flags, "-c", "src/moss.cpp", "-o", str(obj)]
            else:
                if not obj.exists():
                    subprocess.run(["g++", *flags, "-c", "src/moss.cpp", "-o", str(obj)],
                                   cwd=work, env=env, check=True)
                cmd = ["g++", str(obj), "-o", str(exe)]
            sample(profile, scenario, index, runs, revision, cmd, work, env, raw, logs,
                   extra={"flags": flags})


def phase_profile(profile: str, runs: int, revision: str, work: Path,
                  raw: Path, logs: Path, env: dict[str, str]) -> None:
    flags = PROFILES[profile]["flags"].split()
    for index in range(1, runs + 1):
        obj = work / f"build/compiler/phase_report_{profile}_{index}.o"
        obj.parent.mkdir(parents=True, exist_ok=True)
        cmd = ["g++", *flags, "-ftime-report", "-c", "src/moss.cpp", "-o", str(obj)]
        sample(profile, "gcc_ftime_report", index, runs, revision, cmd, work, env,
               raw, logs, extra={"flags": flags,
                                 "phase_report_log": str((logs / f"O{profile[-1]}_gcc_ftime_report_{index}.log").relative_to(REPO))})


def summarize(raw: Path, destination: Path, revision: str, expected_runs: int) -> None:
    groups: dict[tuple[str, str], list[dict[str, object]]] = {}
    for line in raw.read_text().splitlines():
        item = json.loads(line)
        if not item.get("usable", True):
            continue
        groups.setdefault((str(item["profile"]), str(item["scenario"])), []).append(item)
    summary = []
    incomplete = []
    required_groups = [(profile, scenario)
                       for profile in PROFILES
                       for scenario in ("clean_build", "implementation_edit", "noop_build",
                                        "common_header_edit", "isolated_header_edit",
                                        "compile_only", "link_only", "gcc_ftime_report")]
    for profile, scenario in required_groups:
        rows = groups.get((profile, scenario), [])
        chosen: dict[int, dict[str, object]] = {}
        for row in rows:
            if row.get("usable", True):
                run_id = int(row["run"])
                if run_id not in chosen or int(row.get("attempt", 1)) > int(chosen[run_id].get("attempt", 1)):
                    chosen[run_id] = row
        rows = list(chosen.values())
        run_ids = sorted(chosen)
        if run_ids != list(range(1, expected_runs + 1)):
            incomplete.append({"profile": profile, "scenario": scenario,
                               "usable_runs": run_ids, "expected_runs": expected_runs})
            continue
        values: dict[str, object] = {"profile": profile, "scenario": scenario,
                                     "runs": len(rows)}
        for key in ("time_wall_seconds", "user_cpu_seconds", "system_cpu_seconds", "max_rss_kb"):
            series = [float(row[key]) for row in rows if key in row]
            if series:
                values[f"median_{key}"] = statistics.median(series)
                values[f"min_{key}"] = min(series)
                values[f"max_{key}"] = max(series)
        summary.append(values)
    document = {
        "revision": revision,
        "requested_runs_per_scenario": expected_runs,
        "complete": not incomplete,
        "complete_scenarios": summary,
        "incomplete_scenarios": incomplete,
    }
    destination.write_text(json.dumps(document, indent=2) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--revision", default=REVISION_DEFAULT)
    parser.add_argument("--runs", type=int, default=RUNS_DEFAULT)
    parser.add_argument("--output", type=Path, default=REPO / "benchmarks/phase25_2")
    parser.add_argument("--work-root", type=Path, default=REPO / "tmp/phase25_2_build_baseline")
    parser.add_argument("--skip-phase-report", action="store_true")
    args = parser.parse_args()
    if args.runs < 5:
        parser.error("--runs must be at least 5 for reproducible scenario medians")
    args.output.mkdir(parents=True, exist_ok=True)
    args.work_root.mkdir(parents=True, exist_ok=True)
    logs = args.output / "logs"
    logs.mkdir(exist_ok=True)
    raw = args.output / "raw_results.jsonl"
    raw.write_text("")
    env = clean_env()
    metadata = machine_info(args.revision)
    (args.output / "environment.json").write_text(json.dumps(metadata, indent=2) + "\n")
    for profile in PROFILES:
        work = args.work_root / profile
        if work.exists():
            shutil.rmtree(work)
        work.mkdir(parents=True)
        export_tree(work, args.revision, env)
        for scenario in ("clean_build", "implementation_edit", "noop_build",
                         "common_header_edit", "isolated_header_edit"):
            build_scenario(profile, scenario, args.runs, args.revision,
                           work, raw, logs, env)
        split_compile_link(profile, args.runs, args.revision, work, raw, logs, env)
        if not args.skip_phase_report:
            phase_profile(profile, args.runs, args.revision, work, raw, logs, env)
    summarize(raw, args.output / "summary.json", args.revision, args.runs)
    print(f"Wrote {raw} and summary.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
