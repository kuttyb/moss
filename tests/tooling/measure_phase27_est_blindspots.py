#!/usr/bin/env python3
"""Measure EST handles whose legacy string-shape comparison never succeeds."""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def classify(source: str) -> str:
    if source.startswith("FileIO.open("):
        return "FileIO.open with a bare mode argument"
    if source.startswith("DataTools."):
        return "DataTools qualified member call"
    return "Other uppercase-qualified call"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler", type=Path)
    parser.add_argument("--output", type=Path,
                        default=Path("tmp/phase27-est-blindspots"))
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    output = args.output if args.output.is_absolute() else ROOT / args.output
    records_dir = output / f"per-source-{time.time_ns()}"
    records_dir.mkdir(parents=True, exist_ok=False)
    sources = subprocess.run(["git", "ls-files", "-z", "*.moss"], cwd=ROOT,
                             check=True, capture_output=True).stdout.decode().split("\0")
    sources = [source for source in sources if source]

    def check(source: str) -> int:
        key = hashlib.sha256(source.encode()).hexdigest()
        env = os.environ.copy()
        env["MOSS_EST"] = "shadow"
        env["MOSS_EST_BLINDSPOT_FILE"] = str(records_dir / f"{key}.tsv")
        result = subprocess.run([str(compiler), "check", str(ROOT / source), "--json"],
                                cwd=ROOT, env=env, capture_output=True,
                                text=True, timeout=60)
        return result.returncode

    with concurrent.futures.ThreadPoolExecutor(
            max_workers=min(16, os.cpu_count() or 4)) as pool:
        exits = list(pool.map(check, sources))

    contexts: dict[tuple, list[tuple]] = {}
    for record in records_dir.glob("*.tsv"):
        for line in record.read_text(encoding="utf-8").splitlines():
            fields = line.split("\t")
            if len(fields) != 10:
                raise SystemExit(f"malformed blind-spot row in {record}")
            missed, failed_source, expression, slot, owner, arg, origin, path, line_no, column = fields
            source_path = Path(path).resolve()
            try:
                relative_path = source_path.relative_to(ROOT).as_posix()
            except ValueError:
                relative_path = path
            key = (relative_path, origin, owner, slot, arg)
            contexts.setdefault(key, []).append((missed == "1", failed_source,
                                                expression, line_no, column))

    total_by_slot: dict[str, int] = {}
    blind_by_slot: dict[str, int] = {}
    forms: dict[str, dict] = {}
    for key, observations in contexts.items():
        path, origin, owner, slot, arg = key
        total_by_slot[slot] = total_by_slot.get(slot, 0) + 1
        if not all(observation[0] for observation in observations):
            continue
        failed_source, expression, line_no, column = next(
            (obs[1], obs[2], obs[3], obs[4]) for obs in observations if obs[0])
        form = classify(failed_source)
        blind_by_slot[slot] = blind_by_slot.get(slot, 0) + 1
        group = forms.setdefault(form, {"count": 0, "slots": {}, "examples": []})
        group["count"] += 1
        group["slots"][slot] = group["slots"].get(slot, 0) + 1
        example = {"path": path, "line": int(line_no), "column": int(column),
                   "slot": slot, "failed_source": failed_source,
                   "handle_expression": expression}
        if failed_source and all(item["failed_source"] != failed_source
                                 for item in group["examples"]):
            if len(group["examples"]) < 3:
                group["examples"].append(example)

    summary = {
        "sources": len(sources),
        "nonzero_check_exits": sum(code != 0 for code in exits),
        "unique_occurrences": len(contexts),
        "never_compared_occurrences": sum(blind_by_slot.values()),
        "per_slot": {
            slot: {"unique_occurrences": count,
                   "never_compared": blind_by_slot.get(slot, 0),
                   "percent": 100.0 * blind_by_slot.get(slot, 0) / count}
            for slot, count in sorted(total_by_slot.items())
        },
        "forms": forms,
        "records": str(records_dir.relative_to(ROOT)),
    }
    report = output / "summary.json"
    output.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    print(f"report={report.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
