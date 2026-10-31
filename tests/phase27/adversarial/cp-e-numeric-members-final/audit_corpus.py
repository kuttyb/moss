#!/usr/bin/env python3
"""Independent Phase 27.3 checked-root verdict/diagnostic/closure differential."""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[4]
NUMERIC = sorted((ROOT / "tests/phase27/est/repro").glob("numeric_member_*.moss"))


def run(binary, mode, source, timeout):
    env = os.environ.copy()
    env["MOSS_EST"] = mode
    env["MOSS_PHASE26_C1_SCOPE_TRACE"] = "1"
    p = subprocess.run([str(binary), "check", str(source), "--json"], cwd=ROOT,
                       env=env, text=True, capture_output=True, timeout=timeout)
    try:
        payload = json.loads(p.stdout)
    except json.JSONDecodeError:
        payload = {"ok": None, "error": {"code": "NO_JSON", "message": p.stdout}}
    error = payload.get("error") or {}
    span = error.get("span") or (error.get("source") or {}).get("span") or {}
    diagnostic = None if payload.get("ok") else {
        "code": error.get("code"), "message": error.get("message"),
        "line": error.get("line"), "column": error.get("column"),
        "span": {key: span.get(key) for key in
                 ("start_line", "start_column", "end_line", "end_column")}}
    prefix = str(ROOT) + "/"
    checked, edges, complete = set(), set(), False
    for line in p.stderr.splitlines():
        if line.startswith("MOSS_C1_SCOPE_TRACE_COMPLETE|"):
            complete = True
        elif line.startswith("MOSS_C1_SCOPE_ID|"):
            _, kind, src, identity = line.split("|", 3)
            checked.add((kind, src.removeprefix(prefix), identity))
        elif line.startswith("MOSS_C1_SCOPE_EDGE|"):
            _, owner, kind, target = line.split("|", 3)
            edges.add((owner.removeprefix(prefix), kind, target))
    return {"returncode": p.returncode, "ok": payload.get("ok"),
            "diagnostic": diagnostic, "trace_complete": complete,
            "checked": sorted(checked), "edges": sorted(edges),
            "stderr_sha256": hashlib.sha256(p.stderr.encode()).hexdigest()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("baseline", type=Path)
    ap.add_argument("candidate", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--jobs", type=int, default=min(os.cpu_count() or 4, 12))
    ap.add_argument("--timeout", type=int, default=60)
    args = ap.parse_args()
    baseline, candidate = args.baseline.resolve(), args.candidate.resolve()
    files = subprocess.check_output(["git", "ls-files", "-z", "*.moss"], cwd=ROOT).decode().split("\0")
    files = [ROOT / f for f in files if f]
    def one(path):
        b = run(baseline, "off", path, args.timeout)
        co = run(candidate, "off", path, args.timeout)
        cs = run(candidate, "shadow", path, args.timeout)
        verdicts = [(x["returncode"], x["ok"], x["diagnostic"]) for x in (b, co, cs)]
        closure = [set(map(tuple, x["checked"])) for x in (b, co, cs)]
        edges = [set(map(tuple, x["edges"])) for x in (b, co, cs)]
        return {"source": path.relative_to(ROOT).as_posix(),
                "baseline_off": b, "candidate_off": co, "candidate_shadow": cs,
                "verdicts_equal": verdicts[0] == verdicts[1] == verdicts[2],
                "checked_missing_candidate_off": sorted(closure[0] - closure[1]),
                "checked_added_candidate_off": sorted(closure[1] - closure[0]),
                "checked_missing_candidate_shadow": sorted(closure[0] - closure[2]),
                "checked_added_candidate_shadow": sorted(closure[2] - closure[0]),
                "checked_off_shadow_delta": sorted(closure[1] ^ closure[2]),
                "edge_missing_candidate_off": sorted(edges[0] - edges[1]),
                "edge_added_candidate_off": sorted(edges[1] - edges[0]),
                "edge_missing_candidate_shadow": sorted(edges[0] - edges[2]),
                "edge_added_candidate_shadow": sorted(edges[2] - edges[0]),
                "trace_complete_all": all(x["trace_complete"] for x in (b, co, cs))}
    rows = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(one, path): path for path in files}
        for future in concurrent.futures.as_completed(futures):
            rows.append(future.result())
    rows.sort(key=lambda r: r["source"])
    numeric = {p.relative_to(ROOT).as_posix() for p in NUMERIC}
    numeric_rows = [r for r in rows if r["source"] in numeric]
    defects = [r for r in rows if (not r["verdicts_equal"] or not r["trace_complete_all"] or
               any(r[k] for k in r if k.startswith(("checked_missing", "checked_off_shadow", "edge_missing", "edge_added"))))]
    result = {"baseline_revision": subprocess.check_output(["git", "rev-parse", "04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4^{commit}"], cwd=ROOT, text=True).strip(),
              "candidate_revision": subprocess.check_output(["git", "rev-parse", "HEAD^{commit}"], cwd=ROOT, text=True).strip(),
              "baseline_binary_sha256": hashlib.sha256(baseline.read_bytes()).hexdigest(),
              "candidate_binary_sha256": hashlib.sha256(candidate.read_bytes()).hexdigest(),
              "source_count": len(rows), "numeric_member_case_count": len(numeric_rows),
              "numeric_member_cases": numeric_rows, "defect_count": len(defects),
              "defect_sources": [r["source"] for r in defects], "entries": rows}
    args.out.write_text(json.dumps(result, separators=(",", ":")) + "\n", encoding="utf-8")
    print(f"sources={len(rows)} numeric_member_cases={len(numeric_rows)} defects={len(defects)} output={args.out}")
    return bool(defects)


if __name__ == "__main__":
    raise SystemExit(main())
