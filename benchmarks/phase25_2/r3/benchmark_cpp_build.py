#!/usr/bin/env python3
"""Reproducible Clang+mold build-latency and object-isolation probe.

All build trees, raw CSV, timing records, and command logs stay inside this
repository. Incremental probes append a temporary comment and restore the exact
source bytes in a finally block.
"""

from __future__ import annotations

import argparse
import csv
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[3]
DEFAULT_OUTPUT = ROOT / "tmp" / "phase25_2_r3_bench"
COMPONENTS = {
    "parser": Path("src/parser.cpp"),
    "checker": Path("src/semantic_checker.cpp"),
    "generator": Path("src/native_generator.cpp"),
}


def inside_repo(path: Path) -> bool:
    try:
        path.resolve().relative_to(ROOT)
        return True
    except ValueError:
        return False


def command_record(argv: list[str], log: Path, jobs: int,
                   compiler_dir: Path) -> dict[str, object]:
    env = os.environ.copy()
    env["CCACHE_DISABLE"] = "1"
    env["SCCACHE_DISABLE"] = "1"
    command = ["make", f"--jobs={jobs}", *argv,
               f"COMPILER_DIR={compiler_dir.relative_to(ROOT)}"]
    time_file = log.with_suffix(".time")
    with log.open("w", encoding="utf-8") as output:
        result = subprocess.run(
            ["/usr/bin/time", "-f", "%e,%U,%S,%M", "-o", str(time_file),
             *command], cwd=ROOT, env=env, stdout=output,
            stderr=subprocess.STDOUT, text=True)
    values = time_file.read_text().strip().split(",")
    if len(values) != 4:
        raise RuntimeError(f"invalid /usr/bin/time output in {time_file}")
    lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
    compiled = [line.split(" -c ", 1)[1] for line in lines if " -c " in line]
    linked = [line for line in lines if re.search(r" -o build/.+\.tmp(?:\s|$)", line)]
    return {
        "exit_code": result.returncode,
        "wall_seconds": float(values[0]),
        "user_seconds": float(values[1]),
        "system_seconds": float(values[2]),
        "max_rss_kib": int(values[3]),
        "compiled_objects": ";".join(compiled),
        "compile_commands": len(compiled),
        "link_commands": len(linked),
        "log": log.relative_to(ROOT).as_posix(),
        "command": " ".join(command),
    }


def record(rows: list[dict[str, object]], scenario: str, sample: int,
           profile: str, measured: dict[str, object]) -> None:
    rows.append({"scenario": scenario, "sample": sample, "profile": profile,
                 **measured})
    if measured["exit_code"] != 0:
        raise RuntimeError(f"{scenario} sample {sample} failed; see {measured['log']}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--samples", type=int, default=3)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    output = args.output_dir if args.output_dir.is_absolute() else ROOT / args.output_dir
    if args.jobs < 1 or args.samples < 1 or not inside_repo(output):
        parser.error("jobs/samples must be positive and output must stay in the repository")

    status = subprocess.run(["git", "status", "--porcelain", "--",
                             *[path.as_posix() for path in COMPONENTS.values()],
                             "src/type_ir.hpp"], cwd=ROOT, check=True,
                            capture_output=True, text=True).stdout
    if status.strip():
        parser.error("incremental probe sources must be clean before running")

    output.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, object]] = []

    # Each cold sample owns a fresh, isolated Make object/PCH/link tree.
    for profile, target in (("o0", "moss-dev"), ("o2", "moss-opt")):
        for sample in range(1, args.samples + 1):
            compiler_dir = ROOT / "build" / "phase25_2_r3_bench" / f"clean-{profile}-{sample:02d}"
            if compiler_dir.exists():
                shutil.rmtree(compiler_dir)
            measured = command_record(
                [target], output / f"clean-{profile}-{sample:02d}.log",
                args.jobs, compiler_dir)
            record(rows, f"clean-{profile}", sample, profile, measured)

    # Reuse one completed O0 tree for warm no-op and implementation-only edits.
    warm = ROOT / "build" / "phase25_2_r3_bench" / f"clean-o0-{args.samples:02d}"
    for sample in range(1, args.samples + 1):
        measured = command_record(
            ["moss-dev"], output / f"noop-{sample:02d}.log", args.jobs, warm)
        if measured["compile_commands"] or measured["link_commands"]:
            raise RuntimeError("no-op unexpectedly compiled or linked; see log")
        record(rows, "noop", sample, "o0", measured)

    def edit_probe(name: str, path: Path, sample: int) -> None:
        source = ROOT / path
        original = source.read_bytes()
        marker = f"\n// R3 temporary benchmark probe: {name} {sample}\n".encode()
        compiler_dir = warm
        try:
            source.write_bytes(original + marker)
            measured = command_record(
                ["moss-dev"], output / f"{name}-{sample:02d}.log",
                args.jobs, compiler_dir)
            if measured["compile_commands"] == 0 or measured["link_commands"] != 1:
                raise RuntimeError(f"{name} must compile and relink; see {measured['log']}")
            if name in COMPONENTS:
                expected = path.as_posix()
                compiled = str(measured["compiled_objects"])
                if measured["compile_commands"] != 1 or expected not in compiled:
                    raise RuntimeError(f"{name} edit did not isolate its owning object: {compiled}")
            record(rows, name, sample, "o0", measured)
        finally:
            source.write_bytes(original)
            restore = command_record(
                ["moss-dev"], output / f"restore-{name}-{sample:02d}.log",
                args.jobs, compiler_dir)
            if restore["exit_code"] != 0:
                raise RuntimeError(f"failed to restore build after {name} probe")

    for name, path in COMPONENTS.items():
        for sample in range(1, args.samples + 1):
            edit_probe(name, path, sample)
    for sample in range(1, args.samples + 1):
        edit_probe("type-ir-header", Path("src/type_ir.hpp"), sample)

    csv_path = output / "results.csv"
    fields = ["scenario", "sample", "profile", "wall_seconds", "user_seconds",
              "system_seconds", "max_rss_kib", "compile_commands",
              "link_commands", "compiled_objects", "exit_code", "command", "log"]
    with csv_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    print(f"wrote {len(rows)} timing rows to {csv_path.relative_to(ROOT)}")
    print("clean builds use fresh isolated compiler dirs; compiler caches are disabled")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"benchmark failed: {error}", file=sys.stderr)
        raise SystemExit(1)
