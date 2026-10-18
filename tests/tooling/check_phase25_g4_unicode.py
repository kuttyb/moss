#!/usr/bin/env python3
"""Phase 25 G4 Test Suite: Unicode String Metadata, Scalar Char, and ABI-v10 Migration."""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else "./moss").resolve()
MARGO = ROOT / "margo"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)


def run_cmd(cmd, cwd=ROOT):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def run_code_in_all_modes(moss_code, test_name):
    """Execute code in Fast Debug, Native Debug, and Native Release."""
    with tempfile.NamedTemporaryFile("w", suffix=".moss", dir=SCRATCH, delete=False) as f:
        f.write(moss_code)
        p = f.name
    try:
        # 1) Fast Debug
        res_interp = run_cmd([str(MOSS), "run", "--interp", p])
        assert res_interp.returncode == 0, (
            f"[{test_name}] Fast Debug failed (code {res_interp.returncode}):\n{res_interp.stderr}\nStdout:\n{res_interp.stdout}"
        )
        interp_out = res_interp.stdout

        # 2) Native Debug
        rs_debug = str(Path(p).with_suffix(".debug.rs"))
        bin_debug = str(Path(p).with_suffix(".debug.bin"))
        res_gen_dbg = run_cmd([str(MOSS), p, "--debug", "-o", rs_debug])
        assert res_gen_dbg.returncode == 0, (
            f"[{test_name}] moss --debug code generation failed:\n{res_gen_dbg.stderr}"
        )
        res_rustc_dbg = run_cmd(["rustc", "--crate-name", "test_crate", rs_debug, "-o", bin_debug])
        assert res_rustc_dbg.returncode == 0, (
            f"[{test_name}] rustc debug compilation failed:\n{res_rustc_dbg.stderr}"
        )
        res_run_dbg = run_cmd([bin_debug])
        assert res_run_dbg.returncode == 0, (
            f"[{test_name}] Native Debug execution failed:\n{res_run_dbg.stderr}"
        )
        dbg_out = res_run_dbg.stdout

        # 3) Native Release
        rs_rel = str(Path(p).with_suffix(".release.rs"))
        bin_rel = str(Path(p).with_suffix(".release.bin"))
        res_gen_rel = run_cmd([str(MOSS), p, "-O", "-o", rs_rel])
        assert res_gen_rel.returncode == 0, (
            f"[{test_name}] moss -O code generation failed:\n{res_gen_rel.stderr}"
        )
        res_rustc_rel = run_cmd(["rustc", "-O", "--crate-name", "test_crate", rs_rel, "-o", bin_rel])
        assert res_rustc_rel.returncode == 0, (
            f"[{test_name}] rustc release compilation failed:\n{res_rustc_rel.stderr}"
        )
        res_run_rel = run_cmd([bin_rel])
        assert res_run_rel.returncode == 0, (
            f"[{test_name}] Native Release execution failed:\n{res_run_rel.stderr}"
        )
        rel_out = res_run_rel.stdout

        # Assert stdout agreement
        assert interp_out == dbg_out == rel_out, (
            f"[{test_name}] Output mismatch across engines:\n"
            f"  Fast Debug:     {interp_out!r}\n"
            f"  Native Debug:   {dbg_out!r}\n"
            f"  Native Release: {rel_out!r}"
        )
    finally:
        for ext in (".moss", ".debug.rs", ".debug.bin", ".release.rs", ".release.bin"):
            Path(p).with_suffix(ext).unlink(missing_ok=True)


def test_char_literal_and_constructors():
    print("Testing scalar Char literals and constructor...")
    code = """
fn main():
  c_ascii = 'A'
  c_unicode = '😀'
  c_newline = '\\n'

  assertEqual(c_ascii.codepoint(), 65)
  assertEqual(c_ascii.to_string(), "A")
  assertEqual(c_unicode.codepoint(), 128512)
  assertEqual(c_unicode.to_string(), "😀")
  assertEqual(c_newline.codepoint(), 10)

  # Constructor Char(Int) -> Char
  c1 = Char(65)
  assertEqual(c1, c_ascii)
  assertEqual(c1.codepoint(), 65)

  c_zero = Char(0)
  assertEqual(c_zero.codepoint(), 0)

  c_d7ff = Char(55295) # 0xD7FF boundary before surrogates
  assertEqual(c_d7ff.codepoint(), 55295)

  c_e000 = Char(57344) # 0xE000 boundary after surrogates
  assertEqual(c_e000.codepoint(), 57344)

  c_max = Char(1114111) # 0x10FFFF max Unicode scalar
  assertEqual(c_max.codepoint(), 1114111)

  # Char is distinct from String; compare through an explicit to_string()
  assertEqual(c_ascii.to_string() == "A", true)
  assertEqual(c_ascii.to_string() == "B", false)
  assertEqual(c_unicode.to_string() == "😀", true)
  assertEqual(c_unicode.to_string() != "😀", false)
  assertEqual("A" == c_ascii.to_string(), true)
  assertEqual(c_ascii == 'B', false)
  echo "CHAR LITERAL AND CTOR PASS"
"""
    run_code_in_all_modes(code, "Char Literal and Constructor")
    print("  PASS: Scalar Char literals and constructor verified.")


def test_string_unicode_traversal():
    print("Testing String Unicode indexing, char_at, chars, and sparse metadata...")
    code = """
fn main():
  # Multi-byte UTF-8 string with ASCII, 2-byte, 3-byte, and 4-byte codepoints
  s = "Hello, 世界 🌍! café"
  # 'H','e','l','l','o',',',' ','世','界',' ','🌍','!',' ','c','a','f','é' -> 17 codepoints
  assertEqual(s.length(), 17)
  assertEqual(s.char_at(0), 'H')
  assertEqual(s.char_at(7), '世')
  assertEqual(s.char_at(8), '界')
  assertEqual(s.char_at(10), '🌍')
  assertEqual(s.char_at(16), 'é')

  # Char is distinct from String: an explicit to_string() gives a String
  assertEqual(s.char_at(0).to_string(), "H")
  assertEqual(s.char_at(7).to_string(), "世")
  assertEqual(s.char_at(10).to_string(), "🌍")
  assertEqual(s.char_at(16).to_string(), "é")

  # chars() -> Vector[Char]
  chars = s.chars()
  assertEqual(chars |> count, 17)
  assertEqual(chars[0], 'H')
  assertEqual(chars[7], '世')
  assertEqual(chars[8], '界')
  assertEqual(chars[10], '🌍')
  assertEqual(chars[16], 'é')

  # Verify 64+ code-point string requiring sparse offset table
  var long_str = ""
  for i in range(0, 100):
    long_str = long_str + "🙂"
  assertEqual(long_str.length(), 100)
  assertEqual(long_str.char_at(0), '🙂')
  assertEqual(long_str.char_at(63), '🙂')
  assertEqual(long_str.char_at(64), '🙂') # Crosses 64-codepoint boundary
  assertEqual(long_str.char_at(99), '🙂')
  echo "STRING UNICODE TRAVERSAL PASS"
"""
    run_code_in_all_modes(code, "String Unicode Traversal")
    print("  PASS: String Unicode traversal verified.")


def test_no_unilateral_abi_bump():
    """ABI v10 is one coordinated Team G/Team S/L7 transition.

    Until it lands, this branch must keep emitting the ABI v9 contract rather
    than relabelling an unchanged String representation as v10.
    """
    print("Testing that the native ABI remains v9 pending the joint v10 cut...")
    with tempfile.TemporaryDirectory(dir=SCRATCH) as td:
        proj = Path(td)
        (proj / "Moss.toml").write_text('[package]\nname = "abi_test"\nversion = "0.1.0"\n')
        src_dir = proj / "src"
        src_dir.mkdir()
        (src_dir / "provider.moss").write_text(
            "module provider\nexport fn initial(s: String) -> Char:\n  return s.char_at(0)\n")
        (src_dir / "main.moss").write_text(
            "module app\nimport provider\nfn main():\n  echo provider.initial(\"é\")\n")
        res_bld = run_cmd([str(MOSS), "build", "--json"], cwd=proj)
        assert res_bld.returncode == 0, f"Build failed: {res_bld.stdout}{res_bld.stderr}"
        mossi_text = (proj / "build" / "debug" / "provider.mossi").read_text()
        assert "\nnative_abi 9\n" in mossi_text, "provider must still declare native_abi 9"
        assert "native_abi 10" not in mossi_text
    print("  PASS: ABI v9 retained; Char crosses the provider boundary.")


def main():
    print("=== Running Phase 25 G4 Unicode & Char Suite ===")
    test_char_literal_and_constructors()
    test_string_unicode_traversal()
    test_no_unilateral_abi_bump()
    print("=== All Phase 25 G4 Tests Passed Cleanly! ===")


if __name__ == "__main__":
    main()
