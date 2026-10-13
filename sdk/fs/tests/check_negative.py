"""Verify FileIO's bounded-request safety remains visible to SDK users."""

import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
MOSS = ROOT / "moss"
with tempfile.TemporaryDirectory(prefix="host-fs-negative-", dir=ROOT / "tmp") as temporary:
    source = Path(temporary) / "unbounded_probe.moss"
    source.write_text('''fn bad(file: FileIO, offset: Int, size: Int) -> Int:
  data = file.read(offset, size)
  return data.length()

fn main():
  echo 1
''', encoding="utf-8")
    result = subprocess.run([str(MOSS), "check", str(source), "--json"],
                            capture_output=True, text=True)
    payload = json.loads(result.stdout)
    assert result.returncode != 0
    assert payload["error"]["code"] == "FILEIO_UNBOUNDED_REQUEST", payload
print("unbounded FileIO request is rejected with FILEIO_UNBOUNDED_REQUEST")
