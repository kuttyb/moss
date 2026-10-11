#!/usr/bin/env python3
"""Root failure trailers remain total inside ordinary main expressions."""

import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
FIXTURES = ROOT / "tests/tooling/fixtures"
OUT = ROOT / "tmp/phase21-root-expression-ingress"
OUT.mkdir(parents=True, exist_ok=True)


def run(*args, cwd=ROOT, expected=0):
    result = subprocess.run(
        [str(arg) for arg in args], cwd=cwd, text=True,
        capture_output=True, timeout=180,
    )
    assert result.returncode == expected, (
        args, result.returncode, result.stdout, result.stderr)
    return result


positive = FIXTURES / "phase21_root_nested_ingress.moss"
negative = FIXTURES / "phase21_root_nested_ingress_unhandled.moss"
assert json.loads(run(COMPILER, "check", positive, "--json").stdout)["ok"]
effects = json.loads(run(
    COMPILER, "effects", "Worker.Fetch", "--source", positive, "--json",
).stdout)["result"]["observable_effects"]
assert effects["raise_set"] == ["enum:E:Bad"] and not effects["unresolved"]

expected_output = "8\n7\n7\nready\n7\n"
for profile, moss_optimization, rust_optimization in (
    ("o0", "-O0", "0"),
    ("o3", "-Oshared-memory", "3"),
):
    rust = OUT / f"nested-{profile}.rs"
    executable = OUT / f"nested-{profile}"
    run(COMPILER, positive, moss_optimization, "-o", rust)
    run("rustc", "--edition=2021", "-Dwarnings", "-C",
        f"opt-level={rust_optimization}", rust, "-o", executable)
    result = run(executable)
    assert result.stdout == expected_output and result.stderr == "", result

debug = run(COMPILER, "run", "--interp", positive)
assert debug.stdout == expected_output and debug.stderr == "", debug

# A lexical recover in main cannot turn a handler without on_fail into a
# recoverable Root call, even when the message is buried in a call argument.
rejected = run(COMPILER, "check", negative, "--json", expected=1)
error = json.loads(rejected.stdout)["error"]
assert error["code"] == "ROOT_UNHANDLED_RAISE", error
assert "enum:E:Bad" in error["message"], error
run(COMPILER, negative, "-o", OUT / "unhandled.rs", expected=1)
run(COMPILER, "run", "--interp", negative, expected=1)

# Rebuild the consumer against only an ABI-v9 provider interface and rlib.
# Changing the consumer from a direct to a nested Root message forces a fresh
# check after the provider's Moss source has been removed.
project = OUT / "source-free"
source_dir = project / "src"
source_dir.mkdir(parents=True, exist_ok=True)
(project / "moss.toml").write_text(
    '[project]\nname="phase21_root_expression_sourcefree"\nversion="0.1.0"\n')
provider = source_dir / "service.moss"
provider.write_text('''module service
export enum E:
  Bad
export domain Worker:
  fn Fetch() -> Int:
    raise E.Bad
  on_fail E.Bad:
    reply 7
''')
consumer = source_dir / "main.moss"
consumer.write_text('''module app
import service
fn main():
  worker = service.Worker()
  echo message worker.Fetch()
''')
first = json.loads(run(COMPILER, "build", "--json", cwd=project).stdout)
assert run(first["result"]["artifacts"]["executable"], cwd=project).stdout == "7\n"
interface = project / "build/debug/service.mossi"
provider_rlib = project / "build/debug/libmoss_service.rlib"
assert interface.exists() and provider_rlib.exists()
provider.rename(provider.with_suffix(".removed"))
consumer.write_text('''module app
import service
fn identity(value: Int) -> Int:
  return value
fn main():
  worker = service.Worker()
  echo 1 + message worker.Fetch()
  echo identity(message worker.Fetch())
''')
rebuilt = json.loads(run(COMPILER, "build", "--json", cwd=project).stdout)
result = run(rebuilt["result"]["artifacts"]["executable"], cwd=project)
assert result.stdout == "8\n7\n" and result.stderr == "", result
assert not provider.exists() and interface.exists() and provider_rlib.exists()

print("Phase 21 nested Root ingress and source-free trailer checks passed")
