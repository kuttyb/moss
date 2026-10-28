"""Exercise the public collection API from compiled-only provider artifacts."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

PACKAGE = Path(__file__).resolve().parents[1]
ROOT = Path(__file__).resolve().parents[4]
MOSS = Path(os.environ.get("MOSS", ROOT / "moss")).resolve()
MARGO = ROOT / "margo"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)
ENV = os.environ.copy()
ENV.update(MOSS=str(MOSS), MARGO_HOME=str(SCRATCH / "margo-collections-source-free"), TMPDIR=str(SCRATCH))

subprocess.run([str(MARGO), "build", "--json"], cwd=PACKAGE, env=ENV, check=True, capture_output=True, text=True)
with tempfile.TemporaryDirectory(prefix="collections-source-free-", dir=SCRATCH) as temporary:
    work = Path(temporary)
    provider = work / "provider"
    app = work / "app"
    provider.mkdir()
    (app / "src").mkdir(parents=True)
    for name in ("collections.mossi", "libcollections.rlib"):
        shutil.copy2(PACKAGE / "build" / "debug" / name, provider / name)
    (app / "Moss.toml").write_text(
        '[package]\nname = "collections_source_free_smoke"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n', encoding="utf-8")
    (app / "src" / "main.moss").write_text(
        'module app\nimport collections\n\nfn main():\n'
        '  words = ["red", "blue", "red"]\n'
        '  echo collections.index_of(words, "red")\n'
        '  echo collections.count_equal(words, "red")\n'
        '  echo collections.contains(words, "blue")\n', encoding="utf-8")
    source_free_env = ENV | {"MOSS_MODULE_PATH": str(provider)}
    check = subprocess.run([str(MOSS), "check", "src/main.moss", "--json"],
                           cwd=app, env=source_free_env, check=True, capture_output=True, text=True)
    assert json.loads(check.stdout)["ok"] is True, check.stdout
    subprocess.run([str(MARGO), "build", "--moss-json"], cwd=app, env=source_free_env,
                   check=True, capture_output=True, text=True)
    result = subprocess.run([str(app / "build" / "debug" / "collections_source_free_smoke")],
                            cwd=app, env=source_free_env, check=True, capture_output=True, text=True)
    assert result.stdout == "0\n2\ntrue\n", result.stdout
print("Compiled-only collections provider: check, build, run passed")
