#!/usr/bin/env python3
"""CP-D output and generated-code comparison against phase27-1-m1."""
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
BASELINE_SHA256 = "4a9d7d7509f9883194f2d3f147c4ff66c39e9c832e758ddffdd7572d57667ffd"
BASELINE = Path(sys.argv[1]).resolve()
CANDIDATE = Path(sys.argv[2]).resolve()
WORK = ROOT / "tmp/phase27-2/cp-d-f38147ee"
REPORT = WORK / "report.json"
TIMEOUT = 30
ADJUDICATED_DEFERRED_PIPELINE_CORRECTIONS = {
    "examples/swarm/expression_surface_2026/validation_engine/failed_attempts/04_pipeline_collection_inference.moss",
    "examples/swarm/modules/domain_services/repro/generic_pipeline/single/src/main.moss",
    "examples/swarm/modules/domain_services/repro/generic_pipeline/split/src/main.moss",
    "examples/swarm/modules/domain_services/repro/generic_pipeline/split/src/stats.moss",
    "examples/swarm/modules/lib_and_app/repro/generic_pipeline_method_placeholder/single/src/main.moss",
}


def invoke(command, cwd, env):
    try:
        result = subprocess.run(command, cwd=cwd, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=TIMEOUT)
        return {"code": result.returncode, "stdout": result.stdout,
                "stderr": result.stderr}
    except subprocess.TimeoutExpired as error:
        def decoded(value):
            if value is None:
                return ""
            return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else value
        return {"code": "TIMEOUT", "stdout": decoded(error.stdout),
                "stderr": decoded(error.stderr)}


def check(compiler, source, env):
    result = invoke([str(compiler), "check", str(source), "--json"],
                    ROOT, env)
    try:
        payload = json.loads(result["stdout"])
    except (json.JSONDecodeError, TypeError):
        return result["code"], "NOJSON"
    if payload.get("ok"):
        return result["code"], "ok"
    error = payload.get("error") or {}
    return result["code"], f"{error.get('code')}:{error.get('line')}"


def compile_hash(compiler, source, mode, work, env):
    rust = work / f"{mode}.rs"
    command = [str(compiler)]
    if mode == "optimized":
        command.append("-O")
    command.extend([str(source), "-o", str(rust)])
    emitted = invoke(command, ROOT, env)
    if emitted["code"] != 0 or not rust.exists():
        return {"error": f"emit:{emitted['code']}",
                "stderr": emitted["stderr"][-2000:]}
    return {"sha256": hashlib.sha256(rust.read_bytes()).hexdigest()}


def source_case(relative):
    source = ROOT / relative
    text = source.read_text(encoding="utf-8", errors="replace")
    has_main = re.search(r"(?m)^\s*fn\s+main\s*\(", text) is not None
    env_base = os.environ.copy()
    env_base["MOSS_EST"] = "off"
    env_shadow = os.environ.copy()
    env_shadow["MOSS_EST"] = "shadow"
    base_code, base_verdict = check(BASELINE, source, env_base)
    candidate_off_code, candidate_off_verdict = check(CANDIDATE, source, env_base)
    candidate_shadow_code, candidate_shadow_verdict = check(CANDIDATE, source, env_shadow)
    record = {"source": relative, "verdicts": {
        "baseline": base_verdict, "candidate_off": candidate_off_verdict,
        "candidate_shadow": candidate_shadow_verdict},
        "verdict_exit_codes": {"baseline": base_code,
                               "candidate_off": candidate_off_code,
                               "candidate_shadow": candidate_shadow_code}}
    if base_verdict != candidate_off_verdict or base_verdict != candidate_shadow_verdict:
        record["verdict_difference"] = True
    if base_verdict != "ok" or candidate_off_verdict != "ok" or \
            candidate_shadow_verdict != "ok" or not has_main:
        return record

    record["fast_debug"] = {}
    for label, compiler, env in (
            ("baseline", BASELINE, env_base),
            ("candidate_off", CANDIDATE, env_base),
            ("candidate_shadow", CANDIDATE, env_shadow)):
        record["fast_debug"][label] = invoke(
            [str(compiler), "run", "--interp", str(source)], ROOT, env)
    record["fast_debug_equal"] = len({
        (json.dumps(value, sort_keys=True))
        for value in record["fast_debug"].values()}) == 1

    with tempfile.TemporaryDirectory(prefix="source-", dir=WORK) as temporary:
        work = Path(temporary)
        record["generated_rust"] = {}
        for mode in ("default", "optimized"):
            record["generated_rust"][mode] = {
                "baseline": compile_hash(BASELINE, source, mode, work, env_base),
                "candidate": compile_hash(CANDIDATE, source, mode, work, env_base),
            }
        record["generated_rust_equal"] = all(
            values["baseline"] == values["candidate"]
            for values in record["generated_rust"].values())
    return record


def main():
    baseline_sha = hashlib.sha256(BASELINE.read_bytes()).hexdigest()
    if baseline_sha != BASELINE_SHA256:
        raise SystemExit(f"baseline SHA mismatch: {baseline_sha}")
    WORK.mkdir(parents=True, exist_ok=True)
    raw = subprocess.run(["git", "ls-files", "examples/**/*.moss", "examples/*.moss"],
                         cwd=ROOT, text=True, capture_output=True, check=True).stdout
    sources = sorted(set(raw.splitlines()))
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(8, os.cpu_count() or 4)) as pool:
        records = list(pool.map(source_case, sources))
    differences = [record for record in records if
                   record.get("verdict_difference") or
                   ("fast_debug_equal" in record and not record["fast_debug_equal"]) or
                   ("generated_rust_equal" in record and not record["generated_rust_equal"])]
    adjudicated = [record for record in differences
                   if record.get("source") in ADJUDICATED_DEFERRED_PIPELINE_CORRECTIONS and
                   record.get("verdicts", {}).get("baseline") == "NOJSON" and
                   record.get("verdicts", {}).get("candidate_off") == "ok" and
                   record.get("verdicts", {}).get("candidate_shadow") == "ok" and
                   "fast_debug_equal" not in record]
    unadjudicated = [record for record in differences if record not in adjudicated]
    report = {"baseline_sha256": baseline_sha, "candidate_sha256":
              hashlib.sha256(CANDIDATE.read_bytes()).hexdigest(),
              "sources": len(records),
              "accepted_main_sources": sum("fast_debug" in row for row in records),
              "verdict_differences": sum(bool(row.get("verdict_difference")) for row in records),
              "fast_debug_differences": sum(
                  "fast_debug_equal" in row and not row["fast_debug_equal"]
                  for row in records),
              "default_or_optimized_generated_rust_differences": sum(
                  "generated_rust_equal" in row and not row["generated_rust_equal"]
                  for row in records),
              "differences": differences,
              "adjudicated_deferred_pipeline_corrections": [
                  row["source"] for row in adjudicated],
              "unadjudicated_differences": unadjudicated,
              "entries": records}
    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in report.items()
                      if key not in ("entries", "differences")}, indent=2))
    print(f"report: {REPORT.relative_to(ROOT)}")
    return 1 if unadjudicated else 0


if __name__ == "__main__":
    sys.exit(main())
