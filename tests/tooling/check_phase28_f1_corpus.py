#!/usr/bin/env python3
"""Compare the pinned direct source corpus against the Phase 28 F1 baseline."""
from __future__ import annotations

import concurrent.futures
import json
import shutil
import subprocess
import sys
import tarfile
from io import BytesIO
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BASELINE = ROOT / "tests/tooling/phase28_f1_corpus_baseline.json"
SOURCE_REVISION = "150ab36b41e45504aaa400aa717da241be7542fb"


def compile_one(args: tuple[str, Path, Path]) -> tuple[str, str, bool]:
    label, compiler, source = args
    result = subprocess.run(
        [str(compiler), "check", str(source), "--json"],
        cwd=source.parents[1], capture_output=True, text=True, timeout=120)
    try:
        checked = json.loads(result.stdout)
    except json.JSONDecodeError:
        checked = {}
    return label, source.relative_to(source.parents[1]).as_posix(), bool(
        checked.get("ok"))


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: check_phase28_f1_corpus.py BASELINE_COMPILER CANDIDATE_COMPILER",
              file=sys.stderr)
        return 2
    baseline_compiler, candidate_compiler = (
        Path(argument).resolve() for argument in sys.argv[1:])
    scratch = ROOT / "tmp/phase28-f1-corpus"
    shutil.rmtree(scratch, ignore_errors=True)
    source_root = scratch / "sources"
    source_root.mkdir(parents=True)
    archive = subprocess.run(
        ["git", "archive", SOURCE_REVISION, "examples", "tests"],
        cwd=ROOT, capture_output=True, check=True).stdout
    with tarfile.open(fileobj=BytesIO(archive)) as tar:
        tar.extractall(source_root, filter="data")
    flat_sources = sorted(path for folder in ("examples", "tests")
                          for path in (source_root / folder).glob("*.moss"))
    if len(flat_sources) != 238:
        print(f"FAIL: expected 238 pinned inputs, found {len(flat_sources)}",
              file=sys.stderr)
        return 1

    jobs = [("baseline", baseline_compiler, source) for source in flat_sources]
    jobs.extend(("candidate", candidate_compiler, source)
                for source in flat_sources)
    with concurrent.futures.ThreadPoolExecutor(max_workers=16) as pool:
        rows = list(pool.map(compile_one, jobs))
    verdicts = {"baseline": {}, "candidate": {}}
    for label, path, accepted in rows:
        verdicts[label][path] = accepted

    baseline_data = json.loads(BASELINE.read_text())
    expected = set(baseline_data["losses"])
    losses = sorted(path for path in verdicts["baseline"]
                    if verdicts["baseline"][path] and
                    not verdicts["candidate"][path])
    gains = sorted(path for path in verdicts["candidate"]
                   if verdicts["candidate"][path] and
                   not verdicts["baseline"][path])
    new_losses = sorted(set(losses) - expected)
    print(f"sources={len(flat_sources)} "
          f"baseline_accepts={sum(verdicts['baseline'].values())} "
          f"candidate_accepts={sum(verdicts['candidate'].values())} "
          f"losses={len(losses)} gains={len(gains)}")
    print("losses:", *losses, sep="\n")
    print("new_losses:", *(new_losses or ["none"]), sep="\n")
    print("gains:", *(gains or ["none"]), sep="\n")
    return 1 if new_losses or len(losses) > len(expected) else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.SubprocessError, tarfile.TarError) as error:
        print(f"FAIL check_phase28_f1_corpus: {error}", file=sys.stderr)
        raise SystemExit(1)
