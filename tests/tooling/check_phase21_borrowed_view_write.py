#!/usr/bin/env python3
"""Reject WRITE-through FileIO views without changing ordinary WRITE parameters."""

import json
from pathlib import Path
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

print("Phase 21 borrowed-view WRITE prohibition and Int WRITE control passed")
