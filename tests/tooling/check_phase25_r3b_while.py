#!/usr/bin/env python3
"""Source-free mutable-local while projection and hostile ABI-v10 imports."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)
MOSS = Path(os.environ.get("MOSS", ROOT / "moss")).resolve()
MARGO = ROOT / "margo"
PROVIDER = """module Service

export fn count_up(value) -> Int:
  let target = value
  var count = 0
  while count < target:
    count = count + 1
  return count
"""
CONSUMER = """module Main
import Service

fn main():
  echo Service.count_up(0)
  echo Service.count_up(4)
  echo Service.count_up(2.5)
"""
EXPECTED = "0\n4\n3\n"


def run(argv, *, cwd, env, expected=0):
    result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True,
                           text=True, timeout=300)
    output = result.stdout + result.stderr
    assert result.returncode == expected, (argv, result.returncode, output)
    return output


with tempfile.TemporaryDirectory(prefix="phase25-r3b-while-", dir=TMP) as name:
    scratch = Path(name)
    base_env = os.environ.copy()
    base_env.update(MOSS_STAGED_ABI="v10", MOSS=str(MOSS), TMPDIR=str(TMP))
    provider = scratch / "provider"
    (provider / "src").mkdir(parents=True)
    (provider / "Moss.toml").write_text(
        '[package]\nname = "r3b_while_provider"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    (provider / "src/service.moss").write_text(PROVIDER)
    run([str(MARGO), "build"], cwd=provider, env=base_env)
    artifacts = scratch / "provider-artifacts"
    shutil.copytree(provider / "build" / "debug", artifacts)

    poisoner = scratch / "poison_generic_callable"
    mutator = scratch / "mutate_r3b_numeric"
    for source, output in (
        ("tests/tooling/poison_phase25_generic_callable.cpp", poisoner),
        ("tests/tooling/mutate_phase25_r3b_numeric_artifact.cpp", mutator),
    ):
        run(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
             "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
             source, "-o", str(output)], cwd=ROOT, env=base_env)
    run([str(poisoner), str(artifacts / "Service.mossi"),
         str(artifacts / "libService.rlib"), "Service", "count_up"],
        cwd=ROOT, env=base_env)
    shutil.rmtree(provider / "src")
    assert not (provider / "src").exists()

    consumer = scratch / "consumer"
    (consumer / "src").mkdir(parents=True)
    (consumer / "Moss.toml").write_text(
        '[package]\nname = "r3b_while_consumer"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    source_path = consumer / "src/main.moss"
    source_path.write_text(CONSUMER)
    env = base_env | {
        "MOSS_MODULE_PATH": str(artifacts),
        "MARGO_HOME": str(scratch / "margo-consumer"),
    }
    run([str(MARGO), "build"], cwd=consumer, env=env)
    trace_env = env | {"MOSS_PHASE25_CHECKED_BODY_TRACE": "1"}
    check = run([str(MOSS), "check", str(source_path), "--json"],
                cwd=consumer, env=trace_env)
    assert "Service:fn:count_up|checked-body-v1" in check, check
    output = run([str(MARGO), "run"], cwd=consumer, env=env)
    assert output == EXPECTED, output

    good_consumer = source_path.read_text()
    source_path.write_text("""module Main
import Service

fn main():
  echo Service.count_up("not numeric")
""")
    bad_result = subprocess.run([str(MOSS), "check", str(source_path), "--json"],
                                cwd=consumer, env=trace_env,
                                capture_output=True, text=True, timeout=300)
    diagnostic = bad_result.stdout + bad_result.stderr
    assert bad_result.returncode != 0 and "TYPE_MISMATCH" in diagnostic, diagnostic
    source_path.write_text(good_consumer)

    for mode in ("while-condition", "while-assignment-target",
                 "while-requirement-result"):
        mutated = scratch / f"artifacts-{mode}"
        shutil.copytree(artifacts, mutated)
        run([str(mutator), str(mutated / "Service.mossi"),
             str(mutated / "libService.rlib"), mode, "Service"],
            cwd=ROOT, env=base_env)
        bad_env = trace_env | {
            "MOSS_MODULE_PATH": str(mutated),
            "MARGO_HOME": str(scratch / f"margo-{mode}"),
        }
        result = subprocess.run([str(MOSS), "check", str(source_path), "--json"],
                                cwd=consumer, env=bad_env, capture_output=True,
                                text=True, timeout=300)
        diagnostic = result.stdout + result.stderr
        assert result.returncode != 0 and "SOURCE_FREE_" in diagnostic, (
            mode, diagnostic)
        assert "Service:fn:count_up|checked-body-v1" not in diagnostic, (
            mode, "forged while body projected before rejection", diagnostic)
        print(f"PASS rehashed {mode} mutation rejected before projection")

print("R3B mutable-local while source-free positive and hostile cases passed")
