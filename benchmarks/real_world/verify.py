#!/usr/bin/env python3
"""Verify SHA256 integrity of the Moss Real-World Benchmark Corpus.

Validates that all real-world fixtures referenced in manifest.json match their
recorded cryptographic hashes.

Exit code 0 on 100% match; non-zero if any hash differs or file is missing.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sys

BASE_DIR = Path(__file__).resolve().parent
REPO_ROOT = BASE_DIR.parent.parent
MANIFEST_PATH = BASE_DIR / "manifest.json"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()


def verify_real_world_manifest() -> bool:
    if not MANIFEST_PATH.is_file():
        print(f"FAIL: Missing manifest: {MANIFEST_PATH}", file=sys.stderr)
        return False

    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    workloads = manifest.get("workloads", [])
    if not workloads:
        print("FAIL: No workloads found in manifest", file=sys.stderr)
        return False

    errors = 0
    verified = 0

    for wl in workloads:
        rel_path = wl["entry_file"]
        target_path = REPO_ROOT / rel_path
        if not target_path.is_file():
            print(f"FAIL: Missing fixture file: {rel_path}", file=sys.stderr)
            errors += 1
            continue

        actual_sha = sha256_file(target_path)
        expected_sha = wl["sha256"]
        if actual_sha != expected_sha:
            print(
                f"FAIL: SHA256 mismatch for {wl['id']} ({rel_path}):\n"
                f"  Expected: {expected_sha}\n"
                f"  Actual:   {actual_sha}",
                file=sys.stderr,
            )
            errors += 1
        else:
            verified += 1

    if errors > 0:
        print(f"FAIL: {errors} real-world fixture(s) failed verification.", file=sys.stderr)
        return False

    print(f"PASS: 100% of {verified} real-world workloads verified against manifest.")
    return True


if __name__ == "__main__":
    if not verify_real_world_manifest():
        sys.exit(1)
