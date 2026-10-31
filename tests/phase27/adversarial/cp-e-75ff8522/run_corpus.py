#!/usr/bin/env python3
"""Paired MOSS_EST off/shadow corpus for the blind CP-E source audit."""
import json
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
COMPILER = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
rows = []
for source in sorted((ROOT / "tests/phase27/est").rglob("*.moss")):
    pair = {}
    for mode in ("off", "shadow"):
        env = {**os.environ, "MOSS_EST": mode}
        result = subprocess.run([str(COMPILER), "check", str(source), "--json"],
                                cwd=ROOT, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            payload = json.loads(result.stdout)
        except json.JSONDecodeError:
            payload = None
        pair[mode] = {"returncode": result.returncode, "payload": payload,
                      "stderr": result.stderr}
    a, b = pair["off"], pair["shadow"]
    # Compare user-visible verdict and diagnostic, allowing EST-specific
    # instrumentation only on stderr.
    verdict_a = (a["returncode"] == 0, (a["payload"] or {}).get("ok"),
                 (a["payload"] or {}).get("error"))
    verdict_b = (b["returncode"] == 0, (b["payload"] or {}).get("ok"),
                 (b["payload"] or {}).get("error"))
    rows.append({"source": source.relative_to(ROOT).as_posix(), "off": pair["off"],
                 "shadow": pair["shadow"], "parity": verdict_a == verdict_b,
                 "verdict": verdict_a[0]})
result = {"source_sha": subprocess.check_output(
    ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
    "corpus_root": "tests/phase27/est", "cases": len(rows),
    "parity_failures": [r["source"] for r in rows if not r["parity"]],
    "rows": rows}
out = pathlib.Path(__file__).with_name("off-shadow-corpus.json")
out.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
print(json.dumps({k: result[k] for k in ("source_sha", "cases", "parity_failures")}))
if result["parity_failures"]:
    sys.exit(1)
