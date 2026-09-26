#!/usr/bin/env python3
"""Project diagnostics retain the physical source of each compiler error."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()


def run(*args, expected=0):
    result = subprocess.run(
        [str(COMPILER), *map(str, args)], text=True, capture_output=True
    )
    if result.returncode != expected:
        raise AssertionError(
            f"{args} returned {result.returncode}, expected {expected}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


with tempfile.TemporaryDirectory(prefix="moss-sw-059-", dir=ROOT / "tmp") as name:
    project = Path(name)
    source_dir = project / "src"
    source_dir.mkdir()
    (project / "Moss.toml").write_text(
        '[project]\nname = "provenance"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n',
        encoding="utf-8",
    )
    root_source = source_dir / "a_main.moss"
    other_source = source_dir / "z_errors.moss"
    root_source.write_text(
        "module app\n"
        "import lib\n"
        "fn main():\n"
        "  echo lib.ok()\n"
        "  echo 1\n",
        encoding="utf-8",
    )

    cases = {
        "ordinary type error": (
            "module lib\n"
            "export fn ok() -> Int:\n"
            "  return 1\n"
            "export fn bad() -> Int:\n"
            '  return "bad"\n',
            "annotated 'int'",
            5,
        ),
        "bad function call": (
            "module lib\n"
            "export fn ok() -> Int:\n"
            "  return 1\n"
            "export fn bad(x: Int) -> Int:\n"
            "  missing(x)\n"
            "  return x\n",
            "unknown local function 'missing'",
            5,
        ),
        "bad method call": (
            "module lib\n"
            "export type Box:\n"
            "  value: Int\n"
            "  fn get() -> Int:\n"
            "    return value\n"
            "export fn ok() -> Int:\n"
            "  return 1\n"
            "export fn bad() -> Int:\n"
            "  box = Box(value: 1)\n"
            "  box.missing()\n"
            "  return 1\n",
            "no matching method",
            10,
        ),
    }

    for name, (other_text, expected_message, expected_line) in cases.items():
        other_source.write_text(other_text, encoding="utf-8")
        root_source.write_text(
            "module app\nimport lib\nfn main():\n  echo lib.ok()\n  echo 1\n",
            encoding="utf-8",
        )

        structured = json.loads(run("check", root_source, "--json", expected=1).stdout)
        error = structured["error"]
        assert not structured["ok"]
        assert Path(error["source_file"]).resolve() == other_source.resolve(), (
            name, error
        )
        assert error["line"] == expected_line, (name, error)
        assert expected_message in error["message"], (name, error)

        human = run("check", root_source, expected=1)
        assert str(other_source.resolve()) in human.stderr, (name, human.stderr)
        assert expected_message in human.stderr, (name, human.stderr)

print("SWARM-059 project diagnostic provenance regression passed")
