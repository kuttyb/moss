"""Regression and contract tests for Phase 24 integer bitwise operations."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else "./moss").resolve()
MARGO = ROOT / "margo"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)


def check_diagnostics():
    """Verify that bitwise operators strictly reject non-Int operands with TYPE_MISMATCH."""
    invalid_cases = [
        ("1 | true", "TYPE_MISMATCH"),
        ("false & 2", "TYPE_MISMATCH"),
        ("1.5 ^ 2", "TYPE_MISMATCH"),
        ("~true", "TYPE_MISMATCH"),
        ('~"hello"', "TYPE_MISMATCH"),
        ("1 << 2.0", "TYPE_MISMATCH"),
        ('"abc" >> 1', "TYPE_MISMATCH"),
        ("~1.0", "TYPE_MISMATCH"),
    ]
    for expr, expected_diag in invalid_cases:
        code = f"fn main():\n  x = {expr}\n"
        with tempfile.NamedTemporaryFile("w", suffix=".moss", dir=SCRATCH, delete=False) as f:
            f.write(code)
            temp_path = f.name
        try:
            res = subprocess.run([str(MOSS), "check", temp_path, "--json"],
                                 capture_output=True, text=True)
            assert res.returncode != 0, f"Expected error for '{expr}', got success"
            parsed = json.loads(res.stdout)
            errors = [d.get("code") for d in parsed.get("diagnostics", [])]
            if "error" in parsed and parsed["error"]:
                errors.append(parsed["error"].get("code"))
            assert expected_diag in errors, (
                f"Expected {expected_diag} in diagnostics for '{expr}', got {errors}"
            )
        finally:
            Path(temp_path).unlink(missing_ok=True)


def check_execution_and_abi():
    """Verify runtime results across interpreter, debug native, release native, and source-free ABI."""
    with tempfile.TemporaryDirectory(prefix="phase24-bitwise-", dir=SCRATCH) as name:
        work = Path(name)
        provider = work / "provider"
        consumer = work / "consumer"
        (provider / "src").mkdir(parents=True)
        (consumer / "src").mkdir(parents=True)

        (provider / "Moss.toml").write_text(
            '[package]\nname = "moss.bitwise_lib"\nversion = "0.1.0"\n'
            '[build]\nsource = "src"\n', encoding="utf-8")
        (provider / "src" / "bitwise.moss").write_text(
            'module bitwise\n'
            'export fn bit_and(a: Int, b: Int) -> Int:\n'
            '  return a & b\n'
            'export fn bit_or(a: Int, b: Int) -> Int:\n'
            '  return a | b\n'
            'export fn bit_xor(a: Int, b: Int) -> Int:\n'
            '  return a ^ b\n'
            'export fn bit_not(a: Int) -> Int:\n'
            '  return ~a\n'
            'export fn shl(a: Int, b: Int) -> Int:\n'
            '  return a << b\n'
            'export fn shr(a: Int, b: Int) -> Int:\n'
            '  return a >> b\n', encoding="utf-8")

        (consumer / "Moss.toml").write_text(
            '[package]\nname = "bitwise_consumer"\nversion = "0.1.0"\n'
            '[build]\nsource = "src"\n'
            '[dependencies]\n"moss.bitwise_lib" = { path = "../provider" }\n',
            encoding="utf-8")

        consumer_code = (
            'module app\nimport bitwise\n'
            'fn main():\n'
            '  # Basic bitwise operations\n'
            '  assert((12 & 10) == 8)\n'
            '  assert((12 | 10) == 14)\n'
            '  assert((12 ^ 10) == 6)\n'
            '  assert(~0 == -1)\n'
            '  assert(~(-1) == 0)\n'
            '  assert(~42 == -43)\n'
            '  assert(~(-43) == 42)\n'
            '  # Shifts\n'
            '  assert((1 << 0) == 1)\n'
            '  assert((1 << 4) == 16)\n'
            '  assert((3 << 2) == 12)\n'
            '  assert((-1 << 1) == -2)\n'
            '  assert((16 >> 2) == 4)\n'
            '  assert((1 >> 1) == 0)\n'
            '  assert((-8 >> 1) == -4)\n'
            '  assert((-8 >> 2) == -2)\n'
            '  assert((-1 >> 1) == -1)\n'
            '  # 64-bit boundaries (INT64_MAX, INT64_MIN)\n'
            '  max_int = 9223372036854775807\n'
            '  min_int = -9223372036854775808\n'
            '  assert((max_int >> 1) == 4611686018427387903)\n'
            '  assert((min_int >> 1) == -4611686018427387904)\n'
            '  assert((max_int >> 63) == 0)\n'
            '  assert((min_int >> 63) == -1)\n'
            '  assert((min_int & max_int) == 0)\n'
            '  assert((min_int | max_int) == -1)\n'
            '  assert((min_int ^ max_int) == -1)\n'
            '  assert(~max_int == min_int)\n'
            '  assert(~min_int == max_int)\n'
            '  assert((1 << 63) == min_int)\n'
            '  # Total shift semantics (shifts by >= 64 or < 0)\n'
            '  assert((1 << 64) == 0)\n'
            '  assert((1 << 65) == 0)\n'
            '  assert((1 << 100) == 0)\n'
            '  assert((1 << -1) == 0)\n'
            '  assert((1 << -64) == 0)\n'
            '  assert((max_int << 64) == 0)\n'
            '  assert((min_int << 64) == 0)\n'
            '  assert((16 >> 64) == 0)\n'
            '  assert((16 >> 100) == 0)\n'
            '  assert((16 >> -1) == 0)\n'
            '  assert((16 >> -64) == 0)\n'
            '  assert((-16 >> 64) == -1)\n'
            '  assert((-16 >> 100) == -1)\n'
            '  assert((-16 >> -1) == -1)\n'
            '  assert((-16 >> -64) == -1)\n'
            '  assert((max_int >> 64) == 0)\n'
            '  assert((min_int >> 64) == -1)\n'
            '  # Precedence hierarchy\n'
            '  # ^ binds tighter than |\n'
            '  assert((1 | 2 ^ 3) == 1)\n'
            '  assert(((1 | 2) ^ 3) == 0)\n'
            '  # & binds tighter than ^\n'
            '  assert((1 ^ 2 & 3) == 3)\n'
            '  assert(((1 ^ 2) & 3) == 3)\n'
            '  # << binds tighter than &\n'
            '  assert((3 & 1 << 1) == 2)\n'
            '  assert(((3 & 1) << 1) == 2)\n'
            '  # + binds tighter than <<\n'
            '  assert((1 + 2 << 3) == 24)\n'
            '  assert((1 << 2 + 1) == 8)\n'
            '  # ~ binds tighter than +\n'
            '  assert(~1 + 2 == 0)\n'
            '  # Comparisons bind looser than bitwise\n'
            '  assert(1 | 2 == 3)\n'
            '  assert(7 & 3 == 3)\n'
            '  # Boolean xor word operator remains preserved\n'
            '  assert(true xor false)\n'
            '  assert(not (true xor true))\n'
            '  assert(not (false xor false))\n'
            '  # Calling exported provider functions\n'
            '  assert(bitwise.bit_and(12, 10) == 8)\n'
            '  assert(bitwise.bit_or(12, 10) == 14)\n'
            '  assert(bitwise.bit_xor(12, 10) == 6)\n'
            '  assert(bitwise.bit_not(0) == -1)\n'
            '  assert(bitwise.shl(1, 4) == 16)\n'
            '  assert(bitwise.shr(-8, 1) == -4)\n'
            '  echo 42\n'
        )
        (consumer / "src" / "main.moss").write_text(consumer_code, encoding="utf-8")

        env = os.environ.copy()
        env.update(MOSS=str(MOSS), MARGO_HOME=str(work / "margo-home"), TMPDIR=str(SCRATCH))
        for args in (("build", "--json"), ("run", "--json"),
                     ("run", "--release", "--json"), ("debug",)):
            result = subprocess.run([str(MARGO), *args], cwd=consumer, env=env,
                                    capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(f"margo {' '.join(args)} failed:\n{result.stdout}\n{result.stderr}")
            if args[0] in ("run", "debug") and result.stdout != "42\n":
                raise AssertionError(f"margo {' '.join(args)}: {result.stdout!r}")

        # Rebuild consumer against precompiled provider only (source-free ABI)
        source_free = work / "source_free"
        (source_free / "src").mkdir(parents=True)
        shutil.copy2(consumer / "src/main.moss", source_free / "src/main.moss")
        (source_free / "Moss.toml").write_text(
            '[project]\nname="bitwise_source_free"\nversion="0.1.0"\n',
            encoding="utf-8")
        provider_source = provider / "src/bitwise.moss"
        provider_source.rename(provider / "src/bitwise.removed")
        compiled_provider = provider / "build/debug"
        assert (compiled_provider / "bitwise.mossi").exists()
        assert (compiled_provider / "libbitwise.rlib").exists()
        source_free_env = env | {"MOSS_MODULE_PATH": str(compiled_provider)}
        for args in (("check", "src/main.moss", "--json"), ("build", "--json")):
            result = subprocess.run([str(MOSS), *args], cwd=source_free,
                                    env=source_free_env, capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(f"source-free {' '.join(args)} failed:\n{result.stdout}\n{result.stderr}")
            parsed = json.loads(result.stdout)
            assert parsed["ok"] is True, parsed
            if args[0] == "build":
                executable = Path(parsed["result"]["artifacts"]["executable"])
                output = subprocess.run([str(executable)], cwd=source_free,
                                        env=source_free_env, capture_output=True, text=True)
                assert output.returncode == 0 and output.stdout == "42\n", output


def main():
    print("Testing bitwise diagnostics...")
    check_diagnostics()
    print("Testing bitwise execution across backends and ABI...")
    check_execution_and_abi()
    print("All Phase 24 bitwise operation checks passed.")


if __name__ == "__main__":
    main()
