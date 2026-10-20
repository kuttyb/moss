#!/usr/bin/env python3
"""Verify source-backed-invalid R0 controls so v10 gaps are not guessed."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
MARGO = ROOT / "margo"
CORPUS = ROOT / "tests/phase25/source_free_corpus/corpus.json"
TMP = ROOT / "tmp"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=TMP / "phase25-1-invalid-probes.json")
    args = parser.parse_args()
    TMP.mkdir(exist_ok=True)
    corpus = json.loads(CORPUS.read_text(encoding="utf-8"))
    results = []
    for case in corpus.get("source_backed_invalid", []):
        with tempfile.TemporaryDirectory(prefix=f"{case['id']}-", dir=TMP) as name:
            project = Path(name)
            source = project / "src"
            source.mkdir()
            (project / "Moss.toml").write_text(
                f'[package]\nname = "{case["id"].replace("-", "_")}"\nversion = "0.1.0"\n\n'
                '[build]\nsource = "src"\n', encoding="utf-8")
            (source / "service.moss").write_text(case["provider"], encoding="utf-8")
            (source / "main.moss").write_text(case["consumer"], encoding="utf-8")
            env = os.environ.copy()
            env["MOSS"] = str(ROOT / "moss")
            env["MARGO_HOME"] = str(TMP / "margo-invalid-probes" / case["id"])
            env["TMPDIR"] = str(TMP)
            env.pop("MOSS_STAGED_ABI", None)
            result = subprocess.run([str(MARGO), "build", "--json"], cwd=project,
                                    env=env, capture_output=True, text=True, timeout=900)
            output = (result.stdout + result.stderr).replace(str(ROOT), "$REPO")
            code = case["expected_diagnostic"]
            expected_message = case["expected_message"]
            ok = result.returncode != 0 and code in output and expected_message in output
            results.append({"id": case["id"], "status": "invalid-by-language" if ok else "unexpected-result",
                            "exit_code": result.returncode, "diagnostic": code,
                            "expected_message": expected_message, "output": output[-5000:]})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    payload = {"schema": "phase25-1-invalid-probes-v1",
               "compiler_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
               "cases": results}
    args.output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"compiler_commit": payload["compiler_commit"],
                      "results": [{"id": row["id"], "status": row["status"], "diagnostic": row["diagnostic"]}
                                  for row in results],
                      "output": str(args.output)}, indent=2))
    return 0 if all(row["status"] == "invalid-by-language" for row in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
