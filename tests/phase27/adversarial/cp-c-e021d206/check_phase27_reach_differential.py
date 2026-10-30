#!/usr/bin/env python3
"""Compare legacy and EST Fast Debug reachability edge/closure sets.

The legacy compiler must be built from phase27-1-m1. The candidate runs with
MOSS_EST=shadow. Both binaries check the same archived source tree, so paths,
package layouts, and entry points are identical within each run.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[2]
REV = "HEAD"
EXPECTED_LEGACY_SHA256 = (
    "4a9d7d7509f9883194f2d3f147c4ff66c39e9c832e758ddffdd7572d57667ffd")


def trace(binary: Path, mode: str, corpus: Path, source: str, timeout: int):
    env = os.environ.copy()
    env["MOSS_EST"] = mode
    env["MOSS_PHASE26_C1_SCOPE_TRACE"] = "1"
    result = subprocess.run([str(binary), "check", source, "--json"],
                            cwd=corpus, env=env, capture_output=True,
                            text=True, timeout=timeout)
    try:
        response = json.loads(result.stdout)
        error = response.get("error") or {}
        verdict = (response.get("ok"), error.get("code"), error.get("line"))
    except (json.JSONDecodeError, AttributeError):
        verdict = ("NOJSON", result.returncode, None)
    edges, checked = set(), set()
    complete = False
    prefix = str(corpus) + "/"
    for row in result.stderr.splitlines():
        if row.startswith("MOSS_C1_SCOPE_TRACE_COMPLETE|"):
            complete = True
        elif row.startswith("MOSS_C1_SCOPE_EDGE|"):
            _, owner, kind, target = row.split("|", 3)
            owner = owner.replace(prefix, "")
            edges.add((owner, kind, target))
        elif row.startswith("MOSS_C1_SCOPE_ID|"):
            _, kind, source_file, identity = row.split("|", 3)
            checked.add((kind, source_file.replace(prefix, ""), identity))
    return {"edges": edges, "checked": checked, "trace_complete": complete,
            "verdict": verdict, "verdict_ok": result.returncode == 0}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("legacy", type=Path,
                        help="phase27-1-m1 compiler with C1 trace support")
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--rev", default=REV)
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 4, 8))
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--expected-legacy-sha256",
                        default=EXPECTED_LEGACY_SHA256,
                        help="required SHA-256 for the frozen phase27-1-m1 compiler")
    parser.add_argument("--out", type=Path,
                        default=ROOT / "tmp/phase27-2/reach-differential.json")
    args = parser.parse_args()
    legacy, candidate = args.legacy.resolve(), args.candidate.resolve()
    legacy_sha256 = hashlib.sha256(legacy.read_bytes()).hexdigest()
    if legacy_sha256 != args.expected_legacy_sha256:
        raise SystemExit(
            "legacy compiler SHA-256 mismatch: "
            f"expected {args.expected_legacy_sha256}, got {legacy_sha256}")
    sources = subprocess.check_output(
        ["git", "ls-tree", "-r", "--name-only", args.rev], cwd=ROOT,
        text=True).splitlines()
    sources = [source for source in sources if source.endswith(".moss")]
    base_root = ROOT / "tmp/phase27-2/reach-differential-base"
    tree_root = ROOT / "tmp/phase27-2/reach-differential-tree"
    for destination in (base_root, tree_root):
        shutil.rmtree(destination, ignore_errors=True)
        destination.mkdir(parents=True)
        archive = subprocess.check_output(["git", "archive", args.rev], cwd=ROOT)
        with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
            tar.extractall(destination, filter="data")

    def compare(source: str):
        before = trace(legacy, "off", base_root, source, args.timeout)
        after = trace(candidate, "shadow", tree_root, source, args.timeout)
        return source, before, after

    failures, rows = [], []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for source, before, after in pool.map(compare, sources):
            edge_missing = sorted(before["edges"] - after["edges"])
            edge_added = sorted(after["edges"] - before["edges"])
            checked_missing = sorted(before["checked"] - after["checked"])
            checked_added = sorted(after["checked"] - before["checked"])
            row = {"entry": source,
                   "legacy_trace_complete": before["trace_complete"],
                   "tree_trace_complete": after["trace_complete"],
                   "legacy_verdict_ok": before["verdict_ok"],
                   "tree_verdict_ok": after["verdict_ok"],
                   "legacy_verdict": before["verdict"],
                   "tree_verdict": after["verdict"],
                   "edge_missing": edge_missing, "edge_added": edge_added,
                   "checked_missing": checked_missing,
                   "checked_added": checked_added,
                   "legacy_edges": sorted(before["edges"]),
                   "tree_edges": sorted(after["edges"]),
                   "legacy_checked": sorted(before["checked"]),
                   "tree_checked": sorted(after["checked"])}
            rows.append(row)
            if (edge_missing or edge_added or checked_missing or checked_added or
                    before["trace_complete"] != after["trace_complete"] or
                    before["verdict"] != after["verdict"]):
                failures.append(row)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps({"revision": args.rev,
                                    "legacy_revision": "phase27-1-m1",
                                    "legacy_binary_sha256": legacy_sha256,
                                    "candidate_binary_sha256": hashlib.sha256(
                                        candidate.read_bytes()).hexdigest(),
                                    "source_count": len(sources),
                                    "failures": failures,
                                    "entries": rows}, indent=2) + "\n")
    try:
        report_name = args.out.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        report_name = str(args.out)
    print(f"{len(sources)} entries; {len(failures)} reachability differences; "
          f"report {report_name}")
    for failure in failures:
        print(f"ENTRY {failure['entry']}")
        for kind in ("edge_missing", "edge_added", "checked_missing",
                     "checked_added"):
            for value in failure[kind]:
                print(f"  {kind}: {value}")
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
