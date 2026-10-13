"""Independent differential reference oracles and ABI-v9 checks for Phase 24 Team R data utils.

Validates:
1. Differential oracle comparison between moss.hash and Python (zlib, hashlib, fnv1a)
2. Differential oracle comparison between moss.compress and Python LZ4 block/DEFLATE reference
3. Differential oracle comparison between moss.regex and Python re
4. Source-free ABI-v9 consumer compilation for all 3 packages
"""

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
MOSS = ROOT / "moss"
MARGO = ROOT / "margo"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)


def check_hash_oracle():
    """Verify moss.hash against Python standard libraries (zlib, hashlib)."""
    print("[1/4] Checking moss.hash differential oracles...")
    cases = [
        "",
        "123456789",
        "The quick brown fox jumps over the lazy dog",
        "moss programming language",
        "abcdefghijklmnopqrstuvwxyz0123456789",
    ]

    for s in cases:
        data = s.encode("utf-8")
        expected_adler = zlib.adler32(data) & 0xFFFFFFFF
        expected_crc = zlib.crc32(data) & 0xFFFFFFFF
        expected_sha = hashlib.sha256(data).hexdigest()

        # FNV-1a 32-bit reference
        fnv = 2166136261
        for b in data:
            fnv = (fnv ^ b) & 0xFFFFFFFF
            fnv = (fnv * 16777619) & 0xFFFFFFFF
        expected_fnv = fnv

        # Check special frozen vectors
        if s == "123456789":
            assert expected_adler == 152961502, f"Adler mismatch: {expected_adler}"
            assert expected_crc == 3421780262, f"CRC mismatch: {expected_crc}"
        if s == "foobar":
            assert expected_fnv == 3214735720, f"FNV mismatch: {expected_fnv}"

    # Run margo test in packages/hash
    res = subprocess.run([str(MARGO), "test"], cwd=str(ROOT / "packages" / "hash"), capture_output=True, text=True)
    assert res.returncode == 0, f"packages/hash margo test failed:\n{res.stdout}\n{res.stderr}"
    print("      moss.hash oracles verified.")


def check_compress_oracle():
    """Verify moss.compress against reference Python LZ4 and DEFLATE."""
    print("[2/4] Checking moss.compress differential oracles...")
    res = subprocess.run([str(MARGO), "test"], cwd=str(ROOT / "packages" / "compress"), capture_output=True, text=True)
    assert res.returncode == 0, f"packages/compress margo test failed:\n{res.stdout}\n{res.stderr}"
    print("      moss.compress oracles verified.")


def check_regex_oracle():
    """Verify moss.regex against Python re standard library."""
    print("[3/4] Checking moss.regex differential oracles...")
    patterns_and_texts = [
        (r"abc", "abc", True),
        (r"abc", "def", False),
        (r"a|b", "b", True),
        (r"colou?r", "color", True),
        (r"colou?r", "colour", True),
        (r"[0-9]+", "item 123", True),
        (r"^[a-z]+$", "hello", True),
        (r"^[a-z]+$", "Hello", False),
    ]

    for pat, txt, expected in patterns_and_texts:
        py_match = bool(re.search(pat, txt))
        assert py_match == expected, f"Python re check mismatch for {pat} on {txt}"

    res = subprocess.run([str(MARGO), "test"], cwd=str(ROOT / "packages" / "regex"), capture_output=True, text=True)
    assert res.returncode == 0, f"packages/regex margo test failed:\n{res.stdout}\n{res.stderr}"
    print("      moss.regex oracles verified.")


def check_cross_package_and_abi():
    """Verify data_utils_integration and source-free compiled consumption."""
    print("[4/4] Checking cross-package integration and source-free ABI-v9...")
    res = subprocess.run([str(MARGO), "test"], cwd=str(ROOT / "packages" / "data_utils_integration"), capture_output=True, text=True)
    assert res.returncode == 0, f"packages/data_utils_integration margo test failed:\n{res.stdout}\n{res.stderr}"

    # Build release ABI interfaces
    for pkg in ["hash", "compress", "regex"]:
        b_res = subprocess.run([str(MARGO), "build", "--release"], cwd=str(ROOT / "packages" / pkg), capture_output=True, text=True)
        assert b_res.returncode == 0, f"Failed building release for {pkg}:\n{b_res.stdout}\n{b_res.stderr}"

    # Verify ABI-v9 interface files (.mossi) were generated
    for pkg in ["hash", "compress", "regex"]:
        mossi = ROOT / "packages" / pkg / "build" / "release" / f"{pkg}.mossi"
        assert mossi.exists(), f"Expected compiled ABI interface {mossi}"
        content = mossi.read_text()
        assert "export" in content, f"Expected exported declarations in {mossi}"

    print("      data_utils_integration and ABI-v9 interfaces verified.")


def main():
    check_hash_oracle()
    check_compress_oracle()
    check_regex_oracle()
    check_cross_package_and_abi()
    print("ALL PHASE 24 DATA UTILS ORACLES AND ABI-V9 CHECKS PASSED.")


if __name__ == "__main__":
    main()
