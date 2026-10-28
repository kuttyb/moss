"""Compiled-only ABI-v9 consumer check for moss.path."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

PACKAGE = Path(__file__).resolve().parents[1]
ROOT = Path(__file__).resolve().parents[3]
MARGO = ROOT / "margo"
MOSS = Path(os.environ.get("MOSS", ROOT / "moss")).resolve()
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)
ENV = os.environ.copy()
ENV.update(MOSS=str(MOSS), MARGO_HOME=str(SCRATCH / "margo-path-source-free"), TMPDIR=str(SCRATCH))
subprocess.run([str(MARGO), "build", "--json"], cwd=PACKAGE, env=ENV,
               check=True, capture_output=True, text=True)

with tempfile.TemporaryDirectory(prefix="path-source-free-", dir=SCRATCH) as temporary:
    work = Path(temporary)
    provider = work / "provider"
    app = work / "app"
    provider.mkdir()
    (app / "src").mkdir(parents=True)
    for name in ("path.mossi", "libpath.rlib"):
        shutil.copy2(PACKAGE / "build" / "debug" / name, provider / name)
    (app / "Moss.toml").write_text(
        '[package]\nname = "path_source_free"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n', encoding="utf-8")
    (app / "src" / "main.moss").write_text('''module app
import path

fn main():
  assertEqual(path.normalize("/a/./b/../c"), "/a/c")
  assertEqual(path.basename("/a/c.txt"), "c.txt")
  assertEqual(path.extension("c.txt"), "txt")
  echo "path source-free ok"
''', encoding="utf-8")
    source_free_env = ENV | {"MOSS_MODULE_PATH": str(provider)}
    checked = subprocess.run([str(MOSS), "check", "src/main.moss", "--json"],
                             cwd=app, env=source_free_env, check=True,
                             capture_output=True, text=True)
    assert json.loads(checked.stdout)["ok"] is True, checked.stdout
    subprocess.run([str(MARGO), "build", "--moss-json"], cwd=app,
                   env=source_free_env, check=True, capture_output=True, text=True)
    result = subprocess.run([str(app / "build" / "debug" / "path_source_free")],
                            cwd=ROOT, env=source_free_env, check=True,
                            capture_output=True, text=True)
    assert result.stdout == "path source-free ok\n", result.stdout

print("Compiled-only ABI-v9 moss.path consumer passed")
