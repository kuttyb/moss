#!/usr/bin/env python3
"""Run every tracked Moss source through EST shadow under ASan and UBSan."""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import io
import json
import os
from pathlib import Path
import resource
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[4]
SANITIZER_MARKERS = (
    "ERROR: AddressSanitizer", "SUMMARY: AddressSanitizer",
    "runtime error:", "UndefinedBehaviorSanitizer",
)


def run_source(compiler: Path, corpus: Path, source: str, timeout: int):
    env = os.environ.copy()
    env.update({
        "MOSS_EST": "shadow",
        "MOSS_PHASE26_C1_SCOPE_TRACE": "1",
        "ASAN_OPTIONS": "abort_on_error=1:symbolize=1:detect_leaks=0",
        "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1",
    })

    def no_core_dump():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))

    try:
        result = subprocess.run(
            [str(compiler), "check", source, "--json"], cwd=corpus,
            env=env, text=True, capture_output=True, timeout=timeout,
            preexec_fn=no_core_dump)
    except subprocess.TimeoutExpired as error:
        def capture_text(value):
            if isinstance(value, bytes):
                return value.decode("utf-8", errors="replace")
            return value or ""
        return {"source": source, "kind": "timeout", "timeout": timeout,
                "stdout": capture_text(error.stdout), "stderr": capture_text(error.stderr)}
    combined = result.stdout + result.stderr
    findings = [marker for marker in SANITIZER_MARKERS if marker in combined]
    try:
        json.loads(result.stdout)
        has_json = True
    except (json.JSONDecodeError, TypeError):
        has_json = False
    if result.returncode < 0 or findings or not has_json:
        return {"source": source, "kind": "sanitizer" if findings else
                "signal" if result.returncode < 0 else "no_json",
                "returncode": result.returncode, "markers": findings,
                "stdout": result.stdout, "stderr": result.stderr}
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler", type=Path)
    parser.add_argument("--rev", default="HEAD")
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 4, 8))
    parser.add_argument("--timeout", type=int, default=90)
    parser.add_argument("--out", type=Path,
                        default=ROOT / "tmp/phase27-2/sanitized-corpus.json")
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    compiler_hash = hashlib.sha256(compiler.read_bytes()).hexdigest()
    sources = subprocess.check_output(
        ["git", "ls-tree", "-r", "--name-only", args.rev], cwd=ROOT,
        text=True).splitlines()
    sources = [source for source in sources if source.endswith(".moss")
               and not source.startswith("tests/phase27/adversarial/")]
    corpus = ROOT / "tmp/phase27-2/sanitized-corpus-tree"
    import shutil
    shutil.rmtree(corpus, ignore_errors=True)
    corpus.mkdir(parents=True)
    archive = subprocess.check_output(["git", "archive", args.rev], cwd=ROOT)
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(corpus, filter="data")
    findings = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(run_source, compiler, corpus, source,
                               args.timeout) for source in sources]
        for future in concurrent.futures.as_completed(futures):
            finding = future.result()
            if finding:
                findings.append(finding)
    findings.sort(key=lambda item: item["source"])
    args.out.parent.mkdir(parents=True, exist_ok=True)
    report = {"revision": args.rev, "compiler_sha256": compiler_hash,
              "mode": "shadow", "sanitizers": ["ASan", "UBSan"],
              "source_count": len(sources), "finding_count": len(findings),
              "findings": findings}
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    try:
        report_path = args.out.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        report_path = str(args.out)
    print(f"{len(sources)} sources; {len(findings)} sanitizer/process findings; "
          f"report {report_path}")
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
