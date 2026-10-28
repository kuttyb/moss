#!/usr/bin/env python3
"""Compare default and verification-mode results over compiler-perf inputs."""

import json
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
TORTURE_ROOT = ROOT / "benchmarks/compiler_torture"


def perf_inputs():
    torture = json.loads(
        (TORTURE_ROOT / "manifest.json").read_text(encoding="utf-8")
    )["fixtures"]
    real_world = json.loads(
        (ROOT / "benchmarks/real_world/manifest.json").read_text(encoding="utf-8")
    )["workloads"]
    inputs = []
    for fixture in torture:
        target = TORTURE_ROOT / fixture["path"]
        if fixture.get("is_project"):
            target = target / "src/main.moss"
        inputs.append((fixture["id"], target))
    for workload in real_world:
        inputs.append((workload["id"], ROOT / workload["entry_file"]))
    return inputs


def check(compiler: Path, source: Path, *, verified: bool):
    env = dict(os.environ)
    for name in (
        "MOSS_PROFILE_COMPILER", "MOSS_LEGACY_INFERENCE_ROUNDS",
        "MOSS_VERIFY_INFERENCE_ROUNDS", "MOSS_VERIFY_SIGNATURE_WORKLIST",
    ):
        env.pop(name, None)
    if verified:
        env["MOSS_VERIFY_INFERENCE_ROUNDS"] = "1"
        env["MOSS_VERIFY_SIGNATURE_WORKLIST"] = "1"
    result = subprocess.run(
        [str(compiler), "check", str(source), "--json"], cwd=ROOT, env=env,
        capture_output=True, text=True, timeout=120,
    )
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise AssertionError(
            f"{source}: invalid JSON ({result.returncode}): "
            f"{result.stdout!r} {result.stderr!r}"
        ) from exc
    if "changed signatures after convergence" in result.stderr or \
            "MOSS_VERIFY_SIGNATURE_WORKLIST mismatch" in result.stderr:
        raise AssertionError(f"{source}: verifier logic error: {result.stderr}")
    return result.returncode, payload


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_phase26_inference_verify_modes.py COMPILER")
    compiler = Path(sys.argv[1]).resolve()
    fixtures = perf_inputs()
    for fixture_id, source in fixtures:
        default = check(compiler, source, verified=False)
        verified = check(compiler, source, verified=True)
        if default != verified:
            raise AssertionError(
                f"{fixture_id}: default and verified results differ:\n"
                f"default={default!r}\nverified={verified!r}"
            )
    print(
        "Phase 26 inference verification modes match default check --json "
        f"for all {len(fixtures)} compiler-perf inputs"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
