#!/usr/bin/env python3
"""Fail closed before native E0308 for a borrowed Range crossing module crates."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


root = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
margo = compiler.parent / "margo"
fixture = root / "tests/tooling/fixtures/phase20_typed_range_cross_crate"
scratch = root / "tmp/phase20_cross_crate_range_abi"
if scratch.exists():
    shutil.rmtree(scratch)
scratch.mkdir(parents=True)


def run(argv, cwd, *, env=None, ok=True):
    result = subprocess.run([str(item) for item in argv], cwd=cwd, env=env,
                            capture_output=True, text=True, timeout=180)
    assert (result.returncode == 0) == ok, (
        argv, result.returncode, result.stdout, result.stderr)
    return result


def expect_range_diagnostic(project, *, env=None):
    checked = run([compiler, "check", project / "src/main.moss", "--json"],
                  project, env=env, ok=False)
    result = json.loads(checked.stdout)
    assert result["error"]["code"] == "FILEIO_CROSS_CRATE_ABI", result
    assert result["error"]["line"] == 17, result
    built = run([margo, "build"], project, env=env, ok=False)
    assert "FILEIO_CROSS_CRATE_ABI" in built.stdout + built.stderr, built
    assert "E0308" not in built.stdout + built.stderr, built


provider = scratch / "provider"
consumer = scratch / "consumer"
shutil.copytree(fixture / "provider", provider)
shutil.copytree(fixture / "consumer", consumer)
(consumer / "tmp").mkdir()
run([margo, "build"], provider)

external = os.environ.copy()
external["MOSS_MODULE_PATH"] = str(provider / "build/debug")
expect_range_diagnostic(consumer, env=external)

# A source-backed module is still a separate generated Rust crate.
source_backed = scratch / "source-backed"
shutil.copytree(fixture / "consumer", source_backed)
shutil.copyfile(fixture / "provider/src/mapper.moss",
                source_backed / "src/mapper.moss")
(source_backed / "tmp").mkdir()
expect_range_diagnostic(source_backed)

# A same-module borrowed Range helper stays in one native crate and remains valid.
same_module = scratch / "same-module"
shutil.copytree(fixture / "consumer", same_module)
(same_module / "tmp").mkdir()
main = (same_module / "src/main.moss").read_text()
main = main.replace("module app\nimport mapper\n", "module app\n")
main = main.replace("mapper.first(chunk)", "first(chunk)")
main = main.replace("enum FileError:\n", "fn first(chunk: Range) -> Int:\n  return chunk[0]\n\nenum FileError:\n")
(same_module / "src/main.moss").write_text(main)
result = run([margo, "run"], same_module)
assert result.stdout == "65\n", result

print("Phase 20 cross-crate Range ABI fail-closed and same-crate control passed")
