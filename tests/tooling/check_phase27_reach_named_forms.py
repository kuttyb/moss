#!/usr/bin/env python3
"""Reach-walk tests for constructor labels and rejected function labels.

Constructor labels are authorized by LANGUAGE_SYNTAX.md §Modules and its
function/message argument rule. Ordinary function labels remain rejected.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
LEGACY = Path(sys.argv[1]).resolve()
COMPILER = Path(sys.argv[2]).resolve()
SCRATCH = ROOT / "tmp/phase27-2/reach-named-forms"


def run(binary: Path, source: Path, env: dict[str, str], *, ok: bool):
    result = subprocess.run([str(binary), "check", str(source), "--json"],
                            cwd=source.parent.parent, env=env, text=True,
                            capture_output=True, timeout=60)
    assert (result.returncode == 0) == ok, (result.stdout, result.stderr)
    return result


def package_check(binary: Path, package: Path, env: dict[str, str]):
    package_env = dict(env)
    package_env["MOSS"] = str(binary)
    result = subprocess.run(
        [str(ROOT / "margo"), "--root", str(package), "build", "--check", "--json"],
        cwd=ROOT, env=package_env, text=True, capture_output=True, timeout=120)
    assert result.returncode != 0, (result.stdout, result.stderr)
    outer = json.loads(result.stdout)
    assert outer.get("ok") is False, outer
    detail = outer.get("error", "")
    start = detail.find("{\n")
    assert start >= 0, detail
    inner = json.loads(detail[start:])
    error = inner.get("error") or {}
    return (error.get("code"), Path(error.get("source_file", "")).name,
            error.get("line"))


def main() -> int:
    shutil.rmtree(SCRATCH, ignore_errors=True)
    source_root = SCRATCH / "src"
    source_root.mkdir(parents=True)
    (SCRATCH / "Moss.toml").write_text(
        '[package]\nname = "reach_named_forms"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    entry = source_root / "main.moss"
    entry.write_text(
        "module app\nimport ops\n"
        "type Point:\n  x: Int\n"
        "fn main():\n  p = Point(x = ops.g(7))\n  echo p.x\n")
    (source_root / "ops.moss").write_text(
        "module ops\n"
        "export fn g(value: Int) -> Int:\n  return value + 1\n"
        "export fn unused() -> Int:\n  return absent()\n")

    env = dict(os.environ)
    env["MOSS_PHASE26_C1_SCOPE_TRACE"] = "1"
    env.pop("MOSS_MODULE_PATH", None)
    env.pop("MOSS_SOURCE_ROOTS", None)
    env.pop("MOSS_FAST_DEBUG_SOURCE_ROOTS", None)
    checked_by_mode = {}
    for mode in ("off", "shadow"):
        env["MOSS_EST"] = mode
        result = run(COMPILER, entry, env, ok=True)
        identities = {
            row.split("|", 3)[3]
            for row in result.stderr.splitlines()
            if row.startswith("MOSS_C1_SCOPE_ID|function|")
        }
        expected = {
            "callable-v1:module-v1:reach_named_forms@0.1.0::ops:fn:g",
        }
        assert identities == expected, (mode, identities, expected)
        main_ids = [row.split("|", 3)[3]
                    for row in result.stderr.splitlines()
                    if row.startswith("MOSS_C1_SCOPE_ID|main|")]
        assert len(main_ids) == 1, (mode, main_ids)
        checked_by_mode[mode] = identities
    assert checked_by_mode["off"] == checked_by_mode["shadow"]

    rejected = SCRATCH / "rejected.moss"
    rejected.write_text(
        "fn g(value: Int) -> Int:\n  return value\n"
        "fn f(value: Int) -> Int:\n  return value\n"
        "fn main():\n  echo f(x = g(1))\n")
    diagnostics = {}
    for mode in ("off", "shadow"):
        env["MOSS_EST"] = mode
        result = run(COMPILER, rejected, env, ok=False)
        response = json.loads(result.stdout)
        error = response.get("error") or {}
        diagnostics[mode] = (error.get("code"), error.get("line"))
        assert diagnostics[mode][0] == "CALL_NAMED_ARGUMENT_UNSUPPORTED", response
    assert diagnostics["off"] == diagnostics["shadow"], diagnostics

    # A same-named export in another imported module is not a target of the
    # qualified call. The Modules section defines this name binding; the
    # legacy splitter loses the qualifier and checks the broken homonym.
    qualified_root = SCRATCH / "qualified"
    qualified_src = qualified_root / "src"
    qualified_src.mkdir(parents=True)
    (qualified_root / "Moss.toml").write_text(
        '[package]\nname = "qualified_reach"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    qualified_entry = qualified_src / "main.moss"
    qualified_entry.write_text(
        "module app\nimport node\nimport token\n"
        "fn main():\n  echo node.kind_num()\n")
    (qualified_src / "node.moss").write_text(
        "module node\nexport fn kind_num() -> Int:\n  return 1\n")
    (qualified_src / "token.moss").write_text(
        "module token\nexport fn kind_num() -> Int:\n  return absent()\n")
    qualified_results = {}
    qualified_sets = {}
    for mode, binary, ok in (
        ("legacy", LEGACY, False), ("tree", COMPILER, True)):
        env["MOSS_EST"] = "off" if mode == "legacy" else "shadow"
        result = run(binary, qualified_entry, env, ok=ok)
        response = json.loads(result.stdout)
        qualified_results[mode] = (
            response.get("ok"), (response.get("error") or {}).get("code"))
        qualified_sets[mode] = {
            row.split("|", 3)[3]
            for row in result.stderr.splitlines()
            if row.startswith("MOSS_C1_SCOPE_ID|function|")
        }
    assert qualified_sets["tree"] == {
        "callable-v1:module-v1:qualified_reach@0.1.0::node:fn:kind_num"}
    assert any("token:fn:kind_num" in identity
               for identity in qualified_sets["legacy"])
    assert qualified_results["legacy"][0] is False
    assert qualified_results["tree"][0] is True
    package_diagnostics = {}
    for mode, binary in (("legacy", LEGACY), ("tree", COMPILER)):
        env["MOSS_EST"] = "off" if mode == "legacy" else "shadow"
        package_diagnostics[mode] = package_check(binary, qualified_root, env)
        assert package_diagnostics[mode] == (
            "UNKNOWN_SYMBOL_OR_TYPE", "token.moss", 3), package_diagnostics
    assert package_diagnostics["legacy"] == package_diagnostics["tree"]

    ambiguous_root = SCRATCH / "ambiguous"
    ambiguous_src = ambiguous_root / "src"
    ambiguous_src.mkdir(parents=True)
    (ambiguous_root / "Moss.toml").write_text(
        '[package]\nname = "ambiguous_reach"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    ambiguous_entry = ambiguous_src / "main.moss"
    ambiguous_entry.write_text(
        "type Left:\n  value: Int\n  fn key() -> Int:\n    return value\n"
        "type Right:\n  value: Int\n  fn key() -> Int:\n    return value\n"
        "fn main():\n  left = Left(value: 1)\n  echo left.key()\n")
    ambiguous_methods = {}
    for mode, binary in (("legacy", LEGACY), ("tree", COMPILER)):
        env["MOSS_EST"] = "off" if mode == "legacy" else "shadow"
        result = run(binary, ambiguous_entry, env, ok=True)
        ambiguous_methods[mode] = {
            row.split("|", 3)[3]
            for row in result.stderr.splitlines()
            if row.startswith("MOSS_C1_SCOPE_ID|method|")
        }
    expected_ambiguous = {"method:Left.key@3", "method:Right.key@7"}
    assert ambiguous_methods["legacy"] == expected_ambiguous, ambiguous_methods
    assert ambiguous_methods["tree"] == expected_ambiguous, ambiguous_methods
    print("PASS Point(x = ops.g(7)) reaches exactly ops.g; entry main is traced separately")
    print("PASS f(x = g(1)) rejects identically: CALL_NAMED_ARGUMENT_UNSUPPORTED")
    print("PASS node.kind_num excludes token.kind_num; legacy rejects, tree accepts")
    print("PASS package-mode Margo check rejects both paths identically: "
          f"{package_diagnostics['tree']}")
    print("PASS left.key() retains both same-name method candidates on both paths")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
