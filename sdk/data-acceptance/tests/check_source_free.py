"""Exercise moss.json and moss.csv from compiled-only ABI-v9 provider artifacts."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

PACKAGE = Path(__file__).resolve().parents[1]
ROOT = PACKAGE.parents[1]
MOSS = Path(os.environ.get("MOSS", ROOT / "moss")).resolve()
MARGO = ROOT / "margo"
CORE = ROOT / "sdk" / "foundations" / "core"
BYTES = ROOT / "sdk" / "foundations" / "bytes"
TEXT = ROOT / "sdk" / "text"
ENCODING = ROOT / "sdk" / "encoding"
PARSE = ROOT / "sdk" / "parse"
FMT = ROOT / "sdk" / "fmt"
JSON_PKG = ROOT / "sdk" / "json"
CSV_PKG = ROOT / "sdk" / "csv"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)
ENV = os.environ.copy()
ENV.update(
    MOSS=str(MOSS),
    MARGO_HOME=str(SCRATCH / "margo-data-source-free"),
    TMPDIR=str(SCRATCH)
)

# Build dependencies
for p in (CORE, BYTES, TEXT, ENCODING, PARSE, FMT, JSON_PKG, CSV_PKG):
    subprocess.run([str(MARGO), "build", "--json"], cwd=p, env=ENV,
                   check=True, capture_output=True, text=True)

with tempfile.TemporaryDirectory(prefix="data-source-free-", dir=SCRATCH) as temporary:
    work = Path(temporary)
    provider = work / "provider"
    app = work / "app"
    provider.mkdir()
    (app / "src").mkdir(parents=True)
    for source, names in (
        (CORE / "build" / "debug", ("core.mossi", "libcore.rlib")),
        (BYTES / "build" / "debug", ("bytes.mossi", "libbytes.rlib")),
        (TEXT / "build" / "debug", ("text.mossi", "libtext.rlib")),
        (ENCODING / "build" / "debug", ("encoding.mossi", "libencoding.rlib")),
        (PARSE / "build" / "debug", ("parse.mossi", "libparse.rlib")),
        (FMT / "build" / "debug", ("fmt.mossi", "libfmt.rlib")),
        (JSON_PKG / "build" / "debug", ("json.mossi", "libjson.rlib")),
        (CSV_PKG / "build" / "debug", ("csv.mossi", "libcsv.rlib")),
    ):
        for name in names:
            shutil.copy2(source / name, provider / name)
    (app / "Moss.toml").write_text(
        '[package]\nname = "data_source_free_smoke"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n', encoding="utf-8")
    (app / "src" / "main.moss").write_text(
        'module app\nimport json\nimport csv\n\nfn main():\n'
        '  try:\n'
        '    doc = json.parse("{\\"a\\": 1}")\n'
        '    echo json.stringify(doc)\n'
        '    table = csv.read_all("x,y\\n1,2", ",")\n'
        '    echo csv.get_field(table, 1, 0)\n'
        '  recover:\n'
        '    echo "error"\n', encoding="utf-8")
    source_free_env = ENV | {"MOSS_MODULE_PATH": str(provider)}
    checked = subprocess.run([str(MOSS), "check", "src/main.moss", "--json"],
                             cwd=app, env=source_free_env,
                             capture_output=True, text=True)
    if checked.returncode != 0:
        print("CHECK STDOUT:", checked.stdout)
        print("CHECK STDERR:", checked.stderr)
        raise SystemExit(1)
    assert json.loads(checked.stdout)["ok"] is True, checked.stdout
    subprocess.run([str(MARGO), "build", "--moss-json"], cwd=app,
                   env=source_free_env, check=True, capture_output=True, text=True)
    result = subprocess.run([str(app / "build" / "debug" / "data_source_free_smoke")],
                            cwd=app, env=source_free_env, check=True,
                            capture_output=True, text=True)
    q = '"'
    expected = f'{{{q}a{q}:1}}\n1\n'
    assert result.stdout == expected, f"got {result.stdout!r}, expected {expected!r}"
print("Compiled-only moss.json and moss.csv provider: check, build, run passed")
