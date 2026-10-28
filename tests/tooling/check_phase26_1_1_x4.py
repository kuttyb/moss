#!/usr/bin/env python3
"""X4 source-free dependency reachability edge cases."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[2]


def run(command, cwd: Path, env: dict[str, str], expected: int = 0):
    result = subprocess.run([str(item) for item in command], cwd=cwd, env=env,
                            text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    if result.returncode != expected:
        raise AssertionError(
            f"{command} returned {result.returncode}, expected {expected}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}")
    return result


def package(root: Path, name: str, sources: dict[str, str], dependencies=""):
    (root / "src").mkdir(parents=True, exist_ok=True)
    (root / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
        f'[build]\nsource = "src"\n{dependencies}', encoding="utf-8")
    for filename, source in sources.items():
        (root / "src" / filename).write_text(source, encoding="utf-8")


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit(f"Usage: {sys.argv[0]} <path-to-moss-compiler>")
    moss = Path(sys.argv[1]).resolve()
    margo = moss.parent / "margo"
    env = dict(os.environ, MOSS=str(moss), RUSTC="/definitely/not/rustc")
    with tempfile.TemporaryDirectory(prefix="phase26-x4-", dir=REPO / "tmp") as temp:
        root = Path(temp)
        callbacks = root / "callbacks"
        domains = root / "domains"
        app = root / "app"
        package(callbacks, "callbacks", {"callbacks.moss": '''module callbacks
export fn apply(callback, value: Int) -> Int:
  return callback(value)
export fn increment(value: Int) -> Int:
  return value + 1
'''})
        package(domains, "domains", {"domains.moss": '''module domains
export domain Counter:
  value: Int
  fn Add(amount: Int) -> Int:
    value = value + amount
    reply value
'''})
        package(app, "app", {"main.moss": '''module app
import callbacks
import domains
fn local_increment(value: Int) -> Int:
  return value + 1
fn main():
  counter = domains.Counter(value: 5)
  echo callbacks.apply(local_increment, 4)
  echo message counter.Add(2)
'''
        }, '\n[dependencies]\ncallbacks = { path = "../callbacks" }\n'
           'domains = { path = "../domains" }\n')
        result = run([margo, "debug"], app, env)
        if result.stdout != "5\n7\n":
            raise AssertionError(f"cross-package callback/domain output: {result.stdout!r}")

        bad_generic = root / "bad_generic"
        bad_app = root / "bad_app"
        package(bad_generic, "bad_generic", {"generic.moss": '''module bad_generic
export fn add_one(value) -> Int:
  return value + 1
'''})
        package(bad_app, "bad_app", {"main.moss": '''module bad_app
import bad_generic
fn main():
  echo bad_generic.add_one("not an integer")
'''
        }, '\n[dependencies]\nbad_generic = { path = "../bad_generic" }\n')
        incompatible = run([margo, "debug"], bad_app, env, expected=1)
        combined = incompatible.stdout + incompatible.stderr
        if "bad_generic" not in combined and "add_one" not in combined:
            raise AssertionError(
                f"incompatible dependency generic lacked its callable diagnostic: {combined}")

        # Two imported callables share a leaf name. The scope pass must retain
        # both candidates until callback specialization disambiguates them.
        first = root / "first"
        second = root / "second"
        ambiguous_app = root / "ambiguous_app"
        package(first, "first", {"first.moss": '''module first
export fn transform(value) -> Int:
  return value + 1
'''})
        package(second, "second", {"second.moss": '''module second
export fn transform(value) -> Int:
  return value + 2
'''})
        package(ambiguous_app, "ambiguous_app", {"main.moss": '''module ambiguous_app
import first
import second
fn main():
  values = [1] |> map(transform)
  echo values[0]
'''
        }, '\n[dependencies]\nfirst = { path = "../first" }\n'
           'second = { path = "../second" }\n')
        ambiguous = run([margo, "debug"], ambiguous_app, env, expected=1)
        if "unresolved functional callable 'transform'" not in (
                ambiguous.stdout + ambiguous.stderr):
            raise AssertionError(
                f"ambiguous callback did not report the expected conservative diagnostic: "
                f"{ambiguous.stdout!r} {ambiguous.stderr!r}")

    print("X4 cross-package callback, domain handler, incompatible generic, and ambiguous callback gates passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
