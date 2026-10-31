#!/usr/bin/env python3
"""Compare optimized functional decisions across EST modes and baseline."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = Path(__file__).resolve().parents[2]
BASELINE = Path(sys.argv[1]).resolve()
CANDIDATE = Path(sys.argv[2]).resolve()
OUTPUT = Path(sys.argv[3]).resolve()


def sources():
    raw = subprocess.check_output(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z", "--", "*.moss"],
        cwd=ROOT)
    return sorted({item.decode() for item in raw.split(b"\0") if item})


def decision_text(output):
    retained = []
    for line in output.splitlines():
        if line.startswith("ExpressionPlan "):
            continue  # exact owner/ExprId probes are checked separately
        if line.startswith("  Source "):
            continue  # expression source formatting, not a semantic decision
        line = re.sub(r"%[0-9]+", "%ID", line)  # transient IR node/group IDs
        retained.append(line.rstrip())
    return "\n".join(retained)


def run(binary, path, mode, dump_output):
    env = os.environ.copy()
    if mode == "default": env.pop("MOSS_EST", None)
    else: env["MOSS_EST"] = mode
    result = subprocess.run(
        [str(binary), str(ROOT / path), "-O", "--dump-functional-ir",
         "-o", str(dump_output)],
        cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, timeout=120)
    return result.returncode, decision_text(result.stdout), result.stderr


def signature(text):
    return hashlib.sha256(text.encode()).hexdigest()


def adjudicate(row, values):
    if row["path"] != "tests/phase27/conformance/field_name_not_binding_use.moss":
        return ""
    baseline, candidate_off, candidate_shadow = values
    if (row["comparisons_equal"] == {
            "baseline_off_vs_candidate_off": False,
            "candidate_off_vs_candidate_shadow": True} and
            "reason: multiple consumers" in baseline[1] and
            "reason: escapes optimization scope" in candidate_off[1] and
            candidate_off[1] == candidate_shadow[1]):
        return "LEGACY MISPARSE: field access p.total counted as a use of binding total"
    return ""


def main():
    (OUTPUT.parent / "decision-parity-output").mkdir(parents=True, exist_ok=True)
    source_paths = sources()

    def compare(path):
        output_name = hashlib.sha256(path.encode()).hexdigest() + ".rs"
        dump_output = OUTPUT.parent / "decision-parity-output" / output_name
        baseline = run(BASELINE, path, "default", dump_output)
        candidate_off = run(CANDIDATE, path, "default", dump_output)
        candidate_shadow = run(CANDIDATE, path, "shadow", dump_output)
        all_success = baseline[0] == candidate_off[0] == candidate_shadow[0] == 0
        comparisons = {
            "baseline_off_vs_candidate_off": baseline[1] == candidate_off[1],
            "candidate_off_vs_candidate_shadow": candidate_off[1] == candidate_shadow[1],
        }
        row = {
            "path": path,
            "accepted_all_modes": all_success,
            "returncodes": [baseline[0], candidate_off[0], candidate_shadow[0]],
            "decision_sha256": [signature(baseline[1]), signature(candidate_off[1]),
                                signature(candidate_shadow[1])],
            "internal_invariant_reports": [
                value[2].count("MOSS_INTERNAL_INVARIANT") for value in
                (baseline, candidate_off, candidate_shadow)],
            "comparisons_equal": comparisons,
        }
        row["adjudication"] = adjudicate(
            row, (baseline, candidate_off, candidate_shadow))
        mismatch = None
        if not all(comparisons.values()):
            mismatch = {
                **row,
                "baseline_decisions": baseline[1],
                "candidate_off_decisions": candidate_off[1],
                "candidate_shadow_decisions": candidate_shadow[1],
            }
        return row, mismatch

    with ThreadPoolExecutor(max_workers=8) as executor:
        compared = list(executor.map(compare, source_paths))
    rows = [item[0] for item in compared]
    mismatches = [item[1] for item in compared if item[1]]
    accepted = sum(row["accepted_all_modes"] for row in rows)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(json.dumps({
        "baseline": str(BASELINE),
        "candidate": str(CANDIDATE),
        "sources": len(rows),
        "accepted_all_modes": accepted,
        "normalization": [
            "transient %<integer> IR pipeline/node/group IDs become %ID",
            "generated ExpressionPlan probe lines are excluded",
            "DataflowGroup Source lines are excluded because they contain source formatting",
            "all other decision, rewrite, materialization, effect/barrier and cardinality text is exact",
        ],
        "rows": rows,
        "mismatches": mismatches,
    }, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    unadjudicated = [m for m in mismatches if not m["adjudication"]]
    print(f"sources={len(rows)} accepted_all_modes={accepted} decision_mismatches={len(mismatches)} unadjudicated={len(unadjudicated)}")
    print(f"report={OUTPUT}")
    if unadjudicated:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
