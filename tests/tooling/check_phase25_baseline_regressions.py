#!/usr/bin/env python3
"""Small delivered-baseline expression controls for Phase 25.1 stabilization."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)

CASES = {
    "quoted-path": 'fn main():\n  echo "tmp/phase21-fileio/input.dat"\n',
    "operator-punctuation-string": (
        'fn main():\n'
        '  let lookup = "\\n\\r\\t !\\\"#$%&\'()*+,-./0123456789"\n'
        '  echo lookup\n'
    ),
    "static-scalar-protocol": (
        "fn main():\n"
        "  echo Int.hash(1)\n"
        "  echo Float.isequal(1.0, 1.0)\n"
    ),
    "indexed-field-in-constructor": (
        "type Item:\n"
        "  name: String\n"
        "  kind: Int\n"
        "  nullable: Bool\n\n"
        "fn copy_items(fields: Vector[Item]) -> Vector[Item]:\n"
        "  var result = Vector[Item]()\n"
        "  var i = 0\n"
        "  n = fields |> count\n"
        "  while i < n:\n"
        "    result.push(Item(name: fields[i].name + \"\", "
        "kind: fields[i].kind, nullable: fields[i].nullable))\n"
        "    i = i + 1\n"
        "  return result\n\n"
        "fn main():\n"
        "  echo copy_items([Item(name: \"x\", kind: 1, nullable: false)]) |> count\n"
    ),
}

with tempfile.TemporaryDirectory(prefix="phase25-baseline-regressions-", dir=TMP) as scratch:
    failures = []
    for name, source in CASES.items():
        path = Path(scratch) / f"{name}.moss"
        path.write_text(source)
        result = subprocess.run([str(MOSS), "check", str(path), "--json"],
                                cwd=ROOT, capture_output=True, text=True)
        payload = json.loads(result.stdout)
        if result.returncode != 0 or not payload["ok"]:
            failures.append((name, result.returncode, payload, result.stderr))
            print(f"FAIL {name}: {payload.get('error', {}).get('message')}")
        else:
            print(f"PASS {name}: checked baseline expression")
    assert not failures, failures
