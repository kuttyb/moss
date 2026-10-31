#!/usr/bin/env python3
"""Compare baseline/candidate generated Rust for accepted candidate-tree inputs."""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("candidate_root", type=Path)
    parser.add_argument("baseline_compiler", type=Path)
    parser.add_argument("candidate_compiler", type=Path)
    parser.add_argument("accepted_report", type=Path)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 4, 8))
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    root = args.candidate_root.resolve()
    baseline, candidate = args.baseline_compiler.resolve(), args.candidate_compiler.resolve()
    corpus = json.loads(args.accepted_report.read_text())
    eligible = sorted(set(corpus["accepted_sources"]["baseline_off"]) &
                      set(corpus["accepted_sources"]["candidate_off"]))
    scratch = root / "tmp" / "phase27-3" / "cp-f-55994899" / "rust"
    scratch.mkdir(parents=True, exist_ok=True)
    tasks = [(source, profile) for source in eligible for profile in ("default", "optimized")]

    def emit(task):
        source, profile = task
        flags = ["-O"] if profile == "optimized" else []
        key = hashlib.sha256((source + "\0" + profile).encode()).hexdigest()[:20]
        outputs = {}
        for label, compiler in (("baseline", baseline), ("candidate", candidate)):
            target = scratch / f"{key}-{label}.rs"
            env = os.environ.copy()
            env["MOSS_EST"] = "off"
            try:
                result = subprocess.run(
                    [str(compiler), source, *flags, "-o", str(target)],
                    cwd=root, env=env, text=True, capture_output=True,
                    timeout=args.timeout)
            except subprocess.TimeoutExpired as exc:
                outputs[label] = {"kind": "timeout", "timeout_seconds": args.timeout,
                                  "stdout": exc.stdout or "", "stderr": exc.stderr or ""}
                continue
            if result.returncode or not target.is_file():
                outputs[label] = {"kind": "emit_failure", "returncode": result.returncode,
                                  "stdout": result.stdout, "stderr": result.stderr}
                target.unlink(missing_ok=True)
                continue
            raw = target.read_bytes()
            normalized = raw.replace(str(root).encode(), b"<CANDIDATE_ROOT>")
            outputs[label] = {"kind": "rust", "raw_sha256": digest(raw),
                              "normalized_sha256": digest(normalized),
                              "size_bytes": len(raw)}
            target.unlink()
        return {"source": source, "profile": profile, **outputs}

    rows = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        rows.extend(pool.map(emit, tasks))
    rows.sort(key=lambda row: (row["source"], row["profile"]))
    failures = []
    identical_emit_failures = []
    unpaired_emit_failures = []
    for row in rows:
        if row["baseline"].get("kind") == "rust" and row["candidate"].get("kind") == "rust":
            if (row["baseline"]["raw_sha256"] != row["candidate"]["raw_sha256"] or
                    row["baseline"]["normalized_sha256"] != row["candidate"]["normalized_sha256"]):
                failures.append(row)
            continue
        def normalized_failure(value):
            result = dict(value)
            for key in ("stdout", "stderr"):
                if key in result:
                    result[key] = result[key].replace(str(root), "<CANDIDATE_ROOT>")
            return result
        row["baseline_candidate_failure_equal"] = (
            normalized_failure(row["baseline"]) == normalized_failure(row["candidate"]))
        if row["baseline_candidate_failure_equal"]:
            identical_emit_failures.append(row)
        else:
            unpaired_emit_failures.append(row)
    report = {
        "source_count_eligible": len(eligible),
        "emission_comparisons": len(rows),
        "profiles": {"default": "MOSS_EST=off without -O", "optimized": "MOSS_EST=off with -O"},
        "source_tree": "tracked candidate revision, same candidate-root path for both compilers",
        "baseline_compiler_sha256": digest(baseline.read_bytes()),
        "candidate_compiler_sha256": digest(candidate.read_bytes()),
        "raw_and_root_normalized_rust_equal_count": len(rows) - len(failures) - len(identical_emit_failures) - len(unpaired_emit_failures),
        "identical_baseline_candidate_emit_failures": identical_emit_failures,
        "rust_mismatches": failures,
        "unpaired_emit_failures_or_timeouts": unpaired_emit_failures,
        "entries": rows,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"eligible={len(eligible)} comparisons={len(rows)} mismatches={len(failures)} "
          f"identical-emit-failures={len(identical_emit_failures)} "
          f"unpaired-emit-failures={len(unpaired_emit_failures)}")
    print(f"report={args.out}")
    return 1 if failures or unpaired_emit_failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
