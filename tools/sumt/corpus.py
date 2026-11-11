#!/usr/bin/env python3
"""Corpus parity and coverage for the SUMT shadow slice.

1. Regression: for every tests/*.moss and examples/*.moss, `moss check` and
   `moss F -o F.rs` must give byte-identical results for
     baseline (unmodified 8c363fee), candidate with shadow off, candidate with shadow on.
2. Coverage: classify every generic function's shadow entry against the
   legacy producer.
"""
import collections, json, os, re, subprocess, sys, tempfile
from pathlib import Path

base, cand, root, out = sys.argv[1], sys.argv[2], Path(sys.argv[3]), Path(sys.argv[4])
out.mkdir(parents=True, exist_ok=True)
files = sorted(list((root / "tests").glob("*.moss")) + list((root / "examples").glob("*.moss")))


def run(binary, args, shadow=None):
    env = dict(os.environ)
    env.pop("MOSS_SUMT_SHADOW", None)
    if shadow:
        env["MOSS_SUMT_SHADOW"] = shadow
    try:
        p = subprocess.run([binary] + args, capture_output=True, text=True, timeout=60,
                           env=env, cwd=root)
        return p.returncode, p.stdout + p.stderr
    except subprocess.TimeoutExpired:
        return "timeout", ""


diffs = []
coverage = collections.Counter()
reasons = collections.Counter()
events = collections.Counter()
uncovered = 0
suppressed_total = 0
legacy_vs_shadow = collections.Counter()
for f in files:
    rel = str(f.relative_to(root))
    shadow = out / (f.stem + ".jsonl")
    if shadow.exists():
        shadow.unlink()
    rs_b, rs_c, rs_s = (out / (f.stem + s) for s in (".base.rs", ".cand.rs", ".shadow.rs"))
    check_b = run(base, ["check", rel])
    check_c = run(cand, ["check", rel])
    check_s = run(cand, ["check", rel], str(shadow))
    def emit(binary, target, shadow_path=None):
        code, text_out = run(binary, [rel, "-o", str(target)], shadow_path)
        stem = str(target)[:-3]
        return code, text_out.replace(stem, "<out>")
    emit_b = emit(base, rs_b)
    emit_c = emit(cand, rs_c)
    emit_s = emit(cand, rs_s, str(out / (f.stem + ".emit.jsonl")))
    def text(p):
        return p.read_text(errors="replace") if p.exists() else "<none>"
    same = (check_b == check_c == check_s and emit_b == emit_c == emit_s and
            text(rs_b) == text(rs_c) == text(rs_s))
    if not same:
        diffs.append(rel)
    for p in (rs_b, rs_c, rs_s):
        p.unlink(missing_ok=True)
        p.with_suffix(".mossmap").unlink(missing_ok=True)
    (out / (f.stem + ".emit.jsonl")).unlink(missing_ok=True)
    if not shadow.exists():
        continue
    recs = [json.loads(l) for l in shadow.read_text().splitlines() if l.strip()]
    final = {}
    for r in recs:
        prev = final.get(r["function"])
        rank = {"final": 3, "generic_check": 2, "generic_rejected": 1}
        if not prev or rank.get(r["phase"], 0) >= rank.get(prev["phase"], 0):
            final[r["function"]] = r
    for name, r in final.items():
        sh = r["shadow"]
        coverage[sh["status"]] += 1
        for m in sh.get("missing_facts", []):
            reasons["fact: " + re.sub(r"line \d+", "line N", re.sub(r"'[^']*'", "'…'", m))] += 1
        for m in sh.get("missing_representation", []):
            reasons["repr: " + re.sub(r"line \d+", "line N", m)] += 1
        for n in sh.get("notes", []):
            reasons["note: " + re.sub(r"line \d+", "line N", n)] += 1
        lg = r.get("legacy") or {}
        legacy_state = ("complete" if lg.get("complete") else
                        "scheme" if lg.get("scheme_complete") else
                        "absent" if not lg.get("present") else "incomplete")
        legacy_vs_shadow[(legacy_state, sh["status"])] += 1
        own = r.get("ownership", {})
        lines_with_events = set()
        for e in own.get("events", []):
            events[e["kind"]] += 1
            lines_with_events.add(e["transfer_line"])
        for s in own.get("suppressed_decisions", []):
            suppressed_total += s["count"]
            if s["line"] not in lines_with_events:
                uncovered += s["count"]

report = {
    "files": len(files),
    "byte_identical": len(files) - len(diffs),
    "differences": diffs,
    "shadow_status": coverage,
    "legacy_vs_shadow": {f"{a} -> {b}": n for (a, b), n in sorted(legacy_vs_shadow.items())},
    "reasons": reasons.most_common(),
    "ownership_events": events,
    "placeholder_suppressed_decisions": suppressed_total,
    "suppressed_without_recorded_event": uncovered,
}
(out / "corpus_report.json").write_text(json.dumps(report, indent=1))
print(json.dumps(report, indent=1))
