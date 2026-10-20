#!/usr/bin/env python3
"""Three-mode executable source-free generic parity oracle (Phase 25.1 R0).

For each selected synthetic or real-source fixture: run source-backed native
Debug/Release controls; run a separately built ABI-v9 provider/consumer with
provider source removed; then run a separately built ABI-v10 provider/consumer
with provider source removed and selected legacy generic_stmt records deleted.
Provider build failures remain first-class red rows in the JSON report.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
MOSS = ROOT / "moss"
MARGO = ROOT / "margo"
POISON_SOURCE = ROOT / "tests/tooling/poison_phase25_generic_callable.cpp"
TMP = ROOT / "tmp"
CORPUS = ROOT / "tests/phase25/source_free_corpus/corpus.json"


def invoke(argv: list[str], cwd: Path, env: dict[str, str], expected: int | None = 0) -> dict:
    result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True, text=True, timeout=900)
    output = result.stdout + result.stderr
    if expected is not None and result.returncode != expected:
        raise RuntimeError(f"{argv!r} returned {result.returncode}, expected {expected}:\n{output[-8000:]}")
    return {
        "command": [str(part).replace(str(ROOT), "$REPO") for part in argv],
        "exit_code": result.returncode,
        "stdout": result.stdout.replace(str(ROOT), "$REPO"),
        "stderr": result.stderr.replace(str(ROOT), "$REPO"),
        "output_sha256": hashlib.sha256(output.replace(str(ROOT), "$REPO").encode()).hexdigest(),
    }


def write_package(path: Path, name: str, sources: dict[str, str], files: dict[str, bytes] | None = None) -> None:
    source_dir = path / "src"
    source_dir.mkdir(parents=True)
    (path / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n',
        encoding="utf-8",
    )
    for filename, text in sources.items():
        (source_dir / filename).write_text(text, encoding="utf-8")
    for filename, contents in (files or {}).items():
        (path / filename).write_bytes(contents)


def env_for(case_root: Path, *, mode: str, profile: str, provider: bool = False) -> dict[str, str]:
    env = os.environ.copy()
    env["MOSS"] = str(MOSS)
    env["MARGO_HOME"] = str(case_root / "margo" / mode / profile / ("provider" if provider else "consumer"))
    env["TMPDIR"] = str(TMP)
    env.pop("MOSS_PHASE25_EMPTY_VECTOR_EMIT", None)
    env.pop("MOSS_STAGED_ABI", None)
    if mode == "v10":
        env["MOSS_STAGED_ABI"] = "v10"
    return env


def margo_build(package: Path, mode: str, profile: str, case_root: Path,
                *, provider: bool = False, module_path: Path | None = None,
                trace: bool = False) -> dict:
    env = env_for(case_root, mode=mode, profile=profile, provider=provider)
    if module_path:
        env["MOSS_MODULE_PATH"] = str(module_path)
    else:
        env.pop("MOSS_MODULE_PATH", None)
    if trace:
        env["MOSS_PHASE25_CHECKED_BODY_TRACE"] = "1"
    else:
        env.pop("MOSS_PHASE25_CHECKED_BODY_TRACE", None)
    argv = [str(MARGO), "build"] + (["--release"] if profile == "release" else [])
    return invoke(argv, package, env, expected=None)


def binary_path(package: Path, name: str, profile: str) -> Path:
    return package / "build" / profile / name


def run_binary(package: Path, name: str, profile: str, mode: str,
               case_root: Path, module_path: Path | None = None) -> dict:
    env = env_for(case_root, mode=mode, profile=profile)
    if module_path:
        env["MOSS_MODULE_PATH"] = str(module_path)
    return invoke([str(binary_path(package, name, profile))], package, env)


def case_result(case: dict, root: Path, poisoner: Path) -> dict:
    case_id = case["id"]
    safe_id = case_id.replace("-", "_")
    result = {"id": case_id, "priority": case.get("priority"), "category": case.get("category"), "modes": {}}
    expected = case.get("expected_output")
    for profile in ("debug", "release"):
        # A: provider and consumer sources in one root package.
        source_app = root / "source-backed" / profile
        write_package(source_app, f"p251_{safe_id}_source_{profile}", {
            case.get("provider_filename", "service.moss"): case.get("provider_source") or case.get("provider"),
            "main.moss": case["consumer"],
        }, case.get("input_files"))
        source_env = env_for(root, mode="source", profile=profile)
        build = margo_build(source_app, "source", profile, root)
        if build["exit_code"]:
            result["modes"].setdefault("source_backed", {})[profile] = {
                "status": "source-build-failed", "build": build,
            }
            continue
        run = run_binary(source_app, f"p251_{safe_id}_source_{profile}", profile,
                         "source", root)
        source_status = "pass" if run["stdout"] == expected else "output-mismatch"
        result["modes"].setdefault("source_backed", {})[profile] = {
            "status": source_status, "build": build, "run": run,
        }

        # B/C: independent provider builds with separate fresh artifact dirs.
        for mode in ("v9", "v10"):
            provider = root / mode / profile / "provider"
            provider_name = f"p251_{safe_id}_{mode}_{profile}_provider"
            write_package(provider, provider_name, {
                case.get("provider_filename", "service.moss"): case.get("provider_source") or case.get("provider")
            })
            provider_build = margo_build(provider, mode, profile, root, provider=True)
            if provider_build["exit_code"]:
                failure_text = provider_build["stdout"] + provider_build["stderr"]
                expected_error = case.get("provider_expected_error")
                status = "valid-but-unavailable-v10" if (
                    mode == "v10" and expected_error and expected_error in failure_text
                ) else "provider-build-failed"
                result["modes"].setdefault(mode, {})[profile] = {
                    "status": status,
                    "stage": "provider-build",
                    "expected_diagnostic": expected_error,
                    "build": provider_build,
                }
                continue
            artifact_dir = provider / "build" / profile
            interface_files = list(artifact_dir.glob("*.mossi"))
            if not interface_files:
                raise RuntimeError(f"missing provider artifacts for {case_id} {mode} {profile}")
            library = artifact_dir / f"lib{interface_files[0].stem}.rlib"
            if not library.is_file():
                raise RuntimeError(f"missing selected provider rlib for {case_id} {mode} {profile}: {library}")
            actual_abi = "native_abi 10" if mode == "v10" else "native_abi 9"
            if actual_abi.encode() not in interface_files[0].read_bytes():
                raise RuntimeError(f"{mode} provider artifact lacks {actual_abi}: {interface_files[0]}")
            artifacts = root / mode / profile / "artifacts"
            shutil.copytree(artifact_dir, artifacts)
            shutil.rmtree(provider / "src")
            if (provider / "src").exists():
                raise RuntimeError("provider source deletion failed")

            if mode == "v10":
                for callable_name in case.get("poison_callables", [case.get("callable_name", "fresh")]):
                    poison = invoke([str(poisoner), str(artifacts / interface_files[0].name),
                                     str(artifacts / library.name), interface_files[0].stem, callable_name],
                                    ROOT, os.environ.copy(), expected=0)
                    result.setdefault("poisoning", []).append({"callable": callable_name, "result": poison})

            consumer = root / mode / profile / "consumer"
            consumer_name = f"p251_{safe_id}_{mode}_{profile}_consumer"
            write_package(consumer, consumer_name, {"main.moss": case["consumer"]}, case.get("input_files"))
            check_env = env_for(root, mode=mode, profile=profile)
            check_env["MOSS_MODULE_PATH"] = str(artifacts)
            check_env["MOSS_PHASE25_CHECKED_BODY_TRACE"] = "1" if mode == "v10" else "0"
            consumer_build = margo_build(consumer, mode, profile, root, module_path=artifacts,
                                         trace=(mode == "v10"))
            if consumer_build["exit_code"]:
                failure_text = consumer_build["stdout"] + consumer_build["stderr"]
                expected_error = case.get("consumer_expected_error") or case.get("provider_expected_error")
                status = "valid-but-unavailable-v10" if (
                    mode == "v10" and expected_error and expected_error in failure_text
                ) else "consumer-build-failed"
                result["modes"].setdefault(mode, {})[profile] = {
                    "status": status, "stage": "source-free-consumer-build",
                    "expected_diagnostic": expected_error,
                    "build": consumer_build,
                }
                continue
            check_result = None
            if mode == "v10":
                check_env = env_for(root, mode=mode, profile=profile)
                check_env["MOSS_MODULE_PATH"] = str(artifacts)
                check_env["MOSS_PHASE25_CHECKED_BODY_TRACE"] = "1"
                check_result = invoke([str(MOSS), "check", str(consumer / "src/main.moss"), "--json"],
                                      consumer, check_env)
            run = run_binary(consumer, consumer_name, profile, mode, root, artifacts)
            status = "pass" if run["stdout"] == expected else "output-mismatch"
            details = {"status": status, "build": consumer_build, "run": run,
                       "provider_source_deleted": True,
                       "provider_interface_sha256": hashlib.sha256(interface_files[0].read_bytes()).hexdigest(),
                       "provider_library_sha256": hashlib.sha256(library.read_bytes()).hexdigest()}
            if mode == "v10":
                details["check"] = check_result
                details["legacy_poisoned"] = True
                details["checked_projection_trace"] = "MOSS_CHECKED_BODY_PROJECTED" in check_result["stdout"] + check_result["stderr"]
                if not details["checked_projection_trace"]:
                    details["status"] = "missing-checked-projection-trace"
            result["modes"].setdefault(mode, {})[profile] = details
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--case", action="append", help="corpus case id; repeat to select more than one")
    parser.add_argument("--include-sdk-bytes-range", action="store_true",
                        help="also exercise the actual bytes.from_range provider and source-free consumer")
    parser.add_argument("--output", type=Path, default=ROOT / "tmp/phase25-1-r0-oracle.json")
    args = parser.parse_args()
    TMP.mkdir(exist_ok=True)
    corpus = json.loads(CORPUS.read_text(encoding="utf-8"))
    cases = [case for case in corpus["cases"] if not args.case or case["id"] in args.case]
    if args.case and {case["id"] for case in cases} != set(args.case):
        raise SystemExit(f"unknown case(s): {sorted(set(args.case) - {case['id'] for case in cases})}")
    if args.include_sdk_bytes_range:
        cases.append({
            "id": "actual-sdk-bytes-from-range",
            "priority": "P1-sdk-essential",
            "category": "SDK generic iteration, assert effect, borrowed Range, owned Vector mutation",
            "provider_source": (ROOT / "sdk/foundations/bytes/src/bytes.moss").read_text(encoding="utf-8"),
            "provider_filename": "bytes.moss",
            "callable_name": "from_range",
            "poison_callables": ["from_range"],
            "consumer": (ROOT / "sdk/foundations/acceptance/repros/source_free_range/src/main.moss").read_text(encoding="utf-8"),
            "input_files": {"p4.bin": b"ABABC"},
            "expected_output": "329\n",
            "consumer_expected_error": "SOURCE_FREE_CHECKED_BODY_UNAVAILABLE",
        })
    with tempfile.TemporaryDirectory(prefix="phase25-1-r0-oracle-", dir=TMP) as directory:
        root = Path(directory)
        poisoner = root / "poison_legacy"
        compiled = invoke(["g++", "-std=c++17", "-O0", "-Wall", "-Wextra", "-Werror",
                          "-Wno-missing-field-initializers", "-pedantic", "-Isrc",
                          str(POISON_SOURCE), "-o", str(poisoner)], ROOT, os.environ.copy())
        results = [case_result(case, root / case["id"], poisoner) for case in cases]
        payload = {
            "schema": "phase25-1-three-mode-oracle-v1",
            "compiler_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
            "language_version": "moss-0.1",
            "compiler_sha256": hashlib.sha256(MOSS.read_bytes()).hexdigest(),
            "poisoner_build": compiled,
            "cases": results,
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        statuses = {}
        for result in results:
            for mode, profiles in result["modes"].items():
                statuses.setdefault(mode, []).extend(item["status"] for item in profiles.values())
        print(json.dumps({"compiler_commit": payload["compiler_commit"], "cases": len(results), "mode_statuses": statuses,
                          "output": str(args.output)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
