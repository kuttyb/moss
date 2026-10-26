#!/usr/bin/env python3
"""Exercise Phase 26 W2 signature worklist behavior and counters."""

import json
import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
W5 = ROOT / "benchmarks/compiler_torture/fixtures/inference/w5_infer_wide_w100.moss"
FORWARD = ROOT / "tests/phase26_w2_forward_inference.moss"
DOMAIN_ONLY = ROOT / "tests/phase26_w2_domain_only.moss"
PIPELINE = ROOT / "tests/phase26_w2_pipeline_callback.moss"
NEGATIVE = ROOT / "tests/negative/phase26_w2_bad_signature.moss"
REGEX = ROOT / "packages/regex/src/main.moss"
DATASTRUCTURES = ROOT / "sdk/datastructures/src/datastructures.moss"
D18 = ROOT / "benchmarks/compiler_torture/fixtures/inference/w4_infer_deep_d018.moss"


def invoke(command, *, env=None):
    return subprocess.run(command, cwd=ROOT, env=env, capture_output=True,
                          text=True, check=False)


def checked(compiler: Path, source: Path, *, profiled=False):
    env = dict(os.environ)
    if profiled:
        env["MOSS_PROFILE_COMPILER"] = "1"
    else:
        env.pop("MOSS_PROFILE_COMPILER", None)
    result = invoke([str(compiler), "check", str(source), "--json"], env=env)
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise AssertionError(f"check produced invalid JSON: {result.stdout!r} {result.stderr!r}") from exc
    return result, payload


def profile_map(result):
    profile = {}
    for line in result.stderr.splitlines():
        if not line.startswith("MOSS_PROFILE|"):
            continue
        parts = line.split("|")
        if len(parts) == 3 and parts[2].isdigit():
            profile[parts[1]] = int(parts[2])
    return profile


def check_project_wave_counts(compiler, source):
    result, payload = checked(compiler, source, profiled=True)
    if result.returncode != 0 or payload.get("ok") is not True:
        raise AssertionError(
            f"project rejected: {source}: {result.stdout!r} {result.stderr!r}"
        )
    checker_profiles = []
    for line in result.stderr.splitlines():
        if not line.startswith("MOSS_PROFILE|"):
            continue
        parts = line.split("|")
        if len(parts) != 3 or not parts[2].isdigit():
            continue
        if parts[1] == "functions_analyzed":
            checker_profiles.append({})
        if checker_profiles:
            checker_profiles[-1].setdefault(parts[1], []).append(int(parts[2]))
    if len(checker_profiles) != 1:
        raise AssertionError(
            f"expected one checker for the exact single-source project check of "
            f"{source}, got {len(checker_profiles)}"
        )
    checker_summaries = []
    for checker_index, records in enumerate(checker_profiles):
        waves = {}
        for key, values in records.items():
            match = re.fullmatch(
                r"inference_signature_invocation_(\d+)_wave_(\d+)_visited", key
            )
            if not match:
                continue
            invocation, wave = map(int, match.groups())
            changed_key = (
                f"inference_signature_invocation_{invocation}_wave_{wave}_changed"
            )
            if changed_key not in records or len(records[changed_key]) != len(values):
                raise AssertionError(f"missing changed count for {key}")
            for visit_count, changed_count in zip(values, records[changed_key]):
                waves.setdefault(invocation, []).append(
                    (wave, visit_count, changed_count)
                )
        if len(waves) < 2 or any(not counts for counts in waves.values()):
            raise AssertionError(
                f"per-invocation wave counts missing for {source}, checker {checker_index}"
            )
        checker_summaries.append(
            f"checker {checker_index}: " + "; ".join(
                f"pass {invocation}: " + ", ".join(
                    f"wave {wave}={visited} visits/{changed} changed"
                    for wave, visited, changed in sorted(counts)
                )
                for invocation, counts in sorted(waves.items())
            )
        )
    print(f"{source.relative_to(ROOT)}: " + " | ".join(checker_summaries))


def check_d18_effect_cache(compiler):
    result, payload = checked(compiler, D18, profiled=True)
    if result.returncode != 0 or payload.get("ok") is not True:
        raise AssertionError(f"W4 D18 rejected: {result.stdout!r} {result.stderr!r}")
    profile = profile_map(result)
    calls = profile.get("specialization_effect_function_evaluations", 0)
    hits = profile.get("specialization_effect_cache_hits", 0)
    computations = profile.get("specialization_effect_computations", 0)
    keys = profile.get("specialization_effect_distinct_inputs", 0)
    rounds = profile.get("effects_fixed_point_rounds", 0)
    limit = profile.get("effects_fixed_point_limit", 0)
    if not calls or not keys or not hits or hits >= calls or computations != calls - hits:
        raise AssertionError(
        f"D18 effects memo counters invalid: calls={calls}, computations={computations}, "
        f"hits={hits}, keys={keys}"
        )
    if computations != keys * 2:
        raise AssertionError(
            "D18 should compute each specialization effect input once per "
            f"independent pass: computations={computations}, keys={keys}"
        )
    if rounds <= 0 or limit <= rounds:
        raise AssertionError(
            f"D18 effects fixed-point counters invalid: rounds={rounds}, limit={limit}"
        )
    print(
        f"W4 D18: specialization effects calls={calls}, computations={computations}, "
        f"cache hits={hits}, "
        f"distinct inputs={keys}; effects fixed-point rounds={rounds}/{limit}"
    )


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_phase26_w2_inference_worklist.py COMPILER")
    compiler = Path(sys.argv[1]).resolve()

    result, payload = checked(compiler, W5, profiled=True)
    if result.returncode != 0 or payload.get("ok") is not True:
        raise AssertionError(f"W5 rejected: {result.stdout!r} {result.stderr!r}")
    profile = {}
    for line in result.stderr.splitlines():
        if not line.startswith("MOSS_PROFILE|"):
            continue
        parts = line.split("|")
        if len(parts) == 3 and parts[2].isdigit():
            profile[parts[1]] = int(parts[2])
    function_count = profile.get("functions_analyzed", 0)
    if function_count != 200:
        raise AssertionError(f"W5 should analyze 200 functions, got {function_count}")
    if profile.get("inference_rounds") != 5:
        raise AssertionError("the existing inference_rounds meaning/count changed")
    visited_by_round = {
        key: value for key, value in profile.items()
        if re.fullmatch(r"inference_reinferred_functions_round_\d+", key)
    }
    changed_by_round = {
        key: value for key, value in profile.items()
        if re.fullmatch(r"inference_changed_functions_round_\d+", key)
    }
    visited_rounds = {key.rsplit("_", 1)[1] for key in visited_by_round}
    changed_rounds = {key.rsplit("_", 1)[1] for key in changed_by_round}
    if not visited_by_round or visited_rounds != changed_rounds:
        raise AssertionError("per-wave visited/changed function counters are incomplete")
    if sum(visited_by_round.values()) != profile.get("inference_reinferred_functions"):
        raise AssertionError("aggregate re-inference counter disagrees with per-wave counts")
    if sum(changed_by_round.values()) != profile.get("inference_changed_functions"):
        raise AssertionError("aggregate changed-signature counter disagrees with per-wave counts")
    if profile["inference_reinferred_functions"] > function_count * 8:
        raise AssertionError("W5 still re-infers too many functions across worklist waves")

    check_project_wave_counts(compiler, REGEX)
    check_project_wave_counts(compiler, DATASTRUCTURES)
    check_d18_effect_cache(compiler)

    result = invoke([str(compiler), "run", "--interp", str(W5)])
    if result.returncode != 0 or result.stdout.strip() != "5250":
        raise AssertionError(f"W5 interpreter output changed: {result.stdout!r} {result.stderr!r}")

    result, payload = checked(compiler, FORWARD)
    if result.returncode != 0 or payload.get("ok") is not True:
        raise AssertionError(f"forward call rejected: {result.stdout!r} {result.stderr!r}")
    result = invoke([str(compiler), "run", "--interp", str(FORWARD)])
    if result.returncode != 0 or result.stdout.strip() != "4":
        raise AssertionError(f"forward-call result changed: {result.stdout!r} {result.stderr!r}")

    result, payload = checked(compiler, DOMAIN_ONLY, profiled=True)
    if result.returncode != 0 or payload.get("ok") is not True:
        raise AssertionError(f"domain-only inference rejected: {result.stdout!r} {result.stderr!r}")
    domain_wave_count = sum(
        1 for line in result.stderr.splitlines()
        if re.fullmatch(
            r"MOSS_PROFILE\|inference_reinferred_functions_round_\d+\|\d+",
            line,
        )
    )
    try:
        domain_profile = {
            parts[1]: int(parts[2])
            for line in result.stderr.splitlines()
            if len(parts := line.split("|")) == 3 and
            parts[0] == "MOSS_PROFILE" and parts[2].isdigit()
        }
    except ValueError as exc:
        raise AssertionError("domain-only profile contained an invalid counter") from exc
    if domain_profile.get("functions_analyzed") != 0:
        raise AssertionError(
            "domain-only fixture must exercise inference with zero analyzed functions"
        )
    if domain_wave_count <= domain_profile.get("inference_rounds", 0):
        raise AssertionError("domain-only shared signature changes did not schedule another wave")
    result = invoke([str(compiler), "run", "--interp", str(DOMAIN_ONLY)])
    if result.returncode != 0 or result.stdout.strip() != "7":
        raise AssertionError(f"domain-only handler result changed: {result.stdout!r} {result.stderr!r}")

    result, payload = checked(compiler, PIPELINE)
    if result.returncode != 0 or payload.get("ok") is not True:
        raise AssertionError(f"pipeline callback rejected: {result.stdout!r} {result.stderr!r}")
    result = invoke([str(compiler), "run", "--interp", str(PIPELINE)])
    if result.returncode != 0 or result.stdout.strip() != "[2, 3, 4]":
        raise AssertionError(f"pipeline callback result changed: {result.stdout!r} {result.stderr!r}")

    result, payload = checked(compiler, NEGATIVE)
    error = payload.get("error") or {}
    if result.returncode == 0 or payload.get("ok") is not False or \
            error.get("code") != "MOSS_COMPILE_ERROR" or error.get("line") != 2:
        raise AssertionError(f"negative signature diagnostic changed: {result.stdout!r} {result.stderr!r}")

    print("W2 inference worklist checks passed: W5 output/counters, forward call, domain-only inference, pipeline callback, negative diagnostic")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
