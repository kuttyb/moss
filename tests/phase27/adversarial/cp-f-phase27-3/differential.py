#!/usr/bin/env python3
"""Independent CP-F verdict, diagnostic, Rust, and Fast Debug corpus comparison."""
from __future__ import annotations
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[4]
BASELINE = Path(sys.argv[1]).resolve()
CANDIDATE = Path(sys.argv[2]).resolve()
BASELINE_REV = "04be1a1bbbc7a4fd8fe2e4090b4b5bad5dd573e4"
CANDIDATE_REV = "2bafad47eb15af1923fbf1673b1a6069dca0f63e"
OUT = ROOT / "tests/phase27/adversarial/cp-f-phase27-3/differential.jsonl"
TIMEOUT = int(os.environ.get("CPF_TIMEOUT", "120"))
FAST_TIMEOUT = int(os.environ.get("CPF_FAST_TIMEOUT", "8"))
NATIVE_TIMEOUT = int(os.environ.get("CPF_NATIVE_TIMEOUT", "20"))
JOBS = int(os.environ.get("CPF_JOBS", "8"))

def tracked(rev: str) -> set[str]:
    return set(subprocess.check_output(["git", "ls-tree", "-r", "--name-only", rev], cwd=ROOT, text=True).splitlines())

def invoke(argv, cwd, env, timeout=TIMEOUT):
    try:
        p = subprocess.run(argv, cwd=cwd, env=env, text=True, capture_output=True, timeout=timeout)
        return {"code": p.returncode, "stdout": p.stdout, "stderr": p.stderr}
    except subprocess.TimeoutExpired as e:
        dec = lambda x: x.decode("utf-8", "replace") if isinstance(x, bytes) else (x or "")
        return {"code": "TIMEOUT", "timeout_seconds": timeout, "stdout": dec(e.stdout), "stderr": dec(e.stderr)}

def clean_json(text: str):
    try: value = json.loads(text)
    except json.JSONDecodeError: return {"non_json": text}
    # The absolute source path is constant across compiler runs; normalize it
    # to the repository-relative source path for a durable cross-worktree row.
    if isinstance(value, dict):
        for key in ("source_file",):
            if key in value and isinstance(value[key], str):
                try: value[key] = Path(value[key]).resolve().relative_to(ROOT).as_posix()
                except (ValueError, OSError): pass
        err = value.get("error")
        if isinstance(err, dict):
            for key in ("source_file",):
                if key in err and isinstance(err[key], str):
                    try: err[key] = Path(err[key]).resolve().relative_to(ROOT).as_posix()
                    except (ValueError, OSError): pass
            src = err.get("source")
            if isinstance(src, dict) and isinstance(src.get("file"), str):
                try: src["file"] = Path(src["file"]).resolve().relative_to(ROOT).as_posix()
                except (ValueError, OSError): pass
        result = value.get("result")
        if isinstance(result, dict) and isinstance(result.get("source_file"), str):
            try: result["source_file"] = Path(result["source_file"]).resolve().relative_to(ROOT).as_posix()
            except (ValueError, OSError): pass
    return value

def check(compiler, source, mode):
    env = os.environ.copy(); env["MOSS_EST"] = mode
    run = invoke([str(compiler), "check", str(source), "--json"], ROOT, env)
    return {"process": run, "payload": clean_json(run["stdout"])}

def compile_rust(compiler, source, mode, output, env):
    argv = [str(compiler)] + (["-O"] if mode == "optimized" else []) + [str(source), "-o", str(output)]
    emitted = invoke(argv, ROOT, env)
    if emitted["code"] != 0 or not output.exists():
        return {"emit": emitted}
    rust = output.read_bytes()
    compile_result = invoke(["rustc", "--edition=2021", str(output), "-o", str(output.with_suffix(".bin"))], ROOT, env)
    if compile_result["code"] != 0:
        return {"rust_sha256": hashlib.sha256(rust).hexdigest(), "rustc": compile_result}
    behavior = invoke([str(output.with_suffix(".bin"))], output.parent, env, NATIVE_TIMEOUT)
    return {"rust_sha256": hashlib.sha256(rust).hexdigest(), "rustc_code": compile_result["code"], "behavior": behavior}

def source_case(rel: str):
    source = ROOT / rel
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    base = check(BASELINE, source, "off")
    off = check(CANDIDATE, source, "off")
    shadow = check(CANDIDATE, source, "shadow")
    verdict = lambda row: (row["payload"].get("ok") if isinstance(row["payload"], dict) else None,
                           row["process"]["code"])
    diagnostic_equal = base["payload"] == off["payload"] == shadow["payload"]
    row = {"source": rel, "source_sha256": digest,
           "baseline_check": base, "candidate_off_check": off,
           "candidate_shadow_check": shadow,
           "verdicts": {"baseline": verdict(base), "candidate_off": verdict(off), "candidate_shadow": verdict(shadow)},
           "diagnostics_equal": diagnostic_equal}
    accepted = all(isinstance(x["payload"], dict) and x["payload"].get("ok") for x in (base, off, shadow))
    has_main = re.search(r"(?m)^\s*fn\s+main\s*\(", source.read_text(encoding="utf-8", errors="replace")) is not None
    if accepted and has_main:
        env_b = os.environ.copy(); env_b["MOSS_EST"] = "off"
        env_s = os.environ.copy(); env_s["MOSS_EST"] = "shadow"
        with tempfile.TemporaryDirectory(prefix="cpf-", dir=ROOT / "tmp/cpf-phase27-3/evidence") as td:
            w = Path(td)
            row["fast_debug"] = {
                "baseline": invoke([str(BASELINE), "run", "--interp", str(source)], w, env_b, FAST_TIMEOUT),
                "candidate_off": invoke([str(CANDIDATE), "run", "--interp", str(source)], w, env_b, FAST_TIMEOUT),
                "candidate_shadow": invoke([str(CANDIDATE), "run", "--interp", str(source)], w, env_s, FAST_TIMEOUT),
            }
            row["fast_debug_equal"] = row["fast_debug"]["baseline"] == row["fast_debug"]["candidate_off"] == row["fast_debug"]["candidate_shadow"]
            row["rust"] = {}
            for who, comp, env in (("baseline", BASELINE, env_b), ("candidate_off", CANDIDATE, env_b), ("candidate_shadow", CANDIDATE, env_s)):
                for profile in ("default", "optimized"):
                    row["rust"][who + "_" + profile] = compile_rust(comp, source, profile, w / f"{who}-{profile}.rs", env)
            rust_items = list(row["rust"].values())
            behaviors = [item.get("behavior") for item in rust_items]
            row["rust_behaviors_equal"] = all(b == behaviors[0] for b in behaviors)
            row["rust_sources_equal_by_profile"] = {
                profile: len({row["rust"][who + "_" + profile].get("rust_sha256") for who in ("baseline", "candidate_off", "candidate_shadow")}) == 1
                for profile in ("default", "optimized")}
    return row

def main():
    if subprocess.check_output(["git", "rev-parse", BASELINE_REV + "^{commit}"], cwd=ROOT, text=True).strip() != BASELINE_REV:
        raise SystemExit("baseline revision mismatch")
    if subprocess.check_output(["git", "rev-parse", CANDIDATE_REV + "^{commit}"], cwd=ROOT, text=True).strip() != CANDIDATE_REV:
        raise SystemExit("candidate revision mismatch")
    (ROOT / "tmp/cpf-phase27-3/evidence").mkdir(parents=True, exist_ok=True)
    bpaths, cpaths = tracked(BASELINE_REV), tracked(CANDIDATE_REV)
    corpus = sorted(p for p in bpaths | cpaths if p.endswith(".moss"))
    rows = []
    with OUT.open("w", encoding="utf-8") as f:
        with concurrent.futures.ThreadPoolExecutor(max_workers=JOBS) as pool:
            futures = {pool.submit(source_case, rel): rel for rel in corpus}
            for done_count, future in enumerate(concurrent.futures.as_completed(futures), 1):
                row = future.result()
                rows.append(row)
                f.write(json.dumps(row, sort_keys=True) + "\n")
                f.flush()
                if done_count % 50 == 0:
                    print(f"completed {done_count}/{len(corpus)} source rows", flush=True)
    differing = [r for r in rows if not r["diagnostics_equal"] or r.get("fast_debug_equal") is False or r.get("rust_behaviors_equal") is False or any(v is False for v in r.get("rust_sources_equal_by_profile", {}).values())]
    report = {"baseline_revision": BASELINE_REV, "candidate_revision": CANDIDATE_REV,
              "baseline_compiler_sha256": hashlib.sha256(BASELINE.read_bytes()).hexdigest(),
              "candidate_compiler_sha256": hashlib.sha256(CANDIDATE.read_bytes()).hexdigest(),
              "baseline_tracked_moss_count": sum(x.endswith(".moss") for x in bpaths),
              "candidate_tracked_moss_count": sum(x.endswith(".moss") for x in cpaths),
              "union_case_count": len(rows), "common_case_count": sum(p in bpaths and p in cpaths for p in corpus),
              "candidate_only_case_count": sum(p in cpaths and p not in bpaths for p in corpus),
              "diagnostic_or_behavior_difference_count": len(differing),
              "differing_sources": [r["source"] for r in differing],
              "accepted_case_count": sum(all(isinstance(r[k]["payload"], dict) and r[k]["payload"].get("ok") for k in ("baseline_check", "candidate_off_check", "candidate_shadow_check")) for r in rows),
              "fast_debug_case_count": sum("fast_debug" in r for r in rows),
              "fast_debug_difference_count": sum(r.get("fast_debug_equal") is False for r in rows),
              "native_behavior_case_count": sum("rust" in r for r in rows),
              "fast_debug_timeout_count": sum(1 for r in rows if "fast_debug" in r and any(x["code"] == "TIMEOUT" for x in r["fast_debug"].values())),
              "native_timeout_count": sum(1 for r in rows if "rust" in r and any(x.get("behavior", {}).get("code") == "TIMEOUT" for x in r["rust"].values())),
              "raw_results": str(OUT.relative_to(ROOT))}
    report_path = ROOT / "docs/phase27/adversarial/CP_F_PHASE27_3_DIFFERENTIAL.json"
    report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 1 if differing else 0

if __name__ == "__main__": raise SystemExit(main())
