"""Exercise moss.encoding from compiled-only ABI-v9 provider artifacts."""

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
BYTES = ROOT / "sdk" / "foundations" / "bytes"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)
ENV = os.environ.copy()
ENV.update(MOSS=str(MOSS), MARGO_HOME=str(SCRATCH / "margo-encoding-source-free"),
           TMPDIR=str(SCRATCH))

subprocess.run([str(MARGO), "build", "--json"], cwd=PACKAGE, env=ENV,
               check=True, capture_output=True, text=True)
with tempfile.TemporaryDirectory(prefix="encoding-source-free-", dir=SCRATCH) as temporary:
    work = Path(temporary)
    provider = work / "provider"
    app = work / "app"
    provider.mkdir()
    (app / "src").mkdir(parents=True)
    for source, names in (
        (PACKAGE / "build" / "debug", ("encoding.mossi", "libencoding.rlib")),
        (BYTES / "build" / "debug", ("bytes.mossi", "libbytes.rlib")),
    ):
        for name in names:
            shutil.copy2(source / name, provider / name)
    (app / "Moss.toml").write_text(
        '[package]\nname = "encoding_source_free_smoke"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n', encoding="utf-8")
    (app / "src" / "main.moss").write_text(
        'module app\nimport encoding\n\nfn main():\n'
        '  try:\n'
        '    encoded = encoding.encode_utf8([0, 127, 128578])\n'
        '    echo encoded |> count\n'
        '    decoded = encoding.decode_utf8(encoded)\n'
        '    echo decoded[2]\n'
        '  recover error:\n'
        '    echo -1\n', encoding="utf-8")
    source_free_env = ENV | {"MOSS_MODULE_PATH": str(provider)}
    checked = subprocess.run([str(MOSS), "check", "src/main.moss", "--json"],
                             cwd=app, env=source_free_env, check=True,
                             capture_output=True, text=True)
    assert json.loads(checked.stdout)["ok"] is True, checked.stdout
    subprocess.run([str(MOSS), "build", "--json"], cwd=app,
                   env=source_free_env, check=True, capture_output=True, text=True)
    result = subprocess.run([str(app / "build" / "debug" / "encoding_source_free_smoke")],
                            cwd=app, env=source_free_env, check=True,
                            capture_output=True, text=True)
    assert result.stdout == "6\n128578\n", result.stdout
print("Compiled-only moss.encoding provider: check, build, run passed")
