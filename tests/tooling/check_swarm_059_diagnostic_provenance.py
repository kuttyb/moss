#!/usr/bin/env python3
"""Phase 15.14 SWARM-059 diagnostic source provenance regression coverage.

Verifies:
- In a multi-file project, diagnostics identify the physical source file that
  actually contains the offending construct rather than attributing the error
  to the root module.
- Preserves provenance through parsing, module composition, checking, and
  structured JSON diagnostics.
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


def env_for(compiler, **extra):
    env = dict(os.environ)
    env["MOSS"] = str(compiler)
    env.update(extra)
    return env


def test_semantic_error_provenance_non_root(compiler):
    with tempfile.TemporaryDirectory() as tmpdir:
        root = Path(tmpdir)
        src = root / "src"
        src.mkdir(parents=True)

        (root / "Moss.toml").write_text(
            '[package]\nname = "provenance_proj"\nversion = "0.1.0"\n\n'
            '[build]\nsource = "src"\n', encoding="utf-8")

        (src / "main.moss").write_text(
            'module provenance_proj\n'
            'import helper\n'
            'fn main():\n'
            '  x = helper.compute(10)\n', encoding="utf-8")

        # helper.moss has a semantic type error at line 5
        helper_path = src / "helper.moss"
        helper_path.write_text(
            'module helper\n'
            'export fn compute(x: Int) -> Int:\n'
            '  y: NonExistentType = x\n'
            '  return y\n', encoding="utf-8")

        # 1. Human-readable diagnostic from root check
        res_human = run([str(compiler), "check", str(src / "main.moss")], cwd=root)
        if res_human.returncode == 0:
            raise AssertionError(f"Expected check to fail, but succeeded:\n{res_human.stdout}")
        combined = res_human.stderr + res_human.stdout
        if "helper.moss" not in combined:
            raise AssertionError(
                f"Expected error to reference helper.moss, got:\n{combined}")
        if "main.moss" in combined and "helper.moss" not in combined:
            raise AssertionError(
                f"Error was incorrectly attributed to root main.moss:\n{combined}")

        # 2. Structured JSON diagnostic from root check
        res_json = run([str(compiler), "check", str(src / "main.moss"), "--json"], cwd=root)
        try:
            payload = json.loads(res_json.stdout)
        except json.JSONDecodeError as err:
            raise AssertionError(f"Expected JSON output, got:\n{res_json.stdout}\n{res_json.stderr}") from err

        # Extract source_file from payload
        source_file = None
        if not payload.get("ok"):
            error_obj = payload.get("error", {})
            source_file = error_obj.get("source_file")
        else:
            diagnostics = payload.get("result", {}).get("diagnostics", [])
            if diagnostics:
                source_file = diagnostics[0].get("source_file")

        if not source_file or "helper.moss" not in source_file:
            raise AssertionError(
                f"JSON diagnostic source_file did not identify helper.moss: {source_file!r}\nPayload:\n{payload}")

    print("  [PASS] semantic error provenance in non-root module (human and JSON)")


def test_parse_error_provenance_non_root(compiler):
    with tempfile.TemporaryDirectory() as tmpdir:
        root = Path(tmpdir)
        src = root / "src"
        src.mkdir(parents=True)

        (root / "Moss.toml").write_text(
            '[package]\nname = "parse_prov_proj"\nversion = "0.1.0"\n\n'
            '[build]\nsource = "src"\n', encoding="utf-8")

        (src / "main.moss").write_text(
            'module parse_prov_proj\n'
            'import submod\n'
            'fn main():\n'
            '  submod.do_thing()\n', encoding="utf-8")

        # submod.moss has a parse error
        (src / "submod.moss").write_text(
            'module submod\n'
            'export fn do_thing(:\n'
            '  x = 1\n', encoding="utf-8")

        # 1. Human-readable diagnostic
        res_human = run([str(compiler), "check", str(src / "main.moss")], cwd=root)
        if res_human.returncode == 0:
            raise AssertionError(f"Expected parse check to fail, but succeeded:\n{res_human.stdout}")
        combined = res_human.stderr + res_human.stdout
        if "submod.moss" not in combined:
            raise AssertionError(
                f"Expected parse error to reference submod.moss, got:\n{combined}")

        # 2. Structured JSON diagnostic
        res_json = run([str(compiler), "check", str(src / "main.moss"), "--json"], cwd=root)
        try:
            payload = json.loads(res_json.stdout)
        except json.JSONDecodeError as err:
            raise AssertionError(f"Expected JSON output, got:\n{res_json.stdout}\n{res_json.stderr}") from err

        source_file = None
        if not payload.get("ok"):
            error_obj = payload.get("error", {})
            source_file = error_obj.get("source_file")
        else:
            diagnostics = payload.get("result", {}).get("diagnostics", [])
            if diagnostics:
                source_file = diagnostics[0].get("source_file")

        if not source_file or "submod.moss" not in source_file:
            raise AssertionError(
                f"JSON diagnostic source_file did not identify submod.moss: {source_file!r}\nPayload:\n{payload}")

    print("  [PASS] parse error provenance in non-root module (human and JSON)")


def main():
    compiler = REPO / "moss"
    if not compiler.exists():
        compiler = Path(os.environ.get("MOSS", "moss"))
    print("Testing SWARM-059 diagnostic source provenance...")
    test_semantic_error_provenance_non_root(compiler)
    test_parse_error_provenance_non_root(compiler)
    print("SWARM-059 tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
