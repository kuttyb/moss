#!/usr/bin/env python3
"""Run standalone check verdict matrix over each checkout's tracked Moss corpus."""
import concurrent.futures
import hashlib
import json
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
MODES = ("off", "shadow")

def tracked_sources(root):
    result = subprocess.run(["git", "ls-files", "-z", "--", "*.moss"], cwd=root,
                            stdout=subprocess.PIPE, check=True).stdout
    return sorted(x.decode() for x in result.split(b"\0") if x)

def norm(diags):
    return [{k: d.get(k) for k in ("code", "severity", "message", "line", "column")}
            for d in diags]

def run(root, binary, mode, rel):
    env = os.environ.copy()
    env["MOSS_EST"] = mode
    p = subprocess.run([str(binary), "check", rel, "--json"], cwd=root, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        payload = json.loads(p.stdout)
        verdict = bool(payload.get("ok"))
        diags = norm(payload.get("result", {}).get("diagnostics", []))
        if payload.get("error"):
            diags = norm([payload["error"]])
    except Exception:
        verdict, diags = None, []
    return {"mode": mode, "file": rel, "exit": p.returncode,
            "verdict": verdict, "diagnostics": diags,
            "stdout_sha256": hashlib.sha256(p.stdout.encode()).hexdigest(),
            "stderr_sha256": hashlib.sha256(p.stderr.encode()).hexdigest(),
            "parse_error": verdict is None}

def main():
    # Args: baseline root and candidate root, each compiler is repo-local.
    base, cand = (pathlib.Path(x).resolve() for x in sys.argv[1:3])
    rows = []
    files = tracked_sources(cand)
    missing = [f for f in files if not (cand / f).is_file()]
    files = [f for f in files if (cand / f).is_file()]
    manifest = pathlib.Path(__file__).with_name("corpus-input-manifest.jsonl")
    manifest.write_text("".join(json.dumps({"file":f,"sha256":hashlib.sha256(
        (cand/f).read_bytes()).hexdigest()},sort_keys=True)+"\n" for f in files))
    if "--manifest-only" in sys.argv:
        print(json.dumps({"candidate_corpus":len(files),"missing":len(missing),
                          "manifest":str(manifest)}))
        return
    for label, binary_root, source_root in (("baseline", base, cand),
                                             ("candidate", cand, cand)):
        binary = binary_root / "moss"
        tasks = [(mode, f) for mode in MODES for f in files]
        with concurrent.futures.ThreadPoolExecutor(max_workers=24) as pool:
            results = list(pool.map(lambda x: run(source_root, binary, x[0], x[1]), tasks))
        rows.extend({"revision": label, **x} for x in results)
        print(label, len(files), "candidate corpus inputs", file=sys.stderr)
    out = pathlib.Path(__file__).with_name("corpus-matrix.jsonl")
    out.write_text("".join(json.dumps(r, sort_keys=True) + "\n" for r in rows))
    by = {(r["revision"], r["mode"], r["file"]): r for r in rows}
    files = set(files)
    mismatch = []
    for f in sorted(files):
        for mode in MODES:
            a, b = by[("baseline", mode, f)], by[("candidate", mode, f)]
            if (a["verdict"], a["diagnostics"]) != (b["verdict"], b["diagnostics"]):
                mismatch.append({"file": f, "mode": mode, "baseline": a,
                                 "candidate": b})
    pathlib.Path(__file__).with_name("corpus-diffs.json").write_text(
        json.dumps({"intersection_files": len(files), "mismatches": mismatch},
                   indent=2, sort_keys=True) + "\n")
    pathlib.Path(__file__).with_name("corpus-missing.json").write_text(
        json.dumps({"candidate_missing": missing,
                    "baseline_tracked_moss_file_count": len(tracked_sources(base))},
                   indent=2, sort_keys=True) + "\n")
    print(json.dumps({"baseline_count": sum(r["revision"] == "baseline" for r in rows),
                      "candidate_count": sum(r["revision"] == "candidate" for r in rows),
                      "candidate_corpus": len(files), "diffs": len(mismatch),
                      "candidate_missing": len(missing),
                      "baseline_tracked": len(tracked_sources(base))}, sort_keys=True))

if __name__ == "__main__":
    main()
