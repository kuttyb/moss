#!/usr/bin/env python3
"""Reject WRITE-through FileIO views without changing ordinary WRITE parameters."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1] if len(sys.argv) > 1 else repo / "moss").resolve()
scratch = repo / "tmp/phase21-borrowed-view-write"
scratch.mkdir(parents=True, exist_ok=True)


def check(name, source):
    path = scratch / f"{name}.moss"
    path.write_text(source)
    result = subprocess.run(
        [str(compiler), "check", str(path), "--json"], cwd=repo,
        capture_output=True, text=True, timeout=60)
    return path, result, json.loads(result.stdout)


negative = {
    "range": ("Range", '''fn retarget_range(r: Range):
  r = r.slice(1, r.length())

fn main():
  pass
'''),
    "batch": ("RangeBatch", '''fn retarget_batch(b: RangeBatch, file: FileIO):
  b = file.read([(0, 2), (2, 2)])

fn main():
  pass
'''),
}
for name, (view_type, source) in negative.items():
    _, result, data = check(name, source)
    assert result.returncode != 0 and not data["ok"], (name, data)
    error = data["error"]
    assert error["code"] == "FILEIO_VIEW_PARAMETER_WRITE", (name, error)
    assert view_type in error["message"] and "WRITE" in error["message"], error

positive_path, positive, data = check("int", '''fn set_value(value: Int, replacement: Int):
  value = replacement

fn main():
  var value = 1
  set_value(value, 2)
  echo value
''')
assert positive.returncode == 0 and data["ok"], data
effects = subprocess.run(
    [str(compiler), "effects", "set_value", "--source", str(positive_path),
     "--json"], cwd=repo, capture_output=True, text=True, timeout=60)
assert effects.returncode == 0, (effects.stdout, effects.stderr)
ownership = json.loads(effects.stdout)["result"]["ownership"]
assert ownership[0]["effect"] == "WRITE", ownership


# A generic WRITE helper may arrive only as .mossi semantic IR. Build its
# provider, remove the source, then prove that an Int specialization remains
# usable while both borrowed FileIO view specializations fail closed.
source_free = scratch / "source_free"
if source_free.exists():
    shutil.rmtree(source_free)
provider = source_free / "provider"
consumer = source_free / "consumer"
artifacts = source_free / "artifacts"
for package, name in ((provider, "viewprovider"), (consumer, "viewconsumer")):
    (package / "src").mkdir(parents=True)
    (package / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
(provider / "src/provider.moss").write_text('''module Provider

export fn retarget(target):
  target = target
''')
environment = os.environ.copy()
environment["MOSS"] = str(compiler)
built = subprocess.run(
    [str(repo / "margo"), "build", "--json"], cwd=provider,
    env=environment, capture_output=True, text=True, timeout=120)
assert built.returncode == 0, (built.stdout, built.stderr)
artifacts.mkdir()
for name in ("Provider.mossi", "libProvider.rlib"):
    shutil.copy2(provider / "build/debug" / name, artifacts / name)
(provider / "src").rename(provider / "src.hidden")
assert {item.name for item in artifacts.iterdir()} == {
    "Provider.mossi", "libProvider.rlib"}
environment["MOSS_MODULE_PATH"] = str(artifacts)
app = consumer / "src/main.moss"
app.write_text('''module Consumer
import Provider

fn main():
  var value = 1
  Provider.retarget(value)
  echo value
''')
built = subprocess.run(
    [str(compiler), "build", "--json"], cwd=consumer,
    env=environment, capture_output=True, text=True, timeout=120)
assert built.returncode == 0, (built.stdout, built.stderr)
binary = json.loads(built.stdout)["result"]["artifacts"]["executable"]
executed = subprocess.run(
    [binary], cwd=consumer, capture_output=True, text=True, timeout=60)
assert executed.returncode == 0 and executed.stdout == "1\n", executed

file_error = '''enum FileError:
  NotFound
  PermissionDenied
  NotRegularFile
  InUse
  Full
  IO
'''
for view_name, read in (("Range", "file.read(0, 2)"),
                        ("RangeBatch", "file.read([(0, 2), (2, 2)])")):
    app.write_text('''module Consumer
import Provider

''' + file_error + '''
fn exercise():
  file = FileIO.open("input", ro)
  value = ''' + read + '''
  Provider.retarget(value)
  file.close()

fn main():
  try:
    exercise()
  recover:
    pass
''')
    checked = subprocess.run(
        [str(compiler), "check", str(app), "--json"], cwd=consumer,
        env=environment, capture_output=True, text=True, timeout=60)
    result = json.loads(checked.stdout)
    assert checked.returncode != 0 and not result["ok"], (view_name, result)
    assert result["error"]["code"] in {
        "FILEIO_PINNED_OWNERSHIP", "FILEIO_VIEW_PARAMETER_WRITE"
    }, (view_name, result)

print("Phase 21 borrowed-view WRITE prohibition, source-free specialization, and Int WRITE control passed")
