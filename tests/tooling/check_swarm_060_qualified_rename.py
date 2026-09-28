#!/usr/bin/env python3
"""Phase 15.14 SWARM-060 semantic rename of qualified entities regression coverage.

Verifies:
- moss edit rename resolves module-qualified entities using canonical semantic identity
- Two modules containing the same simple function name
- Qualified selection of one of them renames only that entity and its call sites
- Other module's same-named function is untouched
- Unqualified ambiguous request fails with EDIT_TARGET_AMBIGUOUS
- Imports and qualified references remain valid and checked
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[2]


def run(command, *, cwd, env=None):
    return subprocess.run(command, cwd=cwd, env=env or dict(os.environ), text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)


def require_ok(result, context):
    if result.returncode:
        raise AssertionError(f"{context} failed (rc={result.returncode}):\n"
                             f"stdout: {result.stdout}\nstderr: {result.stderr}")


def test_qualified_rename_and_ambiguity_rejection(compiler):
    with tempfile.TemporaryDirectory() as tmpdir:
        root = Path(tmpdir)
        src = root / "src"
        src.mkdir(parents=True)

        (root / "Moss.toml").write_text(
            '[package]\nname = "rename_proj"\nversion = "0.1.0"\n\n'
            '[build]\nsource = "src"\n', encoding="utf-8")

        # mod_a has helper
        (src / "mod_a.moss").write_text(
            'module mod_a\n\n'
            'export fn helper(x: Int) -> Int:\n'
            '  return x + 10\n', encoding="utf-8")

        # mod_b has helper
        (src / "mod_b.moss").write_text(
            'module mod_b\n\n'
            'export fn helper(x: Int) -> Int:\n'
            '  return x * 10\n', encoding="utf-8")

        # main imports both and calls both
        (src / "main.moss").write_text(
            'module rename_proj\n'
            'import mod_a\n'
            'import mod_b\n\n'
            'fn main():\n'
            '  a = mod_a.helper(5)\n'
            '  b = mod_b.helper(5)\n', encoding="utf-8")

        # Check project is initially valid
        require_ok(run([str(compiler), "check", str(src / "main.moss"), "--json"], cwd=root),
                   "initial check")

        # 1. Unqualified rename of "helper" must fail with EDIT_TARGET_AMBIGUOUS
        ambig_res = run([str(compiler), "edit", "rename", "helper", "renamed_helper", "--json"], cwd=root)
        if ambig_res.returncode == 0:
            raise AssertionError(f"Expected unqualified rename to fail with ambiguity, but succeeded:\n{ambig_res.stdout}")
        ambig_json = json.loads(ambig_res.stdout)
        if ambig_json.get("ok"):
            raise AssertionError(f"Expected ok: false, got {ambig_json}")
        if ambig_json.get("error", {}).get("code") != "EDIT_TARGET_AMBIGUOUS":
            raise AssertionError(f"Expected EDIT_TARGET_AMBIGUOUS, got:\n{ambig_json}")

        # 2. Qualified rename of "mod_a.helper" to "calc_a" must succeed
        rename_res = run([str(compiler), "edit", "rename", "mod_a.helper", "calc_a", "--json"], cwd=root)
        require_ok(rename_res, "qualified rename mod_a.helper -> calc_a")
        rename_json = json.loads(rename_res.stdout)
        if not rename_json.get("ok"):
            raise AssertionError(f"Rename failed: {rename_json}")

        # Verify mod_a.moss was updated to calc_a
        mod_a_text = (src / "mod_a.moss").read_text(encoding="utf-8")
        if "fn calc_a(" not in mod_a_text or "fn helper(" in mod_a_text:
            raise AssertionError(f"mod_a.moss was not properly updated:\n{mod_a_text}")

        # Verify mod_b.moss was UNTOUCHED (still has helper)
        mod_b_text = (src / "mod_b.moss").read_text(encoding="utf-8")
        if "fn helper(" not in mod_b_text or "calc_a" in mod_b_text:
            raise AssertionError(f"mod_b.moss was incorrectly modified:\n{mod_b_text}")

        # Verify main.moss has mod_a.calc_a and still has mod_b.helper
        main_text = (src / "main.moss").read_text(encoding="utf-8")
        if "mod_a.calc_a(5)" not in main_text:
            raise AssertionError(f"main.moss missing mod_a.calc_a(5):\n{main_text}")
        if "mod_b.helper(5)" not in main_text:
            raise AssertionError(f"main.moss lost mod_b.helper(5):\n{main_text}")

        # Verify the modified project checks cleanly
        require_ok(run([str(compiler), "check", str(src / "main.moss"), "--json"], cwd=root),
                   "post-rename check")

    print("  [PASS] qualified entity rename and ambiguity rejection")


def test_qualified_rename_multi_file_same_module(compiler):
    with tempfile.TemporaryDirectory() as tmpdir:
        root = Path(tmpdir)
        src = root / "src"
        src.mkdir(parents=True)

        (root / "Moss.toml").write_text(
            '[package]\nname = "multi_file_rename_proj"\nversion = "0.1.0"\n\n'
            '[build]\nsource = "src"\n', encoding="utf-8")

        # mod_a split across two files: part1 declares helper
        (src / "mod_a_part1.moss").write_text(
            'module mod_a\n\n'
            'export fn helper(x: Int) -> Int:\n'
            '  return x + 1\n', encoding="utf-8")

        # mod_a part2 calls helper unqualified
        (src / "mod_a_part2.moss").write_text(
            'module mod_a\n\n'
            'export fn caller(x: Int) -> Int:\n'
            '  return helper(x)\n', encoding="utf-8")

        # mod_b has its own same-named helper
        (src / "mod_b.moss").write_text(
            'module mod_b\n\n'
            'export fn helper(x: Int) -> Int:\n'
            '  return x * 10\n', encoding="utf-8")

        # main imports both and calls mod_a.helper, mod_a.caller, and mod_b.helper
        (src / "main.moss").write_text(
            'module multi_file_rename_proj\n'
            'import mod_a\n'
            'import mod_b\n\n'
            'fn main():\n'
            '  a1 = mod_a.helper(5)\n'
            '  a2 = mod_a.caller(5)\n'
            '  b = mod_b.helper(5)\n', encoding="utf-8")

        # Initial check must succeed
        require_ok(run([str(compiler), "check", str(src / "main.moss"), "--json"], cwd=root),
                   "initial check")

        # 1. Unqualified ambiguous rename must fail with EDIT_TARGET_AMBIGUOUS
        ambig_res = run([str(compiler), "edit", "rename", "helper", "calc_a", "--json"], cwd=root)
        if ambig_res.returncode == 0:
            raise AssertionError(f"Expected unqualified rename to fail with ambiguity, but succeeded:\n{ambig_res.stdout}")
        ambig_json = json.loads(ambig_res.stdout)
        if ambig_json.get("ok"):
            raise AssertionError(f"Expected ok: false, got {ambig_json}")
        if ambig_json.get("error", {}).get("code") != "EDIT_TARGET_AMBIGUOUS":
            raise AssertionError(f"Expected EDIT_TARGET_AMBIGUOUS, got:\n{ambig_json}")

        # 2. Qualified rename mod_a.helper -> calc_a
        rename_res = run([str(compiler), "edit", "rename", "mod_a.helper", "calc_a", "--json"], cwd=root)
        require_ok(rename_res, "qualified rename mod_a.helper -> calc_a across multi-file module")
        rename_json = json.loads(rename_res.stdout)
        if not rename_json.get("ok"):
            raise AssertionError(f"Rename failed: {rename_json}")

        # 3. Verify mod_a_part1.moss declaration changed
        part1_text = (src / "mod_a_part1.moss").read_text(encoding="utf-8")
        if "fn calc_a(" not in part1_text or "fn helper(" in part1_text:
            raise AssertionError(f"mod_a_part1.moss was not properly updated:\n{part1_text}")

        # 4. Verify unqualified same-module call in mod_a_part2.moss changed
        part2_text = (src / "mod_a_part2.moss").read_text(encoding="utf-8")
        if "calc_a(x)" not in part2_text or "helper(x)" in part2_text:
            raise AssertionError(f"mod_a_part2.moss unqualified call was not updated:\n{part2_text}")

        # 5. Verify qualified call in main.moss changed, and mod_b call was untouched
        main_text = (src / "main.moss").read_text(encoding="utf-8")
        if "mod_a.calc_a(5)" not in main_text:
            raise AssertionError(f"main.moss missing mod_a.calc_a(5):\n{main_text}")
        if "mod_a.helper(5)" in main_text:
            raise AssertionError(f"main.moss still contains mod_a.helper(5):\n{main_text}")
        if "mod_b.helper(5)" not in main_text:
            raise AssertionError(f"main.moss lost mod_b.helper(5):\n{main_text}")

        # 6. Verify mod_b.moss was untouched
        mod_b_text = (src / "mod_b.moss").read_text(encoding="utf-8")
        if "fn helper(" not in mod_b_text or "calc_a" in mod_b_text:
            raise AssertionError(f"mod_b.moss was incorrectly modified:\n{mod_b_text}")

        # 7. Post-rename project checks cleanly
        require_ok(run([str(compiler), "check", str(src / "main.moss"), "--json"], cwd=root),
                   "post-rename check")

    print("  [PASS] qualified rename across multi-file module and ambiguity rejection")


def main():
    compiler = REPO / "moss"
    if not compiler.exists():
        compiler = Path(os.environ.get("MOSS", "moss"))
    print("Testing SWARM-060 semantic rename of qualified entities...")
    test_qualified_rename_and_ambiguity_rejection(compiler)
    test_qualified_rename_multi_file_same_module(compiler)
    print("SWARM-060 tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
