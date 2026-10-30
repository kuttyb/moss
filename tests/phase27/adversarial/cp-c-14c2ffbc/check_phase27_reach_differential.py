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

# Exact module-qualified calls whose same-leaf homonyms are selected by the
# retired text splitter. LANGUAGE_SYNTAX §Modules makes the qualified target
# unambiguous; the archived source entry itself is replayed through both
# compilers and both checked sets are stored in the report.
LEGACY_TEXT_ARTIFACTS = {
    "examples/swarm/modules/calc_interpreter/repro/natural_full/src/parser.moss": {
        "callable-v1:module-v1:calc-interpreter@0.1.0::node:fn:kind_assign",
        "callable-v1:module-v1:calc-interpreter@0.1.0::node:fn:kind_num",
    },
    "examples/swarm/modules/calc_interpreter/repro/natural_full/tests/lexer_test.moss": {
        "callable-v1:module-v1:calc-interpreter@0.1.0::node:fn:kind_assign",
    },
    "examples/swarm/modules/calc_interpreter/repro/natural_full/tests/parser_test.moss": {
        "callable-v1:module-v1:calc-interpreter@0.1.0::node:fn:kind_num",
    },
    "examples/swarm/modules/calc_interpreter/repro/natural_full/tests/token_test.moss": {
        "callable-v1:module-v1:calc-interpreter@0.1.0::node:fn:kind_num",
    },
    "examples/swarm/modules/calc_interpreter/tests/lexer_test.moss": {
        "callable-v1:module-v1:calc-interpreter@0.1.0::node:fn:kind_assign",
    },
    "examples/swarm/modules/calc_interpreter/tests/token_test.moss": {
        "callable-v1:module-v1:calc-interpreter@0.1.0::node:fn:kind_num",
        "callable-v1:module-v1:calc-interpreter@0.1.0::node:fn:make",
    },
}
LEGACY_PIPELINE_CRASHES_FIXED = {
    "examples/swarm/expression_surface_2026/validation_engine/failed_attempts/04_pipeline_collection_inference.moss",
    "examples/swarm/modules/domain_services/repro/generic_pipeline/single/src/main.moss",
    "examples/swarm/modules/domain_services/repro/generic_pipeline/split/src/main.moss",
    "examples/swarm/modules/domain_services/repro/generic_pipeline/split/src/stats.moss",
    "examples/swarm/modules/lib_and_app/repro/generic_pipeline_method_placeholder/single/src/main.moss",
}


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
                        help="report path (default names the source and candidate SHAs)")
    args = parser.parse_args()
    legacy, candidate = args.legacy.resolve(), args.candidate.resolve()
    legacy_sha256 = hashlib.sha256(legacy.read_bytes()).hexdigest()
    if legacy_sha256 != args.expected_legacy_sha256:
        raise SystemExit(
            "legacy compiler SHA-256 mismatch: "
            f"expected {args.expected_legacy_sha256}, got {legacy_sha256}")
    revision = subprocess.check_output(
        ["git", "rev-parse", f"{args.rev}^{{commit}}"], cwd=ROOT,
        text=True).strip()
    candidate_sha256 = hashlib.sha256(candidate.read_bytes()).hexdigest()
    candidate_revision = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    candidate_dirty_paths = subprocess.check_output(
        ["git", "status", "--porcelain"], cwd=ROOT, text=True).splitlines()
    legacy_revision = subprocess.check_output(
        ["git", "rev-parse", "phase27-1-m1^{commit}"], cwd=ROOT,
        text=True).strip()
    sources = subprocess.check_output(
        ["git", "ls-tree", "-r", "--name-only", revision], cwd=ROOT,
        text=True).splitlines()
    sources = [source for source in sources if source.endswith(".moss")]
    base_root = ROOT / "tmp/phase27-2/reach-differential-base"
    tree_root = ROOT / "tmp/phase27-2/reach-differential-tree"
    for destination in (base_root, tree_root):
        shutil.rmtree(destination, ignore_errors=True)
        destination.mkdir(parents=True)
        archive = subprocess.check_output(["git", "archive", revision], cwd=ROOT)
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
    checked_missing = sum(len(row["checked_missing"]) for row in rows)
    checked_added = sum(len(row["checked_added"]) for row in rows)
    actual_text_artifacts = {
        row["entry"]: {value[2] for value in row["checked_missing"]}
        for row in rows if row["checked_missing"]
    }
    expected_text_artifacts = {
        entry: set(identities) for entry, identities in LEGACY_TEXT_ARTIFACTS.items()
    }
    unexpected_checked_missing = {
        entry: sorted(identities - expected_text_artifacts.get(entry, set()))
        for entry, identities in actual_text_artifacts.items()
        if identities != expected_text_artifacts.get(entry, set())
    }
    checked_rulings = []
    for row in rows:
        for kind, source, identity in row["checked_missing"]:
            checked_rulings.append({
                "entry": row["entry"], "kind": kind, "source": source,
                "identity": identity,
                "classification": "LEGACY MISPARSE: qualified module prefix was lost by the legacy text splitter",
                "oracle": "LANGUAGE_SYNTAX §Modules resolves the imported qualifier to the named module",
                "two_path_reproducer": {
                    "entrypoint": "moss check <same tracked source> --json",
                    "legacy_mode": "MOSS_EST=off",
                    "tree_mode": "MOSS_EST=shadow",
                    "legacy_checks_identity": identity in {
                        value[2] for value in row["legacy_checked"]
                    },
                    "tree_checks_identity": identity in {
                        value[2] for value in row["tree_checked"]
                    },
                    "legacy_verdict": row["legacy_verdict"],
                    "tree_verdict": row["tree_verdict"],
                },
            })
    allowed_baseline_crashes = [
        {"entry": row["entry"], "legacy_verdict": row["legacy_verdict"],
         "tree_verdict": row["tree_verdict"],
         "classification": "MIGRATION BUG FIX: candidate now handles deferred pipeline type state without crashing"}
        for row in rows
        if row["entry"] in LEGACY_PIPELINE_CRASHES_FIXED and
        row["legacy_verdict"][0] == "NOJSON" and
        row["tree_verdict"][0] is True
    ]
    out = args.out or ROOT / "tmp/phase27-2" / (
        f"reach-differential-{revision[:10]}-{candidate_sha256[:10]}.json")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps({"revision": revision,
                                    "candidate_revision": candidate_revision,
                                    "candidate_dirty_paths": candidate_dirty_paths,
                                    "legacy_revision": legacy_revision,
                                    "legacy_binary_sha256": legacy_sha256,
                                    "candidate_binary_sha256": candidate_sha256,
                                    "source_count": len(sources),
                                    "checked_declaration_differences": {
                                        "legacy_only": checked_missing,
                                        "tree_only": checked_added,
                                        "adjudicated_text_artifacts": checked_rulings,
                                        "unexpected_legacy_only": unexpected_checked_missing},
                                    "allowed_baseline_pipeline_crash_fixes": allowed_baseline_crashes,
                                    "comparison": {
                                        "entrypoint": "moss check <tracked .moss source> --json",
                                        "cwd": "same archived repository root for each path",
                                        "legacy_mode": "MOSS_EST=off",
                                        "candidate_mode": "MOSS_EST=shadow",
                                        "checked_key": ["kind", "relative source path", "declaration identity"],
                                        "closure_invariant": "tree checked declarations must be a superset of legacy"},
                                    "failures": failures,
                                    "entries": rows}, indent=2) + "\n")
    try:
        report_name = out.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        report_name = str(out)
    print(f"{len(sources)} entries; {len(failures)} reachability differences; "
          f"checked declarations legacy-only={checked_missing} "
          f"tree-only={checked_added}; report {report_name}")
    for failure in failures:
        print(f"ENTRY {failure['entry']}")
        for kind in ("edge_missing", "edge_added", "checked_missing",
                     "checked_added"):
            for value in failure[kind]:
                print(f"  {kind}: {value}")
    unexpected_verdict_or_trace = any(
        row["legacy_trace_complete"] != row["tree_trace_complete"] or
        (row["legacy_verdict"] != row["tree_verdict"] and
         row["entry"] not in {
             item["entry"] for item in allowed_baseline_crashes})
        for row in rows)
    return 1 if unexpected_verdict_or_trace or unexpected_checked_missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
