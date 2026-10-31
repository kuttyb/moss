#!/usr/bin/env python3
"""Run the frozen candidate's full tracked Moss corpus under ASan and UBSan."""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import subprocess


MARKERS = ("ERROR: AddressSanitizer", "SUMMARY: AddressSanitizer",
           "runtime error:", "UndefinedBehaviorSanitizer")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("candidate_revision")
    parser.add_argument("candidate_root", type=Path)
    parser.add_argument("sanitized_compiler", type=Path)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 4, 8))
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    root, compiler = args.candidate_root.resolve(), args.sanitized_compiler.resolve()
    paths = subprocess.check_output(
        ["git", "ls-tree", "-r", "--name-only", args.candidate_revision],
        cwd=root, text=True).splitlines()
    sources = sorted(path for path in paths if path.endswith(".moss"))
    inputs = sorted(path for path in paths if path.endswith(".moss") or
                    (Path(path).name in ("Moss.toml", "Moss.lock") and
                     not path.startswith(("docs/phase27/adversarial/",
                                          "tests/phase27/adversarial/cp-",
                                          "tests/phase27/adversarial/perf-"))))
    corpus = root / "tmp" / "phase27-3" / "cp-f-55994899" / "sanitized-corpus"
    shutil.rmtree(corpus, ignore_errors=True)
    for relative in inputs:
        source_path = root / relative
        if not source_path.is_file():
            raise SystemExit(f"candidate corpus input missing from frozen worktree: {relative}")
        target_path = corpus / relative
        target_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_path, target_path)

    def no_core_dump():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

    def run_source(source: str):
        env = os.environ.copy()
        env.update({"MOSS_EST": "shadow",
                    "MOSS_PHASE26_C1_SCOPE_TRACE": "1",
                    "ASAN_OPTIONS": "abort_on_error=1:symbolize=1:detect_leaks=0",
                    "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1"})
        try:
            result = subprocess.run([str(compiler), "check", source, "--json"],
                                    cwd=corpus, env=env, text=True,
                                    capture_output=True, timeout=args.timeout,
                                    preexec_fn=no_core_dump)
        except subprocess.TimeoutExpired as exc:
            return {"source": source, "kind": "timeout",
                    "timeout_seconds": args.timeout,
                    "stdout": exc.stdout or "", "stderr": exc.stderr or ""}
        combined = result.stdout + result.stderr
        markers = [marker for marker in MARKERS if marker in combined]
        try:
            response = json.loads(result.stdout)
            has_json = True
            verdict = {"ok": response.get("ok"),
                       "error_code": (response.get("error") or {}).get("code"),
                       "error_line": (response.get("error") or {}).get("line")}
        except (json.JSONDecodeError, TypeError):
            has_json, verdict = False, None
        if result.returncode < 0 or markers or not has_json:
            return {"source": source,
                    "kind": "sanitizer" if markers else
                            "signal" if result.returncode < 0 else "no_json",
                    "returncode": result.returncode, "markers": markers,
                    "verdict": verdict, "stdout": result.stdout,
                    "stderr": result.stderr}
        return None

    findings = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(run_source, source) for source in sources]
        for future in concurrent.futures.as_completed(futures):
            finding = future.result()
            if finding:
                findings.append(finding)
    findings.sort(key=lambda item: item["source"])
    report = {"candidate_revision": args.candidate_revision,
              "candidate_compiler": str(compiler),
              "compiler_sha256": hashlib.sha256(compiler.read_bytes()).hexdigest(),
              "source_count": len(sources), "sanitizers": ["ASan", "UBSan"],
              "mode": "MOSS_EST=shadow", "timeout_seconds": args.timeout,
              "review_evidence_materialized": False,
              "finding_count": len(findings), "findings": findings}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"sources={len(sources)} findings={len(findings)}")
    print(f"report={args.out}")
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
