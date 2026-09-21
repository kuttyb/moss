#!/usr/bin/env python3
"""Hermetic source-free module-provider ambiguity regressions."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
root = repo / "tmp" / "module-provider-ambiguity"
shutil.rmtree(root, ignore_errors=True)
root.mkdir(parents=True)


def run(args, cwd, *, env=None, expected=0):
    process = subprocess.run([str(value) for value in args], cwd=cwd, text=True,
                             capture_output=True, env=env, timeout=120)
    assert process.returncode == expected, (args, process.stdout, process.stderr)
    return process


def write_package(path, name, source):
    (path / "src").mkdir(parents=True)
    (path / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n',
        encoding="utf-8")
    (path / "src" / "main.moss").write_text(source, encoding="utf-8")


def build(path):
    return run([compiler, "build", "--json"], path)


def module_path(*packages):
    return os.pathsep.join(str(package / "build" / "debug") for package in packages)


def app(name, source):
    path = root / name
    write_package(path, name, source)
    return path


alpha = app("alpha", "module Alpha\n\nexport fn value() -> Int:\n  20\n")
beta = app("beta", "module Beta\n\nexport fn value() -> Int:\n  22\n")
utils_a = app("utils-a", "module Utils\n\nexport fn value() -> Int:\n  1\n")
utils_b = app("utils-b", "module Utils\n\nexport fn value() -> Int:\n  2\n")
math_a = app("math-a", "module Math\n\nexport fn value() -> Int:\n  40\n")
math_b = app("math-b", "module Math\n\nexport fn value() -> Int:\n  41\n")
for provider in (alpha, beta, utils_a, utils_b, math_a, math_b):
    build(provider)

# Different provider names resolve normally.
different = app("different", "module App\nimport Alpha\nimport Beta\n\nfn main():\n  first = Alpha.value()\n  second = Beta.value()\n  echo first + second\n")
different_env = dict(os.environ, MOSS_MODULE_PATH=module_path(alpha, beta))
different_result = run([compiler, "build", "--json"], different, env=different_env)
different_build = json.loads(different_result.stdout)
assert different_build["ok"] is True
assert run([different_build["result"]["artifacts"]["executable"]],
           different).stdout.strip() == "42"

# Duplicate modules stay legal until an import asks Moss to resolve them.
unused = app("unused", "module App\n\nfn main():\n  echo 7\n")
unused_env = dict(os.environ, MOSS_MODULE_PATH=module_path(utils_a, utils_b))
assert json.loads(run([compiler, "build", "--json"], unused, env=unused_env).stdout)["ok"] is True

# A local source module keeps its established precedence over external
# providers of the same declared module identity.
local = app("local", "module App\nimport Utils\n\nfn main():\n  echo Utils.value()\n")
(local / "src" / "utils.moss").write_text(
    "module Utils\n\nexport fn value() -> Int:\n  42\n", encoding="utf-8")
local_build = json.loads(run([compiler, "build", "--json"], local,
                             env=unused_env).stdout)
assert local_build["ok"] is True
assert run([local_build["result"]["artifacts"]["executable"]],
           local).stdout.strip() == "42"


def ambiguity(path, environment, module, paths):
    first = run([compiler, "build", "--json"], path, env=environment, expected=1)
    second = run([compiler, "build", "--json"], path, env=environment, expected=1)
    assert first.stdout == second.stdout
    payload = json.loads(first.stdout)
    assert payload["error"]["code"] == "MODULE_IMPORT_AMBIGUOUS"
    message = payload["error"]["message"]
    assert f"ambiguous imported module '{module}'" in message
    canonical = sorted(str(item.resolve()) for item in paths)
    assert all(item in message for item in canonical)
    assert message.index(canonical[0]) < message.index(canonical[1])


# Direct duplicate import names are rejected deterministically.
direct = app("direct", "module App\nimport Utils\n\nfn main():\n  echo Utils.value()\n")
direct_env = dict(os.environ, MOSS_MODULE_PATH=module_path(utils_b, utils_a))
ambiguity(direct, direct_env, "Utils",
          [utils_a / "build" / "debug" / "Utils.mossi",
           utils_b / "build" / "debug" / "Utils.mossi"])

# The same physical interface reached through duplicate and overlapping roots
# remains exactly one provider.
same = app("same", "module App\nimport Utils\n\nfn main():\n  echo Utils.value()\n")
same_root = utils_a / "build" / "debug"
same_env = dict(os.environ, MOSS_MODULE_PATH=os.pathsep.join(
    [str(same_root), str(same_root / ".." / "debug"), str(same_root)]))
same_build = json.loads(run([compiler, "build", "--json"], same, env=same_env).stdout)
assert same_build["ok"] is True
assert run([same_build["result"]["artifacts"]["executable"]], same).stdout.strip() == "1"

# A provider's declared imports use the same ambiguity rule during transitive
# .mossi closure loading; Geometry itself is compiled once against Math A.
geometry = app("geometry", "module Geometry\nimport Math\n\nexport fn answer() -> Int:\n  Math.value() + 2\n")
build_geometry_env = dict(os.environ, MOSS_MODULE_PATH=module_path(math_a))
assert json.loads(run([compiler, "build", "--json"], geometry,
                      env=build_geometry_env).stdout)["ok"] is True
transitive = app("transitive", "module App\nimport Geometry\n\nfn main():\n  echo Geometry.answer()\n")
transitive_env = dict(os.environ, MOSS_MODULE_PATH=module_path(geometry, math_b, math_a))
ambiguity(transitive, transitive_env, "Math",
          [math_a / "build" / "debug" / "Math.mossi",
           math_b / "build" / "debug" / "Math.mossi"])

print("External module provider ambiguity, transitive closure, and same-provider dedup checks passed.")
