#!/usr/bin/env python3
"""Build shipped packages serially and inventory exported generic callables.

Enumeration comes from compiler-emitted semantic_exports in each .mossi file.
Source-free harnesses are catalogued separately as evidence pointers; this
script does not infer that a callable is covered merely from a filename.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tomllib


ROOT = Path(__file__).resolve().parents[3]
MARGO = ROOT / "margo"
MOSS = Path(os.environ.get("MOSS", ROOT / "moss")).resolve()
OUT_DEFAULT = ROOT / "docs/phase25/PHASE_25_1_GENERIC_INVENTORY.json"
GENERIC_EXPORT = re.compile(r'^export fn (.+?) kind=generic semantic_hash=([^ ]+) return=([^ ]+) effects=(.*)$')
SOURCE_FREE_MARKERS = {
    "module-path": "MOSS_MODULE_PATH",
    "source-free-diagnostic": "SOURCE_FREE_",
    "abi-v9-artifact": "native_abi 9",
    "abi-v10-artifact": "native_abi 10",
    "legacy-generic-body": "generic_stmt",
    "staged-abi-selector": "MOSS_STAGED_ABI",
}
STATEMENT_TAG = re.compile(r'^  generic_stmt (\d+) ')
STMT_KIND = {0: "raw", 1: "pass", 2: "assign", 3: "call", 4: "message",
             5: "echo", 6: "if", 7: "else", 8: "while", 9: "for",
             10: "match", 11: "case", 12: "let", 13: "var", 14: "reply",
             15: "return", 16: "try", 17: "recover", 18: "raise"}


def package_roots() -> list[Path]:
    result = []
    for parent in (ROOT / "sdk", ROOT / "packages"):
        for manifest in sorted(parent.rglob("Moss.toml")):
            rel = manifest.parent.relative_to(ROOT).as_posix()
            if any(part in rel.split("/") for part in ("examples", "tests", "acceptance", "feasibility", "repros")):
                continue
            data = tomllib.loads(manifest.read_text(encoding="utf-8"))
            name = data.get("package", data.get("project", {})).get("name", "")
            if not name or "acceptance" in name or "integration" in name:
                continue
            result.append(manifest.parent)
    return result


def parse_interface(path: Path) -> list[dict]:
    lines = path.read_text(encoding="utf-8").splitlines()
    rows: list[dict] = []
    current: dict | None = None
    inside_exports = False
    dependency_stack: list[str] = []
    module_id = None
    for line in lines:
        if line.startswith("module_id "):
            module_id = line.removeprefix("module_id ")
        if line == "semantic_exports":
            inside_exports = True
            continue
        if not inside_exports:
            continue
        match = GENERIC_EXPORT.match(line)
        if match:
            if current:
                rows.append(current)
            current = {
                "module_id": module_id,
                "callable": match.group(1),
                "semantic_hash": match.group(2),
                "return_type": match.group(3),
                "effects": match.group(4),
                "parameters": [],
                "open_parameters": [],
                "requirements": [],
                "specialization_dependencies": [],
                "generic_entity_id": None,
                "generic_body_hash": None,
                "generic_statement_tags": [],
                "generic_statement_kinds": [],
                "generic_statement_count": 0,
                "private_dependencies": [],
                "private_dependency_statement_counts": {},
            }
            dependency_stack = []
            continue
        if current is None:
            if line in ("module_end", "end"):
                break
            continue
        stripped = line.strip()
        if stripped.startswith("export ") or stripped in ("nominals", "end_exports"):
            rows.append(current)
            current = None
            continue
        if stripped.startswith("param "):
            current["parameters"].append(stripped)
        elif stripped.startswith("open_parameters "):
            current["open_parameters"].append(stripped)
        elif stripped.startswith("requirements"):
            current["requirements"].append(stripped)
        elif stripped and stripped[0].isdigit() and current["requirements"]:
            current["requirements"].append(stripped)
        elif stripped.startswith("specialization_dependencies"):
            current["specialization_dependencies"].append(stripped)
        elif stripped.startswith("fn:") and current["specialization_dependencies"]:
            current["specialization_dependencies"].append(stripped)
        elif stripped.startswith("generic_entity_id "):
            current["generic_entity_id"] = stripped.removeprefix("generic_entity_id ")
        elif stripped.startswith("semantic_ir_body_hash="):
            current["generic_body_hash"] = stripped.split("=", 1)[1]
        elif stripped.startswith("generic_dependency_begin "):
            match_dependency = re.search(r'generic_dependency_begin\s+"([^"]+)"', stripped)
            if match_dependency:
                dependency = match_dependency.group(1)
                current["private_dependencies"].append(dependency)
                current["private_dependency_statement_counts"].setdefault(dependency, 0)
                dependency_stack.append(dependency)
        elif stripped.startswith("generic_dependency_end"):
            if dependency_stack:
                dependency_stack.pop()
        tag = STATEMENT_TAG.match(line)
        if tag:
            numeric_tag = int(tag.group(1))
            if dependency_stack:
                current["private_dependency_statement_counts"][dependency_stack[-1]] += 1
            else:
                current["generic_statement_tags"].append(numeric_tag)
                current["generic_statement_kinds"].append(STMT_KIND.get(numeric_tag, f"unknown:{numeric_tag}"))
                current["generic_statement_count"] += 1
    if current:
        rows.append(current)
    return rows


def harness_catalog() -> list[dict]:
    candidates = []
    for base in (ROOT / "tests", ROOT / "sdk", ROOT / "packages"):
        for path in sorted(base.rglob("*")):
            if not path.is_file() or path.suffix not in (".py", ".sh", ".moss"):
                continue
            rel = path.relative_to(ROOT).as_posix()
            name = path.name.lower()
            text = path.read_text(encoding="utf-8", errors="replace")
            reasons = []
            if "source_free" in name or "source-free" in name or "source_free" in rel or "source-free" in rel:
                reasons.append("source-free path/name")
            indicators = [marker for key, marker in SOURCE_FREE_MARKERS.items()
                          if marker in text]
            if path.suffix in (".py", ".sh") and "MOSS_MODULE_PATH" in indicators:
                reasons.append("explicit compiled-module search path")
            if path.suffix in (".py", ".sh") and "SOURCE_FREE_" in indicators:
                reasons.append("source-free diagnostic assertion")
            if ("generic_stmt" in indicators and "MOSS_STAGED_ABI" in indicators):
                reasons.append("staged generic-body artifact manipulation")
            if any(marker in indicators for marker in ("native_abi 9", "native_abi 10")):
                reasons.append("ABI artifact assertion")
            if reasons:
                candidates.append({
                    "path": rel,
                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                    "discovery_reasons": reasons,
                    "source_free_indicators": indicators,
                    "text": text,
                })
    return candidates


def link_harness_candidates(packages: list[dict], harnesses: list[dict]) -> None:
    for package in packages:
        for callable_record in package["exported_generic_callables"]:
            module_short = (callable_record.get("module_id") or "").rsplit("::", 1)[-1]
            callable_name = callable_record["callable"]
            candidates = []
            for harness in harnesses:
                text = harness["text"]
                has_callable = re.search(rf"\b{re.escape(callable_name)}\b", text) is not None
                has_module = not module_short or re.search(rf"\b{re.escape(module_short)}\b", text) is not None
                if has_callable and has_module:
                    candidates.append({"path": harness["path"], "match": "module-name-and-callable-token"})
            callable_record["source_free_harness_candidates"] = candidates


def run_build(package: Path, out: Path) -> dict:
    env = os.environ.copy()
    env.pop("MOSS_STAGED_ABI", None)
    env["MOSS"] = str(MOSS)
    env["MARGO_HOME"] = str(ROOT / "tmp/phase25-1-r0-margo" / package.relative_to(ROOT))
    env["TMPDIR"] = str(ROOT / "tmp")
    command = [str(MARGO), "build", "--json"]
    result = subprocess.run(command, cwd=package, env=env, capture_output=True, text=True, timeout=900)
    output = result.stdout + result.stderr
    entry = {
        "package_path": package.relative_to(ROOT).as_posix(),
        "package_name": None,
        "build_status": "pass" if result.returncode == 0 else "environmental-or-source-failure",
        "command": "MARGO_HOME=<isolated> TMPDIR=tmp ./margo build --json",
        "exit_code": result.returncode,
        "failure_output_tail": output[-5000:].replace(str(ROOT), "$REPO") if result.returncode else None,
        "interfaces": [],
        "exported_generic_callables": [],
    }
    try:
        payload = json.loads(result.stdout)
        entry["package_name"] = payload.get("project")
        entry["compiler_artifact_paths"] = [
            Path(path).resolve().relative_to(ROOT).as_posix()
            for path in payload.get("artifacts", {}).get("artifacts", {}).get("module_interfaces", [])
        ]
        entry["build_order"] = payload.get("build_order", [])
        entry["profile"] = payload.get("profile")
    except (json.JSONDecodeError, AttributeError):
        pass
    if result.returncode == 0:
        artifact_paths = [ROOT / path for path in entry.get("compiler_artifact_paths", [])]
        if not artifact_paths:
            artifact_paths = sorted((package / "build/debug").glob("*.mossi"))
        for interface in artifact_paths:
            if not interface.is_file():
                continue
            entry["interfaces"].append({"path": str(interface.relative_to(ROOT)), "sha256": hashlib.sha256(interface.read_bytes()).hexdigest()})
            entry["exported_generic_callables"].extend(parse_interface(interface))
    return entry


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=OUT_DEFAULT)
    parser.add_argument("--inventory-only", action="store_true", help="list production package roots without building")
    parser.add_argument("--refresh-exports", action="store_true",
                        help="reparse recorded compiler artifacts without rebuilding packages")
    args = parser.parse_args()
    args.output = args.output.resolve()
    packages = package_roots()
    if args.inventory_only:
        for package in packages:
            print(package.relative_to(ROOT))
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    compiler_sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    artifact_compiler_sha = compiler_sha
    compiler_executable = str(MOSS.relative_to(ROOT)) if MOSS.is_relative_to(ROOT) else str(MOSS)
    compiler_binary_sha256 = hashlib.sha256(MOSS.read_bytes()).hexdigest()
    if args.refresh_exports:
        payload = json.loads(args.output.read_text(encoding="utf-8"))
        artifact_compiler_sha = payload["compiler_commit"]
        # Reparse the exact interfaces recorded by the original inventory
        # build. Keep its compiler commit because package artifacts are not
        # rebuilt by this mode; this supports refreshing discovery metadata
        # without misattributing package builds to a later docs/test commit.
        for package in payload["packages"]:
            package["exported_generic_callables"] = []
            for interface in package.get("interfaces", []):
                path = ROOT / interface["path"]
                if path.is_file():
                    package["exported_generic_callables"].extend(parse_interface(path))
        results = payload["packages"]
        compiler_executable = payload.get("compiler_executable") or compiler_executable
        compiler_binary_sha256 = payload.get("compiler_binary_sha256") or compiler_binary_sha256
    else:
        results = [run_build(package, args.output) for package in packages]
    harnesses = harness_catalog()
    # The source text is used only to find candidate harness references. Every
    # candidate remains unverified until the harness is inspected/run.
    link_harness_candidates(results, harnesses)
    payload = {
        "schema": "phase25-1-generic-inventory-v1",
        "compiler_commit": artifact_compiler_sha,
        "compiler_executable": compiler_executable,
        "compiler_binary_sha256": compiler_binary_sha256,
        "inventory_refresh_commit": compiler_sha,
        "language_version": "moss-0.1",
        "inventory_method": "Margo debug package build; generic exports parsed from compiler-emitted semantic_exports in .mossi",
        "package_count": len(results),
        "generic_callable_count": sum(len(package["exported_generic_callables"]) for package in results),
        "build_failure_count": sum(package["build_status"] != "pass" for package in results),
        "packages": results,
        "existing_source_free_harnesses": [{key: value for key, value in harness.items() if key != "text"}
                                            for harness in harnesses],
    }
    args.output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({key: payload[key] for key in ("compiler_commit", "package_count", "generic_callable_count", "build_failure_count", "existing_source_free_harnesses")}, indent=2))
    print(f"wrote {args.output.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
