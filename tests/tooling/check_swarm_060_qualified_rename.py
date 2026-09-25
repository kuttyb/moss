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


def main():
    compiler = REPO / "moss"
    if not compiler.exists():
        compiler = Path(os.environ.get("MOSS", "moss"))
    print("Testing SWARM-060 semantic rename of qualified entities...")
    test_qualified_rename_and_ambiguity_rejection(compiler)
    print("SWARM-060 tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
