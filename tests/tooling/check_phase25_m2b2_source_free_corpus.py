#!/usr/bin/env python3
"""Run the prioritized ABI-v10 source-free generic acceptance corpus.

Legacy-dependent rows prove their current source-free behavior and then poison
generic_stmt. Checked-body-supported rows must keep their result after that
poison and report checked-body projection. Existing suites provide additional
positive and record-corruption gates.
"""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "tests/phase25/source_free_corpus/corpus.json"
TMP = ROOT / "tmp"
TMP.mkdir(exist_ok=True)
parser = argparse.ArgumentParser()
parser.add_argument("moss", nargs="?", default=str(ROOT / "moss"))
parser.add_argument("--inventory-only", action="store_true",
                    help="validate and print corpus inventory without builds")
args = parser.parse_args()
MOSS = Path(args.moss).resolve()
MARGO = ROOT / "margo"
corpus = json.loads(MANIFEST.read_text())


def command(argv, *, cwd, env, expected=0):
    result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True,
                            text=True, timeout=240)
    output = result.stdout + result.stderr
    assert result.returncode == expected, (
        f"command {argv!r} returned {result.returncode}, expected {expected}:\n"
        f"{output}")
    return output


def package(path, name, source):
    (path / "src").mkdir(parents=True)
    (path / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n')
    (path / "src/main.moss").write_text(source)


if args.inventory_only:
    for case in corpus["cases"]:
        print(f"{case['priority']}: {case['id']} — {case['category']}")
    for case in corpus["already-covered"]:
        print(f"covered: {case['id']} — {case['test']}")
    raise SystemExit(0)


with tempfile.TemporaryDirectory(prefix="phase25-m2b2-corpus-", dir=TMP) as temp:
    scratch = Path(temp)
    poisoner = scratch / "poison_legacy"
    command(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
             "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
             "tests/tooling/poison_phase25_generic_callable.cpp", "-o",
             str(poisoner)], cwd=ROOT, env=os.environ.copy())
    parameter_mutator = scratch / "mutate_parameter_return_artifact"
    command(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
             "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
             "tests/tooling/mutate_phase25_parameter_return_artifact.cpp", "-o",
             str(parameter_mutator)], cwd=ROOT, env=os.environ.copy())

    base_env = os.environ.copy()
    base_env.update(MOSS_STAGED_ABI="v10", MOSS=str(MOSS), TMPDIR=str(TMP))
    for case in corpus["cases"]:
        case_root = scratch / case["id"]
        provider = case_root / "provider"
        package(provider, f"m2b2_{case['id'].replace('-', '_')}_provider",
                case["provider"])
        provider_env = base_env | {"MARGO_HOME": str(case_root / "margo-provider")}
        provider_build = subprocess.run(
            [str(MARGO), "build"], cwd=provider, env=provider_env,
            capture_output=True, text=True, timeout=240)
        provider_output = provider_build.stdout + provider_build.stderr
        if case.get("provider_expected_error"):
            assert provider_build.returncode != 0 and (
                case["provider_expected_error"] in provider_output), (
                    case["id"], case["provider_expected_error"], provider_output)
            assert not list((provider / "build").rglob("Service.mossi")), (
                case["id"], "provider left a usable ABI-v10 interface behind")
            print(f"PASS {case['id']}: staged provider rejects unsupported body")
            continue
        assert provider_build.returncode == 0, (
            case["id"], provider_build.returncode, provider_output)
        artifacts = case_root / "artifacts"
        shutil.copytree(provider / "build/debug", artifacts)
        interface = artifacts / "Service.mossi"
        assert b"generic_stmt " in interface.read_bytes(), (
            case["id"], "uncovered case stopped emitting its legacy body")
        shutil.rmtree(provider / "src")
        assert not (provider / "src").exists()

        consumer = case_root / "consumer"
        package(consumer, f"m2b2_{case['id'].replace('-', '_')}_consumer",
                case["consumer"])
        env = base_env | {
            "MOSS_MODULE_PATH": str(artifacts),
            "MARGO_HOME": str(case_root / "margo-consumer"),
        }
        command([str(MARGO), "build"], cwd=consumer, env=env)
        output = command([str(MARGO), "run"], cwd=consumer, env=env)
        assert output == case["expected_output"], (case["id"], output)
        if case["checked_body_status"].startswith("checked-body supported"):
            trace_env = env | {"MOSS_PHASE25_CHECKED_BODY_TRACE": "1"}
            trace = "MOSS_CHECKED_BODY_PROJECTED|callable-v1:"
            check_output = command(
                [str(MOSS), "check", str(consumer / "src/main.moss"), "--json"],
                cwd=consumer, env=trace_env)
            assert (trace in check_output and
                    ":fn:fresh|checked-body-v1" in check_output), (
                        case["id"], "checked-body projection trace missing",
                        check_output)
            print(f"PASS {case['id']}: source-free checked-body baseline executes")

            poisoned = case_root / "poisoned-artifacts"
            shutil.copytree(artifacts, poisoned)
            poison_targets = (("fresh", "helper")
                              if case["id"] == "single-call" else ("fresh",))
            poison_outputs = []
            for callable_name in poison_targets:
                poison_output = command(
                    [str(poisoner), str(poisoned / "Service.mossi"),
                     str(poisoned / "libService.rlib"), "Service",
                     callable_name], cwd=ROOT, env=base_env)
                prefix = f"removed "
                suffix = f" generic_stmt records from {callable_name}"
                assert prefix in poison_output and suffix in poison_output, poison_output
                removed = int(poison_output.split(prefix, 1)[1].split(suffix, 1)[0])
                assert removed > 0, poison_output
                poison_outputs.append(poison_output)
            if case["id"] == "collection-loop-equality-index":
                assert b"generic_stmt " not in (
                    poisoned / "Service.mossi").read_bytes(), (
                        "collection-loop still contains a legacy body after poisoning")
            if case["id"] == "single-call":
                assert b"generic_stmt " not in (
                    poisoned / "Service.mossi").read_bytes(), (
                        "single-call still contains a legacy body after "
                        "poisoning fresh and helper")
            poisoned_env = trace_env | {
                "MOSS_MODULE_PATH": str(poisoned),
                "MARGO_HOME": str(case_root / "margo-poisoned"),
            }
            poisoned_build = command(
                [str(MARGO), "build"], cwd=consumer, env=poisoned_env)
            poisoned_check = command(
                [str(MOSS), "check", str(consumer / "src/main.moss"), "--json"],
                cwd=consumer, env=poisoned_env)
            assert (trace in poisoned_build + poisoned_check and
                    ":fn:fresh|checked-body-v1" in poisoned_build + poisoned_check), (
                        case["id"], "poisoned import did not project checked body",
                        poisoned_build + poisoned_check)
            if case["id"] == "single-call":
                assert ":fn:helper|checked-body-v1" in poisoned_build + poisoned_check, (
                    case["id"], "private helper projection trace missing",
                    poisoned_build + poisoned_check)
            poisoned_output = command(
                [str(MARGO), "run"], cwd=consumer, env=poisoned_env)
            assert poisoned_output == case["expected_output"], (
                case["id"], poisoned_output)
            removed_report = "; ".join(output.strip() for output in poison_outputs)
            print(f"PASS {case['id']}: {removed_report}; checked-body result unchanged")

            negative_cases = {
                "missing-summary": ("SOURCE_FREE_SUMMARY_INVALID",),
                "missing-body": ("SOURCE_FREE_CHECKED_BODY_INVALID",),
                "corrupted-summary": ("SOURCE_FREE_CHECKED_BODY_INVALID",),
                "identity": ("SOURCE_FREE_",),
                "mismatch-hash": ("SOURCE_FREE_CHECKED_BODY_INVALID",
                                  "SOURCE_FREE_SUMMARY_INVALID"),
                "missing-provenance": ("SOURCE_FREE_SUMMARY_INVALID",
                                       "SOURCE_FREE_CONTRACT_INVALID"),
                "forged-provenance": ("SOURCE_FREE_SUMMARY_INVALID",),
            }
            if case["id"] == "single-call":
                negative_cases.update({
                    "missing-callee-summary": ("SOURCE_FREE_PRIVATE_DEPENDENCY_MISSING",
                                               "SOURCE_FREE_SUMMARY_INVALID"),
                    "missing-callee-body": ("SOURCE_FREE_PRIVATE_DEPENDENCY_MISSING",
                                            "SOURCE_FREE_CHECKED_BODY_INVALID"),
                    "wrong-callable": ("SOURCE_FREE_CHECKED_BODY_INVALID",),
                    "missing-dependency": ("SOURCE_FREE_CHECKED_BODY_INVALID",
                                           "SOURCE_FREE_PRIVATE_DEPENDENCY_MISSING"),
                    "dependency-hash": ("SOURCE_FREE_CHECKED_BODY_INVALID",
                                        "SOURCE_FREE_SUMMARY_INVALID",
                                        "SOURCE_FREE_CONTRACT_INVALID"),
                    "wrong-type": ("SOURCE_FREE_CHECKED_BODY_INVALID",
                                   "SOURCE_FREE_SUMMARY_INVALID"),
                    "wrong-argument-type": ("SOURCE_FREE_CHECKED_BODY_INVALID",
                                             "SOURCE_FREE_SUMMARY_INVALID"),
                    "unsupported-arity": ("SOURCE_FREE_CHECKED_BODY_INVALID",),
                    "unsupported-shape": ("SOURCE_FREE_CHECKED_BODY_INVALID",
                                          "SOURCE_FREE_PRIVATE_DEPENDENCY_MISSING"),
                    "duplicate-record": ("SOURCE_FREE_CHECKED_BODY_INVALID",
                                         "SOURCE_FREE_SUMMARY_INVALID",
                                         "SOURCE_FREE_CONTRACT_INVALID"),
                })
            if case["id"] == "parameter-return":
                negative_cases["type-binding"] = (
                    "SOURCE_FREE_CHECKED_BODY_INVALID",)
            if case["id"] == "field-read":
                negative_cases.update({
                    "bad-field-identity": ("SOURCE_FREE_CHECKED_BODY_INVALID",),
                    "bad-field-ordinal": ("SOURCE_FREE_CHECKED_BODY_INVALID",),
                    "bad-field-type": ("SOURCE_FREE_CHECKED_BODY_INVALID",),
                    "missing-dependency": ("SOURCE_FREE_CHECKED_BODY_INVALID",),
                })
            for mode, expected_codes in negative_cases.items():
                mutated = case_root / f"checked-{mode}"
                shutil.copytree(poisoned, mutated)
                command([str(parameter_mutator), str(mutated / "Service.mossi"),
                         str(mutated / "libService.rlib"), mode,
                         "fresh"],
                        cwd=ROOT, env=base_env)
                bad_consumer = case_root / f"consumer-{mode}"
                package(bad_consumer,
                        f"m2b3b_{case['id'].replace('-', '_')}_{mode.replace('-', '_')}_consumer",
                        case["consumer"])
                bad_env = poisoned_env | {
                    "MOSS_MODULE_PATH": str(mutated),
                    "MARGO_HOME": str(case_root / f"margo-{mode}"),
                }
                rejected = subprocess.run(
                    [str(MARGO), "build"], cwd=bad_consumer, env=bad_env,
                    capture_output=True, text=True, timeout=240)
                reject_output = rejected.stdout + rejected.stderr
                assert rejected.returncode != 0 and any(
                    code in reject_output for code in expected_codes), (
                        mode, expected_codes, reject_output)
                print(f"PASS {case['id']} {mode} rejects fail-closed")

            v9_env = base_env.copy()
            v9_env.pop("MOSS_STAGED_ABI", None)
            v9_provider = case_root / "provider-v9"
            package(v9_provider, "m2b3a_parameter_return_provider_v9",
                    case["provider"])
            v9_env["MARGO_HOME"] = str(case_root / "margo-provider-v9")
            command([str(MARGO), "build"], cwd=v9_provider, env=v9_env)
            v9_artifacts = case_root / "artifacts-v9"
            shutil.copytree(v9_provider / "build/debug", v9_artifacts)
            shutil.rmtree(v9_provider / "src")
            v9_consumer = case_root / "consumer-v9"
            package(v9_consumer,
                    "m2b3a_parameter_return_consumer_v9", case["consumer"])
            v9_env = v9_env | {
                "MOSS_MODULE_PATH": str(v9_artifacts),
                "MARGO_HOME": str(case_root / "margo-consumer-v9"),
            }
            command([str(MARGO), "build"], cwd=v9_consumer, env=v9_env)
            v9_output = command([str(MARGO), "run"], cwd=v9_consumer, env=v9_env)
            assert v9_output == case["expected_output"], v9_output
            print(f"PASS {case['id']}: ABI-v9 source-free compatibility")
            continue

        print(f"PASS {case['id']}: source-free baseline executes via legacy record")

        poisoned = case_root / "poisoned-artifacts"
        shutil.copytree(artifacts, poisoned)
        command([str(poisoner), str(poisoned / "Service.mossi"),
                 str(poisoned / "libService.rlib"), "Service", "fresh"],
                cwd=ROOT, env=base_env)
        poisoned_env = env | {
            "MOSS_MODULE_PATH": str(poisoned),
            "MARGO_HOME": str(case_root / "margo-poisoned"),
        }
        poisoned_build = subprocess.run(
            [str(MARGO), "build"], cwd=consumer, env=poisoned_env,
            capture_output=True, text=True, timeout=240)
        if poisoned_build.returncode == 0:
            poisoned_run = subprocess.run(
                [str(MARGO), "run"], cwd=consumer, env=poisoned_env,
                capture_output=True, text=True, timeout=240)
            poisoned_output = poisoned_run.stdout + poisoned_run.stderr
            assert (poisoned_run.returncode != 0 or
                    poisoned_run.stdout != case["expected_output"]), (
                case["id"], "poisoned body preserved semantics; classify and "
                "prove its non-legacy execution path before accepting it",
                poisoned_output)
            print(f"PASS {case['id']}: poisoned legacy body changes execution")
        else:
            print(f"PASS {case['id']}: removing generic_stmt rejects consumer")

    for case in corpus["already-covered"]:
        suite_env = base_env.copy()
        if case.get("abi_mode") == "v9":
            suite_env.pop("MOSS_STAGED_ABI", None)
        command([sys.executable, case["test"], str(MOSS)], cwd=ROOT,
                env=suite_env)
        print(f"PASS covered checked-body suite: {case['id']}")

print("PASS prioritized ABI-v10 source-free generic corpus")
