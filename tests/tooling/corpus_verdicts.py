#!/usr/bin/env python3
"""Record or compare `moss check` verdicts over every tracked .moss file.

A checker refactor must not change which programs are accepted or the
diagnostic each rejected program receives. This tool checks every tracked
`.moss` file of a revision with one compiler and writes `path<TAB>verdict`,
where the verdict is `ok` or `<code>:<line>`. With `--compare` it reports
every file whose verdict differs from a previously recorded run.

Each run uses a private export of the revision under `tmp/`, so semantic
caches never touch the worktree and two compilers never share a cache.

    python3 tests/tooling/corpus_verdicts.py BASE_MOSS tmp/base.tsv --rev HEAD
    python3 tests/tooling/corpus_verdicts.py ./moss tmp/new.tsv --rev HEAD \
        --compare tmp/base.tsv
"""

import argparse
import concurrent.futures
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def verdict(moss: Path, corpus: Path, source: str, timeout: int) -> str:
    try:
        completed = subprocess.run(
            [str(moss), "check", source, "--json"], cwd=corpus,
            capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return "TIMEOUT"
    try:
        result = json.loads(completed.stdout)
    except json.JSONDecodeError:
        return "NOJSON"
    if result.get("ok"):
        return "ok"
    error = result.get("error") or {}
    return f"{error.get('code')}:{error.get('line')}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("moss", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--rev", default="HEAD")
    parser.add_argument("--compare", type=Path)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()

    moss = args.moss.resolve()
    corpus = ROOT / "tmp" / "corpus-verdicts" / args.output.stem
    shutil.rmtree(corpus, ignore_errors=True)
    corpus.mkdir(parents=True)
    archive = subprocess.run(["git", "archive", args.rev], cwd=ROOT,
                             capture_output=True, check=True).stdout
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(corpus, filter="data")
    sources = [path for path in subprocess.run(
        ["git", "ls-tree", "-r", "--name-only", args.rev], cwd=ROOT,
        capture_output=True, text=True, check=True).stdout.splitlines()
        if path.endswith(".moss")]

    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        verdicts = dict(zip(sources, pool.map(
            lambda source: verdict(moss, corpus, source, args.timeout), sources)))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("".join(
        f"{source}\t{verdicts[source]}\n" for source in sorted(verdicts)))
    print(f"{len(verdicts)} sources checked; "
          f"{sum(v == 'ok' for v in verdicts.values())} accepted")

    if not args.compare:
        return 0
    previous = dict(line.split("\t", 1) for line in
                    args.compare.read_text().splitlines() if line)
    changed = sorted(source for source in set(previous) | set(verdicts)
                     if previous.get(source, "<absent>").strip() !=
                     verdicts.get(source, "<absent>"))
    for source in changed:
        print(f"{source}: {previous.get(source, '<absent>').strip()} -> "
              f"{verdicts.get(source, '<absent>')}")
    print(f"{len(changed)} verdict changes")
    return 1 if changed else 0


if __name__ == "__main__":
    sys.exit(main())
