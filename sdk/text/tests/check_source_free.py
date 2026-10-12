"""Exercise moss.text from compiled-only ABI-v9 provider artifacts."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

PACKAGE = Path(__file__).resolve().parents[1]
ROOT = Path(__file__).resolve().parents[3]
MOSS = ROOT / "moss"
MARGO = ROOT / "margo"
CORE = ROOT / "sdk" / "foundations" / "core"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)
ENV = os.environ.copy()
ENV.update(MOSS=str(MOSS), MARGO_HOME=str(SCRATCH / "margo-text-source-free"),
           TMPDIR=str(SCRATCH))

subprocess.run([str(MARGO), "build", "--json"], cwd=PACKAGE, env=ENV,
               check=True, capture_output=True, text=True)
with tempfile.TemporaryDirectory(prefix="text-source-free-", dir=SCRATCH) as temporary:
    work = Path(temporary)
    provider = work / "provider"
    app = work / "app"
    provider.mkdir()
    (app / "src").mkdir(parents=True)
    for source, names in (
        (PACKAGE / "build" / "debug", ("text.mossi", "libtext.rlib")),
        (CORE / "build" / "debug", ("core.mossi", "libcore.rlib")),
    ):
        for name in names:
            shutil.copy2(source / name, provider / name)
    (app / "Moss.toml").write_text(
        '[package]\nname = "text_source_free_smoke"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n', encoding="utf-8")
    (app / "src" / "main.moss").write_text(
        'module app\nimport text\n\nfn main():\n'
        '  echo text.find("aé🙂z", "é🙂")\n'
        '  echo text.slice("aé🙂z", 1, 2)\n'
        '  echo text.to_ascii_lower("AZ-é🙂")\n'
        '  echo text.to_ascii_upper("az-é🙂")\n'
        '  var builder = text.new_builder()\n'
        '  text.builder_append(builder, "Moss")\n'
        '  text.builder_append(builder, "🙂")\n'
        '  echo text.builder_build(builder)\n', encoding="utf-8")
    source_free_env = ENV | {"MOSS_MODULE_PATH": str(provider)}
    checked = subprocess.run([str(MOSS), "check", "src/main.moss", "--json"],
                             cwd=app, env=source_free_env, check=True,
                             capture_output=True, text=True)
    assert json.loads(checked.stdout)["ok"] is True, checked.stdout
    subprocess.run([str(MOSS), "build", "--json"], cwd=app,
                   env=source_free_env, check=True, capture_output=True, text=True)
    result = subprocess.run([str(app / "build" / "debug" / "text_source_free_smoke")],
                            cwd=app, env=source_free_env, check=True,
                            capture_output=True, text=True)
    assert result.stdout == "1\né🙂\naz-é🙂\nAZ-é🙂\nMoss🙂\n", result.stdout
print("Compiled-only moss.text provider: check, build, run passed")
