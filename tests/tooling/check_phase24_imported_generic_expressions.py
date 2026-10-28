"""Imported generic calls inside value wrappers must emit consumer specializations."""

import os
from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve()
MARGO = ROOT / "margo"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)

with tempfile.TemporaryDirectory(prefix="phase24-generic-expressions-", dir=SCRATCH) as name:
    work = Path(name)
    provider = work / "provider"
    consumer = work / "consumer"
    (provider / "src").mkdir(parents=True)
    (consumer / "src").mkdir(parents=True)
    (provider / "Moss.toml").write_text(
        '[package]\nname = "moss.expression_probe"\nversion = "0.1.0"\n'
        '[build]\nsource = "src"\n', encoding="utf-8")
    (provider / "src" / "probe.moss").write_text(
        'module probe\n'
        'export type Box:\n'
        '  value: Int\n'
        'export fn make(value) -> Box:\n'
        '  return Box(value: value)\n'
        'export fn contains(items, target) -> Bool:\n'
        '  var i = 0\n'
        '  while i < (items |> count):\n'
        '    if items[i] == target:\n'
        '      return true\n'
        '    i = i + 1\n'
        '  return false\n'
        'export fn index_of(items, target) -> Int:\n'
        '  var i = 0\n'
        '  while i < (items |> count):\n'
        '    if items[i] == target:\n'
        '      return i\n'
        '    i = i + 1\n'
        '  return 0 - 1\n', encoding="utf-8")
    (consumer / "Moss.toml").write_text(
        '[package]\nname = "phase24_generic_expression_probe"\nversion = "0.1.0"\n'
        '[build]\nsource = "src"\n'
        '[dependencies]\n"moss.expression_probe" = { path = "../provider" }\n',
        encoding="utf-8")
    (consumer / "src" / "main.moss").write_text(
        'module app\nimport probe\n'
        'fn main():\n'
        '  values = ["red", "blue"]\n'
        '  assert(not probe.contains(values, "green"))\n'
        '  flags = [not probe.contains(values, "green")]\n'
        '  assert(flags[0])\n'
        '  echo 0 - probe.index_of(values, "blue")\n'
        '  echo probe.make(7).value\n'
        '  echo [probe.make(3).value][0]\n'
        '  echo [probe.index_of(values, "blue")][0]\n'
        '  echo [1, 2][0]\n', encoding="utf-8")

    env = os.environ.copy()
    env.update(MOSS=str(MOSS), MARGO_HOME=str(work / "margo-home"), TMPDIR=str(SCRATCH))
    for args in (("build", "--json"), ("run", "--json"),
                 ("run", "--release", "--json"), ("debug",)):
        result = subprocess.run([str(MARGO), *args], cwd=consumer, env=env,
                                capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(f"margo {' '.join(args)} failed:\n{result.stdout}\n{result.stderr}")
        if args[0] in ("run", "debug") and result.stdout != "-1\n7\n3\n1\n1\n":
            raise AssertionError(f"margo {' '.join(args)}: {result.stdout!r}")

    # Rebuild the same consumer against the selected compiled provider only.
    source_free = work / "source_free"
    (source_free / "src").mkdir(parents=True)
    shutil.copy2(consumer / "src/main.moss", source_free / "src/main.moss")
    (source_free / "Moss.toml").write_text(
        '[project]\nname="phase24_generic_expression_source_free"\nversion="0.1.0"\n',
        encoding="utf-8")
    provider_source = provider / "src/probe.moss"
    # Keep package source available for R2 interface freshness validation.
    compiled_provider = provider / "build/debug"
    assert (compiled_provider / "probe.mossi").exists()
    assert (compiled_provider / "libprobe.rlib").exists()
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
            assert output.returncode == 0 and output.stdout == "-1\n7\n3\n1\n1\n", output

print("Phase 24 imported generic expression specializations passed (source-backed and source-free)")
