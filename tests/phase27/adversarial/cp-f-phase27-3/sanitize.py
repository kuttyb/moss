#!/usr/bin/env python3
"""Flush every frozen-corpus Moss check result from an ASan+UBSan compiler."""
from __future__ import annotations
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import resource
import subprocess
import sys
import tarfile
import io

ROOT = Path(__file__).resolve().parents[4]
COMPILER = Path(sys.argv[1]).resolve()
REV = "2bafad47eb15af1923fbf1673b1a6069dca0f63e"
JOBS = int(os.environ.get("CPF_SAN_JOBS", "8"))
TIMEOUT = int(os.environ.get("CPF_SAN_TIMEOUT", "90"))
RAW = ROOT / "tests/phase27/adversarial/cp-f-phase27-3/sanitizer.jsonl"
CORPUS = ROOT / "tmp/cpf-phase27-3/sanitizer-corpus"
MARKERS = ("ERROR: AddressSanitizer", "SUMMARY: AddressSanitizer", "runtime error:", "UndefinedBehaviorSanitizer")

def source_paths():
    paths = subprocess.check_output(["git", "ls-tree", "-r", "--name-only", REV], cwd=ROOT, text=True).splitlines()
    return sorted(p for p in paths if p.endswith(".moss"))

def stage():
    import shutil
    shutil.rmtree(CORPUS, ignore_errors=True)
    CORPUS.mkdir(parents=True)
    archive = subprocess.check_output(["git", "archive", REV], cwd=ROOT)
    with tarfile.open(fileobj=io.BytesIO(archive)) as tf:
        tf.extractall(CORPUS, filter="data")

def one(path):
    env = os.environ.copy()
    env.update({"MOSS_EST": "shadow", "MOSS_PHASE26_C1_SCOPE_TRACE": "1",
                "ASAN_OPTIONS": "abort_on_error=1:symbolize=1:detect_leaks=0",
                "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1"})
    argv = [str(COMPILER), "check", path, "--json"]
    def no_core(): resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    try:
        p = subprocess.run(argv, cwd=CORPUS, env=env, text=True, capture_output=True,
                           timeout=TIMEOUT, preexec_fn=no_core)
        stdout, stderr, code = p.stdout, p.stderr, p.returncode
    except subprocess.TimeoutExpired as e:
        dec = lambda v: v.decode("utf-8", "replace") if isinstance(v, bytes) else (v or "")
        stdout, stderr, code = dec(e.stdout), dec(e.stderr), "TIMEOUT"
    combined = stdout + stderr
    markers = [m for m in MARKERS if m in combined]
    try:
        payload = json.loads(stdout)
        has_json = True
        ok = payload.get("ok") if isinstance(payload, dict) else None
    except json.JSONDecodeError:
        has_json, ok = False, None
    kind = ("timeout" if code == "TIMEOUT" else "sanitizer" if markers else
            "signal" if isinstance(code, int) and code < 0 else
            "no_json" if not has_json else None)
    row = {"source": path, "command": argv, "returncode": code,
           "json": has_json, "json_ok": ok, "markers": markers,
           "stdout_sha256": hashlib.sha256(stdout.encode()).hexdigest(),
           "stderr_sha256": hashlib.sha256(stderr.encode()).hexdigest(),
           "kind": kind}
    if kind:
        row["stdout"] = stdout
        row["stderr"] = stderr
    return row

def main():
    sources = source_paths()
    stage()
    compiler_hash = hashlib.sha256(COMPILER.read_bytes()).hexdigest()
    RAW.parent.mkdir(parents=True, exist_ok=True)
    findings = []
    with RAW.open("w", encoding="utf-8") as out:
        with concurrent.futures.ThreadPoolExecutor(max_workers=JOBS) as pool:
            futures = {pool.submit(one, s): s for s in sources}
            for done, future in enumerate(concurrent.futures.as_completed(futures), 1):
                row = future.result()
                out.write(json.dumps(row, sort_keys=True) + "\n")
                out.flush()
                if row["kind"]:
                    findings.append(row)
                if done % 100 == 0:
                    print(f"sanitized {done}/{len(sources)}", flush=True)
    findings.sort(key=lambda r: r["source"])
    summary = {"candidate_revision": REV, "compiler_sha256": compiler_hash,
        "mode": "shadow", "sanitizers": ["ASan", "UBSan"], "source_count": len(sources),
        "completed_case_count": len(sources), "finding_count": len(findings),
        "timeout_seconds": TIMEOUT, "jobs": JOBS,
        "finding_kinds": {kind: sum(x["kind"] == kind for x in findings) for kind in ("sanitizer", "signal", "timeout", "no_json")},
        "findings": [{k: x.get(k) for k in ("source", "kind", "returncode", "markers", "stderr")} for x in findings],
        "raw_results": str(RAW.relative_to(ROOT))}
    path = ROOT / "docs/phase27/adversarial/CP_F_PHASE27_3_SANITIZER.json"
    path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({k: summary[k] for k in ("candidate_revision", "compiler_sha256", "source_count", "completed_case_count", "finding_count", "finding_kinds", "raw_results")}, indent=2))
    return 1 if findings else 0

if __name__ == "__main__": raise SystemExit(main())
