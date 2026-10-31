#!/usr/bin/env python3
"""Compare checker verdicts/diagnostics over the shared tracked Moss corpus."""
from __future__ import annotations
import concurrent.futures
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import importlib.util

ROOT = Path(__file__).resolve().parents[4]
BASELINE = ROOT.parent / "cp-e-independent-d28-baseline"
OUTPUT = ROOT / "tests/phase27/adversarial/cp-e-independent-d28/corpus-diff.jsonl"
MODES = ("baseline-off", "baseline-shadow", "candidate-off", "candidate-shadow")

def tracked(root: Path) -> set[str]:
    out = subprocess.check_output(["git", "ls-files", "-z", "*.moss"], cwd=root)
    return {x.decode() for x in out.split(b"\0") if x}

def normalize(value, root: Path):
    if isinstance(value, str): return value.replace(str(root), "<ROOT>")
    if isinstance(value, list): return [normalize(v, root) for v in value]
    if isinstance(value, dict): return {k: normalize(v, root) for k, v in value.items()}
    return value

def module_paths(root: Path) -> str:
    p = root / "sdk/advanced-data-acceptance/check_source_free.py"
    spec = importlib.util.spec_from_file_location("source_free", p)
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return ":".join(str(package / "build/debug")
                     for _name, package in module.PROVIDERS)

def run(root: Path, mode: str, source: str):
    env = os.environ.copy(); env["MOSS_EST"] = mode
    env["MOSS_MODULE_PATH"] = module_paths(root)
    binary = root / "build/compiler/moss-dev"
    try:
        p = subprocess.run([str(binary), "check", source, "--json"], cwd=root,
                           env=env, capture_output=True, text=True, timeout=120)
        try: payload = json.loads(p.stdout)
        except json.JSONDecodeError: payload = None
        if isinstance(payload, dict):
            signature = {"ok": payload.get("ok"),
                         "diagnostics": normalize(payload.get("result", {}).get("diagnostics", []), root),
                         "error": normalize(payload.get("error"), root)}
        else: signature = {"ok": "NOJSON", "stdout": p.stdout[:400], "stderr": p.stderr[:800]}
        return {"returncode": p.returncode, "signature": signature,
                "stderr": normalize(p.stderr, root)}
    except subprocess.TimeoutExpired:
        return {"returncode": "TIMEOUT", "signature": {"ok": "TIMEOUT"}}

def main():
    base, cand = tracked(BASELINE), tracked(ROOT)
    common = sorted(base & cand)
    only_base, only_candidate = sorted(base-cand), sorted(cand-base)
    jobs = int(os.environ.get("CP_E_JOBS", "8"))
    print(f"tracked_baseline={len(base)} tracked_candidate={len(cand)} common={len(common)} baseline_only={len(only_base)} candidate_only={len(only_candidate)} jobs={jobs}", flush=True)
    output=[]; started=time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {}
        for src in common:
            futures[(src,"baseline-off")] = pool.submit(run, BASELINE, "off", src)
            futures[(src,"baseline-shadow")] = pool.submit(run, BASELINE, "shadow", src)
            futures[(src,"candidate-off")] = pool.submit(run, ROOT, "off", src)
            futures[(src,"candidate-shadow")] = pool.submit(run, ROOT, "shadow", src)
        for i, src in enumerate(common, 1):
            results = {mode:futures[(src,mode)].result() for mode in MODES}
            sigs = {mode:results[mode]["signature"] for mode in MODES}
            mismatch = {"base_off_shadow": sigs["baseline-off"] != sigs["baseline-shadow"],
                        "candidate_off_shadow": sigs["candidate-off"] != sigs["candidate-shadow"],
                        "baseline_candidate_off": sigs["baseline-off"] != sigs["candidate-off"],
                        "baseline_candidate_shadow": sigs["baseline-shadow"] != sigs["candidate-shadow"]}
            row={"source":src,"results":results,"mismatch":mismatch}
            output.append(row)
            if i % 100 == 0: print(f"completed={i}/{len(common)} elapsed={time.monotonic()-started:.1f}s", flush=True)
    OUTPUT.write_text("".join(json.dumps(row,sort_keys=True)+"\n" for row in output))
    counts={k:sum(bool(r["mismatch"][k]) for r in output) for k in next(iter(output))["mismatch"]}
    print(json.dumps({"rows":len(output),"mismatches":counts,"baseline_only":only_base,"candidate_only":only_candidate,"seconds":round(time.monotonic()-started,1),"output":str(OUTPUT.relative_to(ROOT))},indent=2))

if __name__ == "__main__": main()
