"""Exercise integer algorithms from compiled-only provider artifacts."""

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
ENV.update(MOSS=str(MOSS), MARGO_HOME=str(SCRATCH / "margo-algorithms-source-free"), TMPDIR=str(SCRATCH))

subprocess.run([str(MARGO), "build", "--json"], cwd=PACKAGE, env=ENV, check=True, capture_output=True, text=True)
with tempfile.TemporaryDirectory(prefix="algorithms-source-free-", dir=SCRATCH) as temporary:
    work = Path(temporary)
    provider = work / "provider"
    app = work / "app"
    provider.mkdir()
    (app / "src").mkdir(parents=True)
    for name in ("algorithms.mossi", "libalgorithms.rlib"):
        shutil.copy2(PACKAGE / "build" / "debug" / name, provider / name)
    (app / "Moss.toml").write_text(
        '[package]\nname = "algorithms_source_free_smoke"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n', encoding="utf-8")
    (app / "src" / "main.moss").write_text(
        'module app\nimport algorithms\n\nfn main():\n'
        '  var values = [4, 2, 3, 2, 1]\n'
        '  algorithms.sort_int(values)\n'
        '  echo values[0], values[1], values[2], values[3], values[4]\n'
        '  echo algorithms.lower_bound_int(values, 2)\n'
        '  echo algorithms.binary_search_int(values, 9)\n', encoding="utf-8")
    source_free_env = ENV | {"MOSS_MODULE_PATH": str(provider)}
    check = subprocess.run([str(MOSS), "check", "src/main.moss", "--json"],
                           cwd=app, env=source_free_env, check=True, capture_output=True, text=True)
    assert json.loads(check.stdout)["ok"] is True, check.stdout
    subprocess.run([str(MARGO), "build", "--moss-json"], cwd=app, env=source_free_env,
                   check=True, capture_output=True, text=True)
    result = subprocess.run([str(app / "build" / "debug" / "algorithms_source_free_smoke")],
                            cwd=app, env=source_free_env, check=True, capture_output=True, text=True)
    assert result.stdout == "1 2 2 3 4\n1\n-1\n", result.stdout
print("Compiled-only algorithms provider: check, build, run passed")
