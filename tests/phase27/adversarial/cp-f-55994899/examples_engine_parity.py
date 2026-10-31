#!/usr/bin/env python3
"""Exercise top-level examples in Fast Debug and generated native Rust."""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess


def run(command: list[str], cwd: Path, env: dict, timeout: int) -> dict:
    try:
        result = subprocess.run(command, cwd=cwd, env=env, text=True,
                                capture_output=True, timeout=timeout)
        return {"kind": "process", "returncode": result.returncode,
                "stdout": result.stdout, "stderr": result.stderr}
    except subprocess.TimeoutExpired as exc:
        return {"kind": "timeout", "timeout_seconds": timeout,
                "stdout": exc.stdout or "", "stderr": exc.stderr or ""}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("candidate_revision")
    parser.add_argument("candidate_root", type=Path)
    parser.add_argument("baseline_compiler", type=Path)
    parser.add_argument("candidate_compiler", type=Path)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 4, 4))
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    root = args.candidate_root.resolve()
    baseline, candidate = args.baseline_compiler.resolve(), args.candidate_compiler.resolve()
    paths = subprocess.check_output(
        ["git", "ls-tree", "-r", "--name-only", args.candidate_revision],
        cwd=root, text=True).splitlines()
    all_examples = sorted(path for path in paths if path.startswith("examples/") and
                          path.count("/") == 1 and path.endswith(".moss"))
    long_timeout_cases = ["examples/functional_pipeline.moss",
                          "examples/frontend_syntax.moss"]
    examples = [path for path in all_examples if path not in long_timeout_cases]
    workroot = root / "tmp" / "phase27-3" / "cp-f-55994899" / "examples"
    workroot.mkdir(parents=True, exist_ok=True)

    def check_case(source: str) -> dict:
        source_id = hashlib.sha256(source.encode()).hexdigest()[:12]
        case_dir = workroot / source_id
        case_dir.mkdir(parents=True, exist_ok=True)
        results = {"source": source, "fast_debug": {}, "native": {}}
        checks = {}
        for label, compiler, mode in (("baseline", baseline, "off"),
                                      ("candidate", candidate, "shadow")):
            env = os.environ.copy(); env["MOSS_EST"] = mode
            checks[label] = run([str(compiler), "check", source, "--json"],
                                root, env, args.timeout)
        results["checks"] = checks
        accepted = {}
        for label, checked in checks.items():
            try:
                accepted[label] = json.loads(checked.get("stdout", "")).get("ok") is True
            except (json.JSONDecodeError, AttributeError):
                accepted[label] = False
        if not all(accepted.values()):
            results["status"] = "rejected"
            results["check_equal"] = (
                checks["baseline"].get("returncode") == checks["candidate"].get("returncode") and
                checks["baseline"].get("stdout") == checks["candidate"].get("stdout"))
            return results
        results["status"] = "accepted"
        expected = None
        for label, compiler, mode in (("baseline", baseline, "off"),
                                      ("candidate", candidate, "shadow")):
            env = os.environ.copy(); env["MOSS_EST"] = mode
            fast = run([str(compiler), "run", "--interp", source], root, env, args.timeout)
            results["fast_debug"][label] = fast
            if fast["kind"] == "process" and fast["returncode"] == 0:
                expected = fast["stdout"] if expected is None else expected
        for profile in ("default", "optimized"):
            flags = ["-O"] if profile == "optimized" else []
            for label, compiler in (("baseline", baseline), ("candidate", candidate)):
                rust_path = case_dir / f"{profile}-{label}.rs"
                env = os.environ.copy(); env["MOSS_EST"] = "off"
                emitted = run([str(compiler), source, *flags, "-o", str(rust_path)],
                              root, env, args.timeout)
                if emitted["kind"] != "process" or emitted["returncode"] or not rust_path.is_file():
                    results["native"][f"{profile}:{label}"] = {
                        "emit": emitted, "rustc": None, "run": None}
                    continue
                for rustc_profile, rustc_flags in (("default", []),
                                                   ("optimized", ["-O"])):
                    binary = case_dir / f"{profile}-{label}-{rustc_profile}"
                    compiled = run(["rustc", "--edition=2021", *rustc_flags,
                                    str(rust_path), "-o", str(binary)], root,
                                   os.environ.copy(), args.timeout)
                    native = (run([str(binary)], root, os.environ.copy(), args.timeout)
                              if compiled["kind"] == "process" and compiled["returncode"] == 0
                              else None)
                    results["native"][f"{profile}:{label}:{rustc_profile}"] = {
                        "emit": {"kind": emitted["kind"], "returncode": emitted["returncode"],
                                 "stderr": emitted["stderr"]},
                        "rustc": compiled, "run": native}
                    if native and native["kind"] == "process" and native["returncode"] == 0:
                        expected = native["stdout"] if expected is None else expected
                if rust_path.exists():
                    rust_path.unlink()
        return results

    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        rows = list(pool.map(check_case, examples))
    mismatches, incomplete, paired_fast_debug_failures = [], [], []
    for row in rows:
        if row["status"] == "rejected":
            if not row["check_equal"]:
                mismatches.append({"source": row["source"], "check_delta": row["checks"]})
            continue
        outs = []
        for label, result in row["fast_debug"].items():
            if result["kind"] == "process" and result["returncode"] == 0:
                outs.append((f"fast:{label}", result["stdout"]))
        for label, cell in row["native"].items():
            native = cell.get("run")
            if native and native["kind"] == "process" and native["returncode"] == 0:
                outs.append((f"native:{label}", native["stdout"]))
        values = {output for _, output in outs}
        if len(values) > 1:
            mismatches.append({"source": row["source"], "outputs": outs})
        failures = []
        fast = row["fast_debug"]
        fast_failure = {label for label, result in fast.items()
                        if result["kind"] != "process" or result["returncode"] != 0}
        if fast_failure:
            before, after = fast["baseline"], fast["candidate"]
            if (fast_failure == {"baseline", "candidate"} and
                    before["kind"] == after["kind"] == "process" and
                    before["returncode"] == after["returncode"] and
                    before["stdout"] == after["stdout"] and
                    before["stderr"] == after["stderr"]):
                paired_fast_debug_failures.append({"source": row["source"],
                                                   "returncode": before["returncode"],
                                                   "stderr": before["stderr"]})
            else:
                failures.extend({"engine": f"fast:{label}", "result": fast[label]}
                                for label in fast_failure)
        for label, cell in row["native"].items():
            for phase in ("emit", "rustc", "run"):
                result = cell.get(phase)
                if phase == "run" and result is None:
                    failures.append({"engine": f"native:{label}:{phase}",
                                     "result": "not run after earlier failure"})
                elif result and (result.get("kind") != "process" or
                                 result.get("returncode") != 0):
                    failures.append({"engine": f"native:{label}:{phase}", "result": result})
        if failures:
            incomplete.append({"source": row["source"], "failures": failures})
    out = {"candidate_revision": args.candidate_revision,
           "source_count": len(examples), "excluded_for_paired_600s_probe": long_timeout_cases,
           "comparison": "baseline Fast Debug off, candidate Fast Debug shadow, baseline/candidate Rust default and -O, rustc default and -O",
           "mismatch_count": len(mismatches), "mismatches": mismatches,
           "incomplete_count": len(incomplete), "incomplete": incomplete,
           "rejected_source_count": sum(row["status"] == "rejected" for row in rows),
           "paired_fast_debug_nonzero_count": len(paired_fast_debug_failures),
           "paired_fast_debug_nonzero": paired_fast_debug_failures,
           "entries": rows}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(out, indent=2, sort_keys=True) + "\n")
    print(f"examples={len(examples)} output_mismatches={len(mismatches)}")
    print(f"report={args.out}")
    return 1 if mismatches or incomplete else 0


if __name__ == "__main__":
    raise SystemExit(main())
