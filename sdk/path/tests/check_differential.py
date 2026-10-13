"""Compare selected lexical normalization cases with Python POSIX paths."""

import json
import os
from pathlib import Path
import posixpath
import subprocess
import tempfile

PACKAGE = Path(__file__).resolve().parents[1]
ROOT = Path(__file__).resolve().parents[3]
MARGO = ROOT / "margo"
MOSS = ROOT / "moss"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)
ENV = os.environ.copy()
ENV.update(MOSS=str(MOSS), MARGO_HOME=str(SCRATCH / "margo-path-differential"), TMPDIR=str(SCRATCH))

cases = ["", ".", "./a", "a/../b", "a//b/./c", "../a/../../b",
         "/", "/a/../b", "/a//b/./c", "/a/../../b", "a/b/.."]
expected = [posixpath.normpath(case) for case in cases]

with tempfile.TemporaryDirectory(prefix="path-differential-", dir=SCRATCH) as temporary:
    app = Path(temporary) / "app"
    (app / "src").mkdir(parents=True)
    (app / "Moss.toml").write_text(
        '[package]\nname = "path_differential"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n\n'
        '[dependencies]\n"moss.path" = { path = "../../../sdk/path" }\n',
        encoding="utf-8")
    lines = ["module app", "import path", "", "fn main():"]
    for case in cases:
        lines.append(f"  echo path.normalize({json.dumps(case)})")
    (app / "src" / "main.moss").write_text("\n".join(lines) + "\n", encoding="utf-8")
    subprocess.run([str(MARGO), "build", "--json"], cwd=app, env=ENV,
                   check=True, capture_output=True, text=True)
    result = subprocess.run([str(app / "build" / "debug" / "path_differential")],
                            cwd=ROOT, env=ENV, check=True,
                            capture_output=True, text=True)
    actual = result.stdout.splitlines()
    assert actual == expected, {"cases": cases, "expected": expected, "actual": actual}

print("POSIX lexical normalization differential cases passed")
