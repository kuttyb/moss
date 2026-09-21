#!/usr/bin/env python3
"""Hermetic package-DAG, Git-cache, lockfile, and compatibility checks."""
import json
from pathlib import Path
import shutil
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]
margo = repo / "margo"
moss = Path(sys.argv[1]).resolve()
root = repo / "tmp" / "margo-check"
home = root / "home"
shutil.rmtree(root, ignore_errors=True)
root.mkdir(parents=True)


def run(args, cwd=None, expected=0, env=None):
    values = [str(value) for value in args]
    process = subprocess.run(values, cwd=cwd, text=True, capture_output=True,
                             env=env, timeout=120)
    assert process.returncode == expected, (values, process.stdout, process.stderr)
    return process


def write_package(path, name, source, dependencies=""):
    path.mkdir(parents=True, exist_ok=True)
    (path / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
        f'[build]\nsource = "src"\n{dependencies}', encoding="utf-8")
    (path / "src").mkdir(exist_ok=True)
    (path / "src" / "main.moss").write_text(source, encoding="utf-8")


# A tagged local Git package is intentionally used instead of public network
# access. It supplies a source-free interface to a path package.
math = root / "math"
write_package(math, "Math", """module Math

export fn value() -> Int:
  40
""")
run(["git", "init", "-q"], math)
run(["git", "config", "user.email", "margo@example.invalid"], math)
run(["git", "config", "user.name", "Margo test"], math)
run(["git", "add", "."], math)
run(["git", "commit", "-qm", "math"], math)
run(["git", "tag", "v1"], math)
commit = run(["git", "rev-parse", "HEAD"], math).stdout.strip()

geometry = root / "geometry"
write_package(geometry, "Geometry", """module Geometry
import Math

export fn answer() -> Int:
  Math.value() + 2
""", f'''\n[dependencies]\nMath = {{ git = "{math.as_uri()}", tag = "v1" }}\n''')

app = root / "app"
write_package(app, "App", """module App
import Geometry

fn main():
  echo Geometry.answer()
""", '''\n[dependencies]\nGeometry = { path = "../geometry" }\n''')
(app / "tests").mkdir()
(app / "tests" / "answer.moss").write_text('import Geometry\n\ntest "dependency answer":\n  assertEqual(Geometry.answer(), 42)\n', encoding="utf-8")
(app / "benches").mkdir()
(app / "benches" / "answer.moss").write_text('import Geometry\n\nbench "dependency answer":\n  value = Geometry.answer()\n  value + 0\n', encoding="utf-8")
env = dict(**__import__("os").environ, MARGO_HOME=str(home), MOSS=str(moss))

first = run([margo, "build", "--json"], app, env=env)
result = json.loads(first.stdout)
assert result["ok"] is True
assert result["build_order"] == ["Math", "Geometry", "App"]
assert result["packages"][0]["source"]["commit"] == commit
# These are compiled provider contracts, not a folded Moss source unit.  The
# concrete Rust consumers name their imported module crate and do not contain
# the provider implementation.
geometry_rust = (geometry / "build" / "debug" / "Geometry.rs").read_text(encoding="utf-8")
app_rust = (app / "build" / "debug" / "App.rs").read_text(encoding="utf-8")
assert "extern crate moss_Math;" in geometry_rust
assert "fn Math__value" not in geometry_rust
assert "extern crate moss_Geometry;" in app_rust
assert "fn Geometry__answer" not in app_rust
lock = (app / "Moss.lock").read_text(encoding="utf-8")
assert commit in lock and "git =" in lock
assert run([margo, "run"], app, env=env).stdout.strip() == "42"

# Margo, not ambient shell state, supplies the source-free module roots to
# project test/bench commands. A direct compiler test without that environment
# proves the following Margo invocation is exercising the package handoff.
without_dependencies = dict(env)
without_dependencies.pop("MOSS_MODULE_PATH", None)
missing = run([moss, "test"], app, expected=1, env=without_dependencies)
assert "imported module 'Geometry' was not found" in missing.stderr
run([margo, "test"], app, env=env)
run([margo, "bench", "dependency answer"], app, env=env)

# Once the exact revision is cached, a repeat must not need a changing tag or
# branch resolution. Removing the original remote proves cache/lock reuse.
shutil.rmtree(math)
second = run([margo, "build", "--json"], app, env=env)
assert json.loads(second.stdout)["build_order"] == ["Math", "Geometry", "App"]
assert (app / "Moss.lock").read_text(encoding="utf-8") == lock
run([margo, "clean"], app, env=env)
assert not (app / "build").exists()

# The existing compiler command remains a direct compatibility path for a
# package manifest; Margo owns dependency acquisition, Moss owns compilation.
shutil.rmtree(geometry / "build")
cached_math = next((home / "git" / "checkouts").glob("*/*/build/debug"))
run([moss, "build"], geometry, env=dict(env, MOSS_MODULE_PATH=str(cached_math)))

# Cycle diagnostics are package-layer diagnostics, before compilation.
left, right = root / "left", root / "right"
write_package(left, "Left", "fn main():\n  echo 1\n", '\n[dependencies]\nRight = { path = "../right" }\n')
write_package(right, "Right", "fn main():\n  echo 1\n", '\n[dependencies]\nLeft = { path = "../left" }\n')
cycle = run([margo, "build"], left, expected=1, env=env)
assert "package dependency cycle: Left -> Right -> Left" in cycle.stderr

print("Margo path/Git DAG, lockfile, cache reuse, run, and cycle checks passed.")
