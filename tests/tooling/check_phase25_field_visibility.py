#!/usr/bin/env python3
"""Moss-level record field visibility (Phase 25 G2-D) in real projects.

An unexported field of an unexported record is private to its defining
module: other modules are rejected before native emission, while
every file of the defining module may use it. An unmodular project is one
module, so all of its files may use its records' fields.
"""

import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
MARGO = ROOT / "margo"


def run(*args, cwd=ROOT):
    return subprocess.run([str(arg) for arg in args], cwd=cwd, text=True,
                          capture_output=True, timeout=300)


def project(root, files):
    (root / "src").mkdir(parents=True)
    (root / "Moss.toml").write_text(
        f'[project]\nname = "{root.name}"\nversion = "1.0.0"\n\n[build]\nsource = "src"\n')
    for name, text in files.items():
        (root / "src" / name).write_text(text)
    return root


def runs(root, expected):
    built = run(MARGO, "build", "--json", cwd=root)
    data = json.loads(built.stdout)
    assert built.returncode == 0 and data.get("ok"), (root, built.stdout[-1200:])
    executed = run(data["artifacts"]["artifacts"]["executable"], cwd=root)
    assert executed.returncode == 0 and executed.stdout == expected, (root, executed.stdout)
    interpreted = run(MOSS, "run", "--interp", root / "src/main.moss", cwd=root)
    assert interpreted.returncode == 0 and interpreted.stdout == expected, (root, interpreted.stdout, interpreted.stderr)


# Fields of an exported type default to exported; a record whose values
# leave the module through exported functions keeps unexported fields private.
MODEL = '''module Model
type Record:
  export id: Int
  secret: Int

export fn make(value: Int) -> Record:
  return Record(id: value, secret: value * 2)

export fn reveal(record: Record) -> Int:
  return record.secret
'''

# A second file of the same module may use the private field.
MODEL_MORE = '''module Model
export fn reveal_twice(record: Record) -> Int:
  return record.secret * 2
'''


def main():
    (ROOT / "tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="phase25-field-visibility-", dir=ROOT / "tmp") as directory:
        scratch = Path(directory)
        allowed = project(scratch / "allowed", {
            "model.moss": MODEL, "model_more.moss": MODEL_MORE,
            "main.moss": '''module App
import Model
fn main():
  record = Model.make(21)
  echo record.id
  echo Model.reveal(record)
  echo Model.reveal_twice(record)
'''})
        runs(allowed, "21\n42\n84\n")

        denied = project(scratch / "denied", {
            "model.moss": MODEL,
            "main.moss": '''module App
import Model
fn main():
  record = Model.make(21)
  echo record.secret
'''})
        checked = run(MOSS, "check", denied / "src/main.moss", "--json")
        error = json.loads(checked.stdout).get("error") or {}
        assert checked.returncode != 0 and error.get("code") == "FIELD_VISIBILITY_ERROR", checked.stdout
        assert error.get("line") == 5, error

        unmodular = project(scratch / "unmodular", {
            "shapes.moss": '''type Point:
  export x: Int
  y: Int
''',
            "main.moss": '''fn main():
  point = Point(x: 1, y: 2)
  echo point.x + point.y
'''})
        runs(unmodular, "3\n")
    print("Phase 25 record field visibility: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
