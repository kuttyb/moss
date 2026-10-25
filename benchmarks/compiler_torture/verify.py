#!/usr/bin/env python3
"""Deterministic Fixture Integrity Verification Tool (Phase 25.4 B1-R1).

Strictly verifies the portable torture benchmark corpus:
1. Suite name, version (1.0.0), and seed (20261025).
2. All source file SHA256 hashes against manifest.json.
3. Whole-directory project tree hashes for multi-module/multi-file projects.
4. Fixture metadata: parameters, generator version, generator seed.
5. Expected acceptance status (pass/fail) when compiler is provided.

Exits with code 0 on complete integrity verification; non-zero on any violation.
Refuses to proceed if any fixture is modified, corrupted, or missing.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT_DIR = Path(__file__).resolve().parent
FIXTURES_DIR = ROOT_DIR / "fixtures"
MANIFEST_PATH = ROOT_DIR / "manifest.json"
EXPECTED_MANIFEST_PATH = ROOT_DIR / "expected" / "validation_manifest.json"

EXPECTED_VERSION = "1.0.0"
EXPECTED_SEED = 20261025
EXPECTED_SUITE = "moss_compiler_torture"


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def project_tree_sha256(dir_path: Path) -> str:
    """Computes deterministic hash across all files in directory tree in sorted order."""
    h = hashlib.sha256()
    for p in sorted(dir_path.rglob("*")):
        if p.is_file():
            h.update(p.relative_to(dir_path).as_posix().encode())
            h.update(p.read_bytes())
    return h.hexdigest()


def verify_corpus(manifest_data: dict, verbose: bool = False) -> tuple[bool, list[str]]:
    errors = []

    # 1. Suite-level metadata
    if manifest_data.get("suite_name") != EXPECTED_SUITE:
        errors.append(f"Suite name mismatch: expected '{EXPECTED_SUITE}', got '{manifest_data.get('suite_name')}'")
    if manifest_data.get("version") != EXPECTED_VERSION:
        errors.append(f"Suite version mismatch: expected '{EXPECTED_VERSION}', got '{manifest_data.get('version')}'")
    if manifest_data.get("seed") != EXPECTED_SEED:
        errors.append(f"Suite seed mismatch: expected {EXPECTED_SEED}, got {manifest_data.get('seed')}")

    fixtures = manifest_data.get("fixtures", [])
    if len(fixtures) != manifest_data.get("total_fixtures"):
        errors.append(f"Fixture count mismatch: total_fixtures={manifest_data.get('total_fixtures')}, actual={len(fixtures)}")

    # 2. Check validation_manifest.json if present
    if EXPECTED_MANIFEST_PATH.is_file():
        try:
            val_data = json.loads(EXPECTED_MANIFEST_PATH.read_text())
            if val_data.get("version") != EXPECTED_VERSION:
                errors.append(f"Validation manifest version mismatch: {val_data.get('version')}")
            expectations = val_data.get("expectations", {})
            for f in fixtures:
                fid = f["id"]
                if fid not in expectations:
                    errors.append(f"Fixture '{fid}' missing from validation_manifest.json")
                else:
                    if expectations[fid]["sha256"] != f["sha256"]:
                        errors.append(f"Validation manifest sha256 mismatch for '{fid}'")
                    if expectations[fid]["expected_status"] != f["expected_status"]:
                        errors.append(f"Validation manifest expected_status mismatch for '{fid}'")
        except Exception as e:
            errors.append(f"Failed to parse validation manifest: {e}")

    # 3. Individual fixture files and hashes
    for idx, f in enumerate(fixtures, start=1):
        fid = f.get("id")
        rel_path = f.get("path")
        expected_sha = f.get("sha256")
        is_project = f.get("is_project", False)

        if not fid or not rel_path or not expected_sha:
            errors.append(f"Malformed fixture entry #{idx}: {f}")
            continue

        target_path = ROOT_DIR / rel_path
        if not target_path.exists():
            errors.append(f"Missing fixture target for '{fid}': {target_path}")
            continue

        # Generator version and seed checks
        if f.get("generator_version") != EXPECTED_VERSION:
            errors.append(f"Fixture '{fid}' generator_version mismatch: {f.get('generator_version')}")
        if f.get("generator_seed") != EXPECTED_SEED:
            errors.append(f"Fixture '{fid}' generator_seed mismatch: {f.get('generator_seed')}")

        # Compute hash
        try:
            if is_project:
                if not target_path.is_dir():
                    errors.append(f"Project fixture '{fid}' target is not a directory: {target_path}")
                    continue
                actual_sha = project_tree_sha256(target_path)
            else:
                if not target_path.is_file():
                    errors.append(f"Single-file fixture '{fid}' target is not a file: {target_path}")
                    continue
                actual_sha = file_sha256(target_path)

            if actual_sha != expected_sha:
                errors.append(
                    f"Integrity violation in '{fid}' ({rel_path}):\n"
                    f"  Expected SHA256: {expected_sha}\n"
                    f"  Actual SHA256:   {actual_sha}"
                )
            elif verbose:
                print(f"  [OK] {fid:<32} {rel_path} ({actual_sha[:12]}...)")
        except Exception as e:
            errors.append(f"Error reading fixture '{fid}' at {target_path}: {e}")

    return len(errors) == 0, errors


def verify_against_compiler(moss_bin: Path, fixtures: list[dict], verbose: bool = False) -> tuple[bool, list[str]]:
    errors = []
    print(f"\nVerifying {len(fixtures)} fixtures against compiler: {moss_bin} ...")

    for f in fixtures:
        fid = f["id"]
        rel_path = f["path"]
        is_project = f.get("is_project", False)
        expected_status = f.get("expected_status")

        target = ROOT_DIR / rel_path
        check_target = str(target / "src" / "main.moss") if is_project else str(target)

        res = subprocess.run([str(moss_bin), "check", check_target, "--json"],
                             capture_output=True, text=True)

        accepted = False
        error_code = ""
        try:
            data = json.loads(res.stdout)
            accepted = data.get("ok") is True
            if not accepted and "error" in data:
                error_code = data["error"].get("code", "")
        except Exception:
            accepted = False

        expected_ok = (expected_status == "pass")
        if accepted != expected_ok:
            errors.append(
                f"Semantic expectation failed for '{fid}':\n"
                f"  Expected: {expected_status}\n"
                f"  Actual:   {'pass' if accepted else 'fail'} (exit={res.returncode}, code='{error_code}')\n"
                f"  Stderr:   {res.stderr.strip()[:200]}"
            )
        elif verbose:
            print(f"  [PASS] {fid:<32} status={expected_status} ok={accepted} code='{error_code}'")

    return len(errors) == 0, errors


def main():
    parser = argparse.ArgumentParser(description="Deterministic Fixture Integrity Verification Tool")
    parser.add_argument("--compiler", help="Optional path to compiler binary for semantic acceptance checks")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose per-fixture logging")
    args = parser.parse_args()

    if not MANIFEST_PATH.is_file():
        sys.exit(f"CRITICAL: Manifest missing at {MANIFEST_PATH}")

    try:
        manifest_data = json.loads(MANIFEST_PATH.read_text())
    except Exception as e:
        sys.exit(f"CRITICAL: Failed to parse manifest.json: {e}")

    print(f"Verifying corpus integrity: {manifest_data.get('suite_name')} v{manifest_data.get('version')}...")
    ok, errors = verify_corpus(manifest_data, verbose=args.verbose)

    if not ok:
        print(f"\n[FAIL] Fixture integrity verification failed with {len(errors)} error(s):")
        for err in errors:
            print(f"  - {err}")
        sys.exit(1)

    print(f"[OK] All {len(manifest_data.get('fixtures', []))} fixtures passed 100% SHA256 integrity verification.")

    if args.compiler:
        comp_bin = Path(args.compiler).resolve()
        if not comp_bin.is_file() or not os.access(comp_bin, os.X_OK):
            sys.exit(f"CRITICAL: Compiler binary not executable: {comp_bin}")
        sem_ok, sem_errors = verify_against_compiler(comp_bin, manifest_data["fixtures"], verbose=args.verbose)
        if not sem_ok:
            print(f"\n[FAIL] Semantic acceptance checks failed with {len(sem_errors)} error(s):")
            for err in sem_errors:
                print(f"  - {err}")
            sys.exit(1)
        print(f"[OK] All {len(manifest_data.get('fixtures', []))} fixtures passed 100% semantic compiler validation.")

    print("\nCorpus verification COMPLETE: Suite integrity is VALID and UNCOMPROMISED.")


if __name__ == "__main__":
    main()
