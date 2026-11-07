#!/usr/bin/env python3
"""Reject clone-like Rust operations attributed to Moss lines without copy(...)."""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import re
import subprocess
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EXPECTED_COMPILE_FAILURES = (
    ROOT / "tests/tooling/fixtures/phase28_no_hidden_copies_expected_compile_failures.txt"
)
MARK = re.compile(r"//\s*Moss line (\d+):\s*(.*)")
CLONE = re.compile(r"\.clone\(\)|\.cloned\(\)|\.to_owned\(\)|\.to_vec\(\)|Clone::clone")


def audit_rust(source: Path, rust_path: Path) -> tuple[Counter[str], list[str]]:
    moss_lines = source.read_text(errors="replace").splitlines()
    totals: Counter[str] = Counter()
    hidden: list[str] = []
    line_no: int | None = None
    in_type_support = False
    for rust_line in rust_path.read_text(errors="replace").splitlines():
        marker = MARK.search(rust_line)
        if marker:
            line_no = int(marker.group(1))
            head = marker.group(2).strip()
            if re.match(r"(?:export\s+)?(?:type|enum)\b", head):
                in_type_support = True
            elif re.match(r"(?:export\s+)?(?:fn|domain|on|message)\b", head):
                in_type_support = False
            continue
        if line_no is None:
            continue
        matches = list(CLONE.finditer(rust_line))
        if not matches:
            continue
        moss_source_line = moss_lines[line_no - 1] if 0 < line_no <= len(moss_lines) else ""
        if "copy(" in moss_source_line:
            totals["explicit-copy"] += len(matches)
        elif in_type_support:
            totals["type-support"] += len(matches)
        else:
            totals["hidden"] += len(matches)
            for match in matches:
                hidden.append(
                    f"{source.relative_to(ROOT)}:{line_no}: "
                    f"{moss_source_line.strip()} -> {rust_line.strip()} "
                    f"[{match.group(0)}]"
                )
    return totals, hidden


def compile_one(compiler: str, source: Path, out_root: Path) -> tuple[Path, int, str, str, Path]:
    rel = source.relative_to(ROOT)
    key = hashlib.sha256(str(rel).encode()).hexdigest()[:16]
    target_dir = out_root / key
    target_dir.mkdir(parents=True, exist_ok=True)
    rust_path = target_dir / "generated.rs"
    try:
        proc = subprocess.run(
            [compiler, str(source), "-o", str(rust_path)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=30,
        )
    except subprocess.TimeoutExpired as error:
        diagnostic = "\n".join(
            piece.decode(errors="replace") if isinstance(piece, bytes) else piece or ""
            for piece in (error.stdout, error.stderr)
        )
        (target_dir / "compile-error.txt").write_text(diagnostic)
        return source, 124, "", diagnostic, rust_path
    return source, proc.returncode, proc.stdout, proc.stderr, rust_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("compiler")
    parser.add_argument("--out", default="tmp/phase28-no-hidden-copies")
    parser.add_argument("--jobs", type=int, default=16)
    args = parser.parse_args()

    out_root = (ROOT / args.out).resolve()
    out_root.mkdir(parents=True, exist_ok=True)
    sources = sorted(
        source
        for folder in (ROOT / "tests", ROOT / "examples")
        for source in folder.rglob("*.moss")
    )
    compile_failures: list[tuple[Path, int, str]] = []
    totals: Counter[str] = Counter()
    hidden: list[str] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(compile_one, args.compiler, source, out_root) for source in sources]
        for future in concurrent.futures.as_completed(futures):
            source, code, _stdout, stderr, rust_path = future.result()
            if code != 0 or not rust_path.is_file():
                first_line = next((line.strip() for line in stderr.splitlines() if line.strip()), "no diagnostic output")
                compile_failures.append((source, code, first_line))
                continue
            rows, violations = audit_rust(source, rust_path)
            totals.update(rows)
            hidden.extend(violations)

    print(f"sources={len(sources)} compiled={len(sources) - len(compile_failures)} compile_failures={len(compile_failures)}")
    print(f"clone_occurrences={dict(sorted(totals.items()))}")
    for source, code, diagnostic in sorted(compile_failures):
        print(f"COMPILE-FAIL {source.relative_to(ROOT)} exit={code}: {diagnostic}")
    for row in sorted(hidden):
        print(f"HIDDEN {row}")

    expected_failures = {
        line.strip()
        for line in EXPECTED_COMPILE_FAILURES.read_text().splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }
    actual_failures = {
        source.relative_to(ROOT).as_posix()
        for source, _code, _diagnostic in compile_failures
    }
    digest = hashlib.sha256(
        ("\n".join(sorted(actual_failures)) + "\n").encode()
    ).hexdigest()
    print(f"compile_failure_list_count={len(actual_failures)} sha256={digest}")
    unexpected = sorted(actual_failures - expected_failures)
    missing = sorted(expected_failures - actual_failures)
    for path in unexpected:
        print(f"UNEXPECTED-COMPILE-FAIL {path}")
    for path in missing:
        print(f"EXPECTED-COMPILE-FAIL-MISSING {path}")
    return 1 if hidden or unexpected or missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
