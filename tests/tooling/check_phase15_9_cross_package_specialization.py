#!/usr/bin/env python3
"""Phase 15.9 static-specialization ownership across native artifacts."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


REPO = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
MARGO = REPO / "margo"


def env_for(**extra):
    env = dict(os.environ)
    env["MOSS"] = str(COMPILER)
    env.update(extra)
    return env


def run(command, cwd, *, env=None, expected=0):
    process = subprocess.run([str(part) for part in command], cwd=cwd,
                             env=env or env_for(), text=True,
                             capture_output=True, timeout=180)
    if process.returncode != expected:
        raise AssertionError(
            f"{command} failed with {process.returncode} (expected {expected})\n"
            f"stdout:\n{process.stdout}\nstderr:\n{process.stderr}")
    return process


def package(root, name, sources, dependencies=""):
    (root / "src").mkdir(parents=True)
    (root / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
        f'[build]\nsource = "src"\n{dependencies}', encoding="utf-8")
    for filename, text in sources.items():
        (root / "src" / filename).write_text(text, encoding="utf-8")


def build_json(root, *, env=None):
    return json.loads(run([MARGO, "build", "--json"], root, env=env).stdout)


def require_output(command, root, expected, *, env=None):
    result = run(command, root, env=env)
    if result.stdout != expected:
        raise AssertionError(f"unexpected output for {command}: {result.stdout!r}, expected {expected!r}")
    return result


def test_same_and_cross_module(root):
    same = root / "same"
    package(same, "same", {
        "main.moss": """module Same

fn bump(value):
  return value

fn wrapper() -> Int:
  return bump(5)

fn main():
  echo wrapper()
"""})
    build_json(same)
    require_output([MARGO, "run"], same, "5\n")

    cross = root / "cross"
    package(cross, "cross", {
        "lib.moss": """module Lib

export fn bump(value):
  return value
""",
        "main.moss": """module App
import Lib

fn main():
  echo Lib.bump(9)
"""})
    build_json(cross)
    require_output([MARGO, "run"], cross, "9\n")
    app_rust = (cross / "build" / "debug" / "App.rs").read_text(encoding="utf-8")
    if app_rust.count("fn __moss_specialize_Lib__bump_0") != 1:
        raise AssertionError("cross-module caller did not emit exactly one local specialization")
    print("  [PASS] same-module and source-backed cross-module specializations")


def provider_sources():
    return {"provider.moss": """module Provider

export fn bump(value):
  return value

export fn append(values: Vector[Int]) -> Int:
  return bump(42)

export type Task:
  cost: Int

  fn score() -> Int:
    return cost * 2

export trait Scorable:
  fn score() -> Int

# The untyped slot is existing Moss generic syntax; the trait call remains
# statically resolved for each checked concrete item type.
export fn score_item(item: Scorable, unused):
  return item.score()

export fn score_seed() -> Int:
  return score_item(Task(cost: 21), 0)
"""}


def app_sources(module="App"):
    return {"main.moss": f"""module {module}
import Provider

type Task:
  cost: Int

  fn score() -> Int:
    return cost * 2

fn repeated() -> Int:
  first = Provider.bump(7)
  second = Provider.bump(7)
  return first + second

fn main():
  echo Provider.append([1])
  echo repeated()
  echo Provider.score_item(Task(cost: 21), 0)
"""}


def test_provider_and_source_free(root):
    provider = root / "provider"
    package(provider, "provider", provider_sources())
    provider_result = build_json(provider)
    provider_artifacts = provider_result["artifacts"]["artifacts"]
    if provider_artifacts["specialization_rlib"] is not None:
        raise AssertionError("provider kept a graph-wide moss-specializations artifact")
    provider_rust = (provider / "build" / "debug" / "Provider.rs").read_text(encoding="utf-8")
    required = "fn __moss_specialize_Provider__bump_0"
    if required not in provider_rust or "return __moss_specialize_Provider__bump_0(42_i64);" not in provider_rust:
        raise AssertionError("provider concrete wrapper did not retain its required specialization")
    if "moss_specializations" in provider_rust:
        raise AssertionError("provider still depends on a final-application specialization crate")

    app = root / "app"
    package(app, "app", app_sources(),
            '\n[dependencies]\nprovider = { path = "../provider" }\n')
    (app / "tests").mkdir()
    (app / "tests" / "provider.moss").write_text(
        "import Provider\n\ntest \"provider wrapper\":\n  assertEqual(Provider.append([1]), 42)\n",
        encoding="utf-8")
    build_json(app)
    require_output([MARGO, "run"], app, "42\n14\n42\n")
    run([MARGO, "test"], app)
    app_rust = (app / "build" / "debug" / "App.rs").read_text(encoding="utf-8")
    if app_rust.count("fn __moss_specialize_Provider__bump_0") != 1:
        raise AssertionError("repeated consumer specialization emitted duplicate definitions")
    if "dyn " in app_rust or "vtable" in app_rust:
        raise AssertionError("trait specialization introduced runtime dispatch")

    # Preserve only normal provider artifacts, then make its Moss source
    # unavailable.  The consumer gets semantic generic IR from .mossi and
    # concrete provider implementation from .rlib; no provider source is
    # folded into this compilation.
    artifact_root = root / "provider-artifacts"
    shutil.copytree(provider / "build" / "debug", artifact_root)
    (provider / "src").rename(provider / "src.hidden")
    source_free = root / "source-free"
    package(source_free, "sourcefree", app_sources("Consumer"))
    source_free_env = env_for(MOSS_MODULE_PATH=str(artifact_root))
    build = run([COMPILER, "build", "--json"], source_free, env=source_free_env)
    artifact = json.loads(build.stdout)["result"]["artifacts"]
    require_output([artifact["executable"]], source_free, "42\n14\n42\n",
                   env=source_free_env)
    consumer_rust = Path(artifact["generated_rust"]).read_text(encoding="utf-8")
    if str(provider / "src") in consumer_rust or "fn Provider__append" in consumer_rust:
        raise AssertionError("source-free consumer folded provider Moss implementation")
    if "extern crate moss_Provider;" not in consumer_rust:
        raise AssertionError("source-free consumer did not retain provider rlib linkage")
    if "fn __moss_specialize_Provider__score_item_0" not in consumer_rust:
        raise AssertionError("source-free structural specialization was not projected from .mossi")
    print("  [PASS] provider-owned wrapper, static trait, repeated use, and source-free .mossi + .rlib")


with tempfile.TemporaryDirectory(prefix="moss-phase159-", dir=REPO / "tmp") as temp:
    root = Path(temp)
    test_same_and_cross_module(root)
    test_provider_and_source_free(root)

print("Phase 15.9 cross-package static specialization convergence checks passed.")
