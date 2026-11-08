#!/usr/bin/env python3
"""Check that Map.get borrows values and explicit copy enables mutation."""
from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REJECT_SOURCE = ROOT / "tests/swarm_021_map_get_fallback_owned.moss"
COPY_SOURCE = ROOT / "tests/swarm_021_map_get_fallback_owned_explicit_copy.moss"


def compile_native(compiler: str, source: Path, out: Path) -> tuple[int, str, Path]:
    rust = out / f"{source.stem}.rs"
    result = subprocess.run(
        [compiler, str(source), "-o", str(rust)],
        cwd=ROOT,
        text=True,
        capture_output=True,
        timeout=30,
    )
    return result.returncode, result.stderr, rust


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("compiler")
    parser.add_argument("--out", default="tmp/phase28-map-get-copy-contract")
    args = parser.parse_args()
    out = (ROOT / args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)

    code, diagnostic, rust = compile_native(args.compiler, REJECT_SOURCE, out)
    if code:
        print(f"PASS implicit-copy source rejected by Moss: {diagnostic.strip()}")
    else:
        rejected = subprocess.run(
            ["rustc", str(rust), "-o", str(out / "must_reject")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            timeout=60,
        )
        if rejected.returncode == 0 or "cannot borrow `*result` as mutable" not in rejected.stderr:
            print("FAIL implicit Map.get result was not rejected as a borrowed value")
            print(rejected.stderr)
            return 1
        print("PASS implicit-copy source rejected as a borrowed Map.get result")

    code, diagnostic, rust = compile_native(args.compiler, COPY_SOURCE, out)
    if code:
        print(f"FAIL explicit-copy source rejected by Moss: {diagnostic.strip()}")
        return 1
    binary = out / "explicit_copy"
    built = subprocess.run(
        ["rustc", str(rust), "-o", str(binary)],
        cwd=ROOT,
        text=True,
        capture_output=True,
        timeout=60,
    )
    if built.returncode:
        print(f"FAIL explicit-copy Rust build: {built.stderr.strip()}")
        return 1
    ran = subprocess.run(
        [str(binary)], cwd=ROOT, text=True, capture_output=True, timeout=30
    )
    if ran.returncode or ran.stdout != "2\n1\n":
        print(f"FAIL explicit-copy output: {ran.stdout!r} {ran.stderr!r}")
        return 1
    print("PASS explicit-copy source builds and preserves the borrowed fallback")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
