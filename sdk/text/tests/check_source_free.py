"""Exercise moss.text through current provider interfaces."""

import json
import os
from pathlib import Path
import subprocess
import tempfile

PACKAGE = Path(__file__).resolve().parents[1]
ROOT = Path(__file__).resolve().parents[3]
MOSS = Path(os.environ.get("MOSS", ROOT / "moss")).resolve()
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
    app = work / "app"
    (app / "src").mkdir(parents=True)
    (app / "Moss.toml").write_text(
        '[package]\nname = "text_source_free_smoke"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n', encoding="utf-8")
    (app / "src" / "main.moss").write_text(
        'module app\nimport text\n\n'
        'fn copy_twice(value: String) -> String:\n'
        '  return value + value\n\n'
        'fn main():\n'
        '  echo text.find("aé🙂z", "é🙂")\n'
        '  echo text.slice("aé🙂z", 1, 2)\n'
        '  echo text.to_ascii_lower("AZ-é🙂")\n'
        '  echo text.to_ascii_upper("az-é🙂")\n'
        '  var builder = text.new_builder()\n'
        '  text.builder_append(builder, "Moss")\n'
        '  text.builder_append(builder, "🙂")\n'
        '  echo text.builder_build(builder)\n'
        '  var capacity = text.new_capacity_builder(16)\n'
        '  text.capacity_builder_append(capacity, "Moss")\n'
        '  text.capacity_builder_append_line(capacity, "🙂")\n'
        '  other = "!"\n'
        '  text.capacity_builder_append(capacity, copy_twice(other))\n'
        '  echo text.capacity_builder_build(capacity)\n'
        '  echo text.capacity_builder_finish(capacity)\n', encoding="utf-8")
    source_free_env = ENV | {
        "MOSS_MODULE_PATH": ":".join((
            str(PACKAGE / "build" / "debug"),
            str(CORE / "build" / "debug"),
        ))
    }
    checked = subprocess.run([str(MOSS), "check", "src/main.moss", "--json"],
                             cwd=app, env=source_free_env, check=True,
                             capture_output=True, text=True)
    assert json.loads(checked.stdout)["ok"] is True, checked.stdout
    subprocess.run([str(MOSS), "build", "--json"], cwd=app,
                   env=source_free_env, check=True, capture_output=True, text=True)
    result = subprocess.run([str(app / "build" / "debug" / "text_source_free_smoke")],
                            cwd=app, env=source_free_env, check=True,
                            capture_output=True, text=True)
    assert result.stdout == "1\né🙂\naz-é🙂\nAZ-é🙂\nMoss🙂\nMoss🙂\n!!\nMoss🙂\n!!\n", result.stdout
    (app / "src" / "main.moss").write_text(
        'module app\nimport text\n\nfn main():\n'
        '  var builder = text.new_capacity_builder(16)\n'
        '  text.capacity_builder_append(builder, builder)\n', encoding="utf-8")
    rejected = subprocess.run([str(MOSS), "check", "src/main.moss", "--json"],
                              cwd=app, env=source_free_env, check=False,
                              capture_output=True, text=True)
    assert rejected.returncode != 0, rejected.stdout
    diagnostic = json.loads(rejected.stdout)
    assert diagnostic["error"]["code"] == "OWNERSHIP_CONFLICTING_ACCESS", diagnostic
    (app / "src" / "main.moss").write_text(
        'module app\nimport text\n\n'
        'fn copy_twice(value: String) -> String:\n'
        '  return value + value\n\n'
        'fn main():\n'
        '  var builder = text.new_capacity_builder(16)\n'
        '  text.capacity_builder_append(builder, copy_twice(builder))\n',
        encoding="utf-8")
    nested = subprocess.run([str(MOSS), "check", "src/main.moss", "--json"],
                            cwd=app, env=source_free_env, check=False,
                            capture_output=True, text=True)
    assert nested.returncode != 0, nested.stdout
    nested_diagnostic = json.loads(nested.stdout)
    assert nested_diagnostic["error"]["code"] == \
        "OWNERSHIP_CONFLICTING_ACCESS", nested_diagnostic
    assert "text__capacity_builder_append" in \
        nested_diagnostic["error"]["message"], nested_diagnostic
    assert "overlapping WRITE and READ" in \
        nested_diagnostic["error"]["message"], nested_diagnostic
print("Current moss.text provider interface: check, build, run passed")
