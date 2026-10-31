#!/usr/bin/env python3
"""Run paired baseline/candidate 600-second Fast Debug trace probes."""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time


def run_one(label: str, compiler: Path, mode: str, source: str, root: Path,
            timeout: int, log_dir: Path) -> dict:
    log_dir.mkdir(parents=True, exist_ok=True)
    stdout_path = log_dir / f"{Path(source).stem}-{label}.stdout"
    stderr_path = log_dir / f"{Path(source).stem}-{label}.stderr"
    env = os.environ.copy(); env["MOSS_EST"] = mode
    start = time.monotonic()
    with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
        process = subprocess.Popen([str(compiler), "debug", source, "--trace"],
                                   cwd=root, env=env, stdout=stdout, stderr=stderr,
                                   start_new_session=True)
        try:
            returncode = process.wait(timeout=timeout)
            kind = "complete"
        except subprocess.TimeoutExpired:
            kind = "timeout"
            try:
                os.killpg(process.pid, signal.SIGTERM)
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            returncode = None
    elapsed = time.monotonic() - start
    out_bytes, err_bytes = stdout_path.read_bytes(), stderr_path.read_bytes()
    return {"label": label, "mode": mode, "kind": kind,
            "returncode": returncode, "elapsed_seconds": round(elapsed, 3),
            "stdout_path": str(stdout_path.relative_to(root)),
            "stderr_path": str(stderr_path.relative_to(root)),
            "stdout_bytes": len(out_bytes), "stderr_bytes": len(err_bytes),
            "stdout_sha256": hashlib.sha256(out_bytes).hexdigest(),
            "stderr_sha256": hashlib.sha256(err_bytes).hexdigest()}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("candidate_root", type=Path)
    parser.add_argument("baseline_compiler", type=Path)
    parser.add_argument("candidate_compiler", type=Path)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    root = args.candidate_root.resolve()
    baseline, candidate = args.baseline_compiler.resolve(), args.candidate_compiler.resolve()
    work = root / "tmp" / "phase27-3" / "cp-f-55994899" / "fast-debug-timeouts"
    fixtures = ["examples/functional_pipeline.moss", "examples/frontend_syntax.moss"]
    rows = []
    for source in fixtures:
        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            before = pool.submit(run_one, "baseline", baseline, "off", source,
                                 root, args.timeout, work)
            after = pool.submit(run_one, "candidate", candidate, "shadow", source,
                                root, args.timeout, work)
            baseline_result, candidate_result = before.result(), after.result()
        if baseline_result["kind"] == candidate_result["kind"] == "timeout":
            classification = "BOTH-TIMEOUT; excluded from output comparison"
        elif (baseline_result["kind"] == candidate_result["kind"] == "complete" and
              baseline_result["returncode"] == candidate_result["returncode"] and
              baseline_result["stdout_sha256"] == candidate_result["stdout_sha256"]):
            classification = "paired completion with matching stdout and exit status"
        else:
            classification = "unpaired timeout or completed-output delta"
        rows.append({"source": source, "baseline": baseline_result,
                     "candidate": candidate_result, "classification": classification})
    report = {"timeout_seconds": args.timeout,
              "invocation": "moss debug <fixture> --trace",
              "baseline_mode": "MOSS_EST=off",
              "candidate_mode": "MOSS_EST=shadow",
              "baseline_compiler_sha256": hashlib.sha256(baseline.read_bytes()).hexdigest(),
              "candidate_compiler_sha256": hashlib.sha256(candidate.read_bytes()).hexdigest(),
              "fixtures": rows}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(json.dumps([{"source": row["source"], "classification": row["classification"],
                       "baseline_s": row["baseline"]["elapsed_seconds"],
                       "candidate_s": row["candidate"]["elapsed_seconds"]}
                      for row in rows]))
    print(f"report={args.out}")
    return 1 if any(row["classification"] == "unpaired timeout or completed-output delta"
                    for row in rows) else 0


if __name__ == "__main__":
    raise SystemExit(main())
