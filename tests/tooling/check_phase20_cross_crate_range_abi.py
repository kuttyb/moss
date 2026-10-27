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


def expect_range_diagnostic(project, *, env=None, line=17):
    checked = run([compiler, "check", project / "src/main.moss", "--json"],
                  project, env=env, ok=False)
    result = json.loads(checked.stdout)
    assert result["error"]["code"] == "FILEIO_CROSS_CRATE_ABI", result
    assert result["error"]["line"] == line, result
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

# The caller's module comes from its declaration, whatever kind of callable
# it is: an app helper function and an app record method are both rejected,
# while a method calling a same-module helper stays valid.
def source_backed_variant(name, declarations, call):
    project = scratch / name
    shutil.copytree(fixture / "consumer", project)
    shutil.copyfile(fixture / "provider/src/mapper.moss", project / "src/mapper.moss")
    (project / "tmp").mkdir()
    main = (project / "src/main.moss").read_text()
    main = main.replace("fn main():", declarations + "\nfn main():", 1)
    main = main.replace("echo mapper.first(chunk)", call)
    (project / "src/main.moss").write_text(main)
    return project


expect_range_diagnostic(source_backed_variant(
    "helper-caller",
    "fn via_helper(chunk: Range) -> Int:\n  return mapper.first(chunk)\n",
    "echo via_helper(chunk)"), line=13)
expect_range_diagnostic(source_backed_variant(
    "method-caller",
    "type Reader:\n  tag: Int\n\n  fn read_first(chunk: Range) -> Int:\n"
    "    return mapper.first(chunk)\n",
    "echo Reader(tag: 1).read_first(chunk)"), line=16)
method_local = source_backed_variant(
    "method-local",
    "fn local_first(chunk: Range) -> Int:\n  return chunk[0]\n\n"
    "type Reader:\n  tag: Int\n\n  fn read_first(chunk: Range) -> Int:\n"
    "    return local_first(chunk)\n",
    "echo Reader(tag: 1).read_first(chunk)")
assert run([margo, "run"], method_local).stdout == "65\n"

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

# A generic provider body specializes in the consumer's crate, where the
# actual borrowed Range has the same nominal Rust type. This remains valid
# both with provider source available and with only semantic .mossi IR.
generic_backed = scratch / "generic-backed"
shutil.copytree(fixture / "consumer", generic_backed)
(generic_backed / "tmp").mkdir()
generic_source = "module mapper\nexport fn first(chunk):\n  return chunk.length()\n"
(generic_backed / "src/mapper.moss").write_text(generic_source)
backed_result = run([margo, "run"], generic_backed)
assert backed_result.stdout == "1\n", backed_result

generic_provider = scratch / "generic-provider"
shutil.copytree(fixture / "provider", generic_provider)
(generic_provider / "src/mapper.moss").write_text(generic_source)
run([margo, "build"], generic_provider)
generic_consumer = scratch / "generic-consumer"
shutil.copytree(fixture / "consumer", generic_consumer)
(generic_consumer / "tmp").mkdir()
generic_env = os.environ.copy()
generic_env["MOSS_MODULE_PATH"] = str(generic_provider / "build/debug")
free_result = run([margo, "run"], generic_consumer, env=generic_env)
assert free_result.stdout == "1\n", free_result

print("Phase 20 cross-crate Range ABI fail-closed and source-backed imported-interface generic/same-crate controls passed")
