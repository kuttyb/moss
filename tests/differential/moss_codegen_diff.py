#!/usr/bin/env python3
"""Exact generated-Rust differential test: C++ Moss versus Java Moss.

Invoke from a Moss checkout. Both compilers receive the same .moss source, cwd,
flags, environment, and Rust output pathname. Compile results are compared byte
for byte. Neither compiler's stdout is mistaken for generated-Rust parity.

Note: source files belonging to projects may require project-level builds;
rejections are recorded, NOT counted as successful codegen comparisons.
"""
from __future__ import annotations

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
import difflib
import hashlib
import math
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

SKIP_DIRS = {".git", "build", "tmp", "target", "node_modules", ".venv", "__pycache__"}
CODE_RE = re.compile(r"\[([A-Z][A-Z0-9_]+)\]")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def collect_sources(root: Path, paths: list[str], out: Path) -> list[Path]:
    found = set()
    requested = paths or ["."]
    for raw in requested:
        p = (root / raw).resolve()
        if p.is_file() and p.suffix == ".moss":
            found.add(p)
        elif p.is_dir():
            for base, dirs, files in os.walk(p):
                dirs[:] = [d for d in dirs if d not in SKIP_DIRS and
                           not (Path(base) / d).resolve().is_relative_to(out)]
                for name in files:
                    if name.endswith(".moss"):
                        found.add((Path(base) / name).resolve())
        else:
            print(f"warning: nonexistent/unrecognized source path: {raw}", file=sys.stderr)
    return sorted((p for p in found if p.is_relative_to(root)), key=lambda p: str(p.relative_to(root)))


def run(command: list[str], root: Path, env: dict[str, str], timeout: float, output: Path) -> dict:
    output.unlink(missing_ok=True)
    start = time.perf_counter()
    try:
        p = subprocess.run(command, cwd=root, env=env, capture_output=True,
                           text=True, errors="replace", timeout=timeout)
        ret, stdout, stderr = p.returncode, p.stdout, p.stderr
        failed = None
    except subprocess.TimeoutExpired as e:
        ret, stdout, stderr = None, "", str(e)
        failed = "timeout"
    except OSError as e:
        ret, stdout, stderr = None, "", str(e)
        failed = "launch_error"
    elapsed = round((time.perf_counter() - start) * 1000, 3)
    data = output.read_bytes() if output.is_file() else None
    # stdout/stderr are preserved for divergent cases and truncated in JSONL.
    return {"rc": ret, "ms": elapsed, "data": data,
            "sha256": digest(data) if data is not None else None,
            "error_code": CODE_RE.findall(stderr + "\n" + stdout)[:3],
            "stdout": stdout, "stderr": stderr, "failure": failed}


def classify(c: dict, j: dict) -> str:
    if c["failure"] or j["failure"]:
        return "execution_failure"
    if c["rc"] == 0 and j["rc"] == 0:
        if c["data"] is None or j["data"] is None:
            return "missing_rust_output"
        return "rust_identical" if c["data"] == j["data"] else "rust_different"
    if c["rc"] == 0:
        return "cpp_accepts_java_rejects"
    if j["rc"] == 0:
        return "java_accepts_cpp_rejects"
    if c["error_code"] and j["error_code"] and c["error_code"][0] == j["error_code"][0]:
        return "both_reject_same_error_code"
    if c["error_code"] and j["error_code"]:
        return "both_reject_different_error_code"
    return "both_reject_unclassified"


def compare(source: Path, opts: argparse.Namespace, env: dict[str, str]) -> dict:
    relative = source.relative_to(opts.root).as_posix()
    case_id = digest(relative.encode("utf-8"))[:14]
    work = opts.out / "scratch" / case_id
    work.mkdir(parents=True, exist_ok=True)
    shared_output = work / "generated.rs"
    # The identical output pathname matters: generated Rust can encode paths.
    flags = opts.flag
    c = run(opts.cpp_cmd + flags + [str(source), "-o", str(shared_output)],
            opts.root, env, opts.timeout, shared_output)
    j = run(opts.java_cmd + flags + [str(source), "-o", str(shared_output)],
            opts.root, env, opts.timeout, shared_output)
    category = classify(c, j)
    keep = category not in {"rust_identical", "both_reject_same_error_code", "both_reject_unclassified"}
    if keep:
        dest = opts.out / "divergences" / case_id
        dest.mkdir(parents=True, exist_ok=True)
        if c["data"] is not None:
            (dest / "cpp.rs").write_bytes(c["data"])
        if j["data"] is not None:
            (dest / "java.rs").write_bytes(j["data"])
        for label, info in (("cpp", c), ("java", j)):
            (dest / f"{label}.stdout.txt").write_text(info["stdout"], encoding="utf-8")
            (dest / f"{label}.stderr.txt").write_text(info["stderr"], encoding="utf-8")
        if category == "rust_different":
            a = c["data"].decode("utf-8", "replace").splitlines(keepends=True)
            b = j["data"].decode("utf-8", "replace").splitlines(keepends=True)
            diff = difflib.unified_diff(a, b, fromfile=f"cpp/{relative}", tofile=f"java/{relative}")
            (dest / "rust.diff").write_text("".join(diff), encoding="utf-8")
    # Keep sidecars and temporary generated files out of the source tree.
    shutil.rmtree(work, ignore_errors=True)
    return {
        "source": relative, "category": category,
        "cpp_exit": c["rc"], "java_exit": j["rc"],
        "cpp_ms": c["ms"], "java_ms": j["ms"],
        "cpp_sha256": c["sha256"], "java_sha256": j["sha256"],
        "cpp_error_codes": c["error_code"], "java_error_codes": j["error_code"],
        "cpp_output_bytes": len(c["data"]) if c["data"] is not None else None,
        "java_output_bytes": len(j["data"]) if j["data"] is not None else None,
        "artifacts": f"divergences/{case_id}" if keep else None,
    }


def timing_summary(rows: list[dict], field: str) -> dict:
    v = sorted(r[field] for r in rows if r[field] is not None)
    if not v:
        return {}
    return {"median_ms": round(statistics.median(v), 2),
            "p95_ms": round(v[max(0, math.ceil(len(v) * 0.95) - 1)], 2),
            "total_s": round(sum(v) / 1000, 2)}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="*", help="Moss files/directories; default: whole repository")
    ap.add_argument("--root", type=Path, default=Path("."), help="Moss repository root")
    ap.add_argument("--cpp", default="./build/compiler/moss-dev", help="C++ compiler command")
    ap.add_argument("--java", default="./scripts/java/run.sh", help="Java compiler command")
    ap.add_argument("--jobs", type=int, default=4, help="Parallel source comparisons (default: 4)")
    ap.add_argument("--timeout", type=float, default=90, help="Seconds per compiler invocation")
    ap.add_argument("--abi", choices=["v9", "v10"], default="v9", help="Compiler ABI mode")
    ap.add_argument("--flag", action="append", default=[], help="Extra identical compiler flag (repeatable)")
    ap.add_argument("--max-files", type=int, help="Deterministic smoke-test limit")
    ap.add_argument("--out", type=Path, default=Path("build/java-cpp-codegen-diff"))
    opts = ap.parse_args()
    opts.root = opts.root.resolve()
    opts.out = (opts.root / opts.out).resolve()
    opts.cpp_cmd = shlex.split(opts.cpp)
    opts.java_cmd = shlex.split(opts.java)
    if not opts.cpp_cmd or not opts.java_cmd or opts.jobs < 1:
        ap.error("compiler commands must be nonempty and jobs must be >= 1")
    if not opts.root.is_dir():
        ap.error("repository root does not exist")
    files = collect_sources(opts.root, opts.paths, opts.out)
    if opts.max_files is not None:
        files = files[:opts.max_files]
    if not files:
        ap.error("no Moss source files found")
    opts.out.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.pop("MOSS_STAGED_ABI", None)
    if opts.abi == "v10":
        env["MOSS_STAGED_ABI"] = "v10"
    print(f"Comparing {len(files)} source files in ABI {opts.abi}: ",
          f"C++={opts.cpp!r}, Java={opts.java!r}, jobs={opts.jobs}", flush=True)
    results = []
    with ThreadPoolExecutor(max_workers=opts.jobs) as pool:
        future_map = {pool.submit(compare, file, opts, env): file for file in files}
        for f in as_completed(future_map):
            try:
                result = f.result()
            except Exception as e:
                result = {"source": future_map[f].relative_to(opts.root).as_posix(),
                          "category": "harness_error", "error": str(e),
                          "cpp_ms": None, "java_ms": None}
            results.append(result)
            if result["category"] not in {"rust_identical", "both_reject_same_error_code"}:
                print(f"[{result['category']}] {result['source']}", flush=True)
            if len(results) % 100 == 0:
                print(f"completed {len(results)}/{len(files)}", flush=True)
    results.sort(key=lambda x: x["source"])
    counts = Counter(r["category"] for r in results)
    both_compile = counts["rust_identical"] + counts["rust_different"]
    summary = {
        "abi": opts.abi, "total_files": len(results), "categories": dict(sorted(counts.items())),
        "both_compiled": both_compile,
        "exact_rust_parity_percent": round(100 * counts["rust_identical"] / both_compile, 2) if both_compile else None,
        "timings_all": {"cpp": timing_summary(results, "cpp_ms"), "java": timing_summary(results, "java_ms")},
        "timings_both_compiled": {
            "cpp": timing_summary([r for r in results if r["category"] in {"rust_identical", "rust_different"}], "cpp_ms"),
            "java": timing_summary([r for r in results if r["category"] in {"rust_identical", "rust_different"}], "java_ms")},
        "note": "Individual-file differential pass, not project dependency closure. Both-rejected files are excluded from Rust parity denominator. Wall time includes JVM cold startup. Exact Rust differs does not necessarily imply behavioral difference."
    }
    (opts.out / "results.jsonl").write_text("".join(json.dumps(r, sort_keys=True) + "\n" for r in results), encoding="utf-8")
    (opts.out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2), flush=True)
    print(f"Results: {opts.out / 'summary.json'} and {opts.out / 'results.jsonl'}")
    return 1 if any(counts[k] for k in ["rust_different", "cpp_accepts_java_rejects", "java_accepts_cpp_rejects", "both_reject_different_error_code", "missing_rust_output", "execution_failure", "harness_error"]) else 0


if __name__ == "__main__":
    raise SystemExit(main())