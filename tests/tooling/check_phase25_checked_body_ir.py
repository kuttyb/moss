#!/usr/bin/env python3
"""Check compiler-retained body semantic hashes and explicit unsupported status."""

import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
(ROOT / "tmp").mkdir(exist_ok=True)


def summary(source: Path, contents: str) -> dict:
    source.write_text(contents)
    checked = subprocess.run([str(MOSS), "inspect", "main", "--source",
                              str(source), "--json"], capture_output=True, text=True)
    assert checked.returncode == 0, (checked.stdout, checked.stderr)
    result = json.loads(checked.stdout)
    assert result["ok"], result
    summaries = result["result"]["typed_semantic_v1"]["open_summaries"]
    return next(entry for entry in summaries
                if entry["callable_id"].endswith(":fn:inc"))


with tempfile.TemporaryDirectory(prefix="phase25-checked-body-", dir=ROOT / "tmp") as scratch:
    first = summary(Path(scratch) / "first.moss", """fn inc(value):
  return value + 1

fn main():
  echo inc(10)
""")
    relocated = summary(Path(scratch) / "relocated.moss", """fn inc(value):
    return  value+1

fn main():
    echo inc(10)
""")
    changed = summary(Path(scratch) / "changed.moss", """fn inc(value):
  return value + 2

fn main():
  echo inc(10)
""")
    unsupported = summary(Path(scratch) / "unsupported.moss", """type Box:
  value

fn inc(item: Box):
  return item.value

fn main():
  echo inc(Box(value: 10))
""")
    digest = first["body_semantic_hash"]
    assert re.fullmatch(r"body-semantic-v1:sha256:[0-9a-f]{64}", digest), first
    assert first["checked_body_ir_available"], first
    assert first["body_semantic_status"] == "available_checked_body_v1", first
    assert relocated["body_semantic_hash"] == digest, (first, relocated)
    assert changed["body_semantic_hash"] != digest, (first, changed)
    assert not unsupported["checked_body_ir_available"], unsupported
    assert unsupported["body_semantic_hash"] is None, unsupported
    assert unsupported["body_semantic_status"] == "unsupported_checked_body_form", unsupported

print("checked body IR hash: literal, path, whitespace, and fail-closed cases passed")
