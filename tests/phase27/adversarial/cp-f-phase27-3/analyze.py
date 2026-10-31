#!/usr/bin/env python3
"""Normalize nondeterministic Rust diagnostics and classify CP-F raw rows."""
import json, pathlib, re
ROOT = pathlib.Path(__file__).resolve().parents[4]
RAW = ROOT / "tests/phase27/adversarial/cp-f-phase27-3/differential.jsonl"
rows = [json.loads(line) for line in RAW.read_text(encoding="utf-8").splitlines()]
def normalized_behavior(value):
    if value is None: return None
    item = dict(value)
    item["stderr"] = re.sub(r"thread 'main' \(\d+\)", "thread 'main' (<pid>)", item.get("stderr", ""))
    item["stderr"] = re.sub(r"/[^\s:]+\.rs:(\d+:\d+)", r"<generated-rust>.rs:\1", item["stderr"])
    return item
check_base_off = [r for r in rows if r["baseline_check"]["payload"] != r["candidate_off_check"]["payload"]]
check_off_shadow = [r for r in rows if r["candidate_off_check"]["payload"] != r["candidate_shadow_check"]["payload"]]
shadow_crashes = [r for r in rows if r["candidate_shadow_check"]["process"]["code"] not in (0,1)]
fd_rows = [r for r in rows if "fast_debug" in r]
fd_timeout = [r for r in fd_rows if any(x["code"] == "TIMEOUT" for x in r["fast_debug"].values())]
fd_base_off = [r for r in fd_rows if r["fast_debug"]["baseline"] != r["fast_debug"]["candidate_off"]]
fd_off_shadow = [r for r in fd_rows if r["fast_debug"]["candidate_off"] != r["fast_debug"]["candidate_shadow"]]
rust_rows = [r for r in rows if "rust" in r]
rust_equality = {}
for relationship, left, right in (
    ("baseline_vs_candidate_off", "baseline", "candidate_off"),
    ("candidate_off_vs_candidate_shadow", "candidate_off", "candidate_shadow"),
    ("baseline_vs_candidate_shadow", "baseline", "candidate_shadow")):
    diff_sources=[]; hash_diff={"default": [], "optimized": []}; behavior_diff=[]; failure_diff=[]
    for row in rust_rows:
        for profile in ("default", "optimized"):
            a=row["rust"][left+"_"+profile]; b=row["rust"][right+"_"+profile]
            if a.get("rust_sha256") != b.get("rust_sha256"):
                hash_diff[profile].append(row["source"])
            if normalized_behavior(a.get("behavior")) != normalized_behavior(b.get("behavior")):
                behavior_diff.append(row["source"])
            if ("emit" in a) != ("emit" in b) or ("rustc" in a) != ("rustc" in b):
                failure_diff.append(row["source"])
        if row["source"] in hash_diff["default"] or row["source"] in hash_diff["optimized"] or row["source"] in behavior_diff or row["source"] in failure_diff:
            diff_sources.append(row["source"])
    rust_equality[relationship] = {"default_rust_hash_differences": hash_diff["default"],
        "optimized_rust_hash_differences": hash_diff["optimized"],
        "normalized_native_behavior_differences": sorted(set(behavior_diff)),
        "emit_or_rustc_status_differences": sorted(set(failure_diff)),
        "union_difference_sources": sorted(set(diff_sources))}

report={"row_count":len(rows),"candidate_source_count":2138,"baseline_source_count":2118,
 "candidate_baseline_off_diagnostic_differences":[r["source"] for r in check_base_off],
 "candidate_off_shadow_diagnostic_differences":[r["source"] for r in check_off_shadow],
 "candidate_shadow_sig_or_timeout_cases":[{"source":r["source"],"code":r["candidate_shadow_check"]["process"]["code"],"stderr":r["candidate_shadow_check"]["process"]["stderr"]} for r in shadow_crashes],
 "fast_debug_cases":len(fd_rows),"fast_debug_timeout_cases":[r["source"] for r in fd_timeout],
 "fast_debug_baseline_candidate_off_differences":[r["source"] for r in fd_base_off],
 "fast_debug_candidate_off_shadow_differences":[r["source"] for r in fd_off_shadow],
 "native_profile_cases":len(rust_rows),"rust_relationships":rust_equality}
(ROOT/"docs/phase27/adversarial/CP_F_PHASE27_3_DIFFERENTIAL_ANALYSIS.json").write_text(json.dumps(report,indent=2,sort_keys=True)+"\n",encoding="utf-8")
print(json.dumps({"rows":len(rows),"baseline_vs_off_diagnostic_diff":len(check_base_off),"off_vs_shadow_diagnostic_diff":len(check_off_shadow),"shadow_sig_timeout":len(shadow_crashes),"fast_debug_rows":len(fd_rows),"fast_debug_timeout":len(fd_timeout),"fast_debug_base_off_diff":len(fd_base_off),"fast_debug_off_shadow_diff":len(fd_off_shadow),"native_rows":len(rust_rows),"rust_relationships":{k:{"default_hash_diffs":len(v["default_rust_hash_differences"]),"optimized_hash_diffs":len(v["optimized_rust_hash_differences"]),"behavior_diffs":len(v["normalized_native_behavior_differences"]),"compiler_status_diffs":len(v["emit_or_rustc_status_differences"])} for k,v in rust_equality.items()}},indent=2))
