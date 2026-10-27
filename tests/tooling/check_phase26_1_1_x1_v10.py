#!/usr/bin/env python3
"""Verify ABI-v10 Section 2 execution and non-generic E1 invalidation."""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve()
MARGO = ROOT / "margo"
WORK = ROOT / "tmp" / "phase26-1-1-x1-v10" / str(time.time_ns())


def run(command: list[str], *, cwd: Path, env: dict[str, str], ok: bool = True):
    result = subprocess.run(command, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if ok and result.returncode:
        raise AssertionError(
            f"command failed ({result.returncode}): {command}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}")
    return result


def section(path: Path) -> tuple[dict[str, str], str, str]:
    text = path.read_text(encoding="utf-8")
    marker = "\nexecution_body_version "
    if marker not in text:
        raise AssertionError(f"ABI-v10 interface has no Section 2: {path}")
    section_text = text.split(marker, 1)[1]
    lines = section_text.splitlines()
    fields = dict(line.split(" ", 1) for line in text.splitlines()
                  if " " in line and not line.startswith(" "))
    payload_line = lines.index("execution_body_payload")
    payload_lines = lines[payload_line + 1:-1]
    payload = "\n".join(payload_lines) + "\n"
    declared_length = int(lines[1].split()[1])
    body_hash = lines[2].split("sha256:", 1)[1]
    if len(payload.encode("utf-8")) != declared_length:
        raise AssertionError("ABI-v10 Section 2 payload length does not match")
    return fields, body_hash, payload


def build_check(app: Path, env: dict[str, str]) -> dict:
    result = run([str(MARGO), "build", "--check", "--json"],
                 cwd=app, env=env)
    return json.loads(result.stdout)


def main() -> int:
    library = WORK / "library"
    app = WORK / "app"
    for root, package, dependency in (
            (library, "x1_v10_library", ""),
            (app, "x1_v10_app",
             '\n[dependencies]\nx1_v10_library = { path = "../library" }\n')):
        (root / "src").mkdir(parents=True, exist_ok=True)
        (root / "Moss.toml").write_text(
            f'[package]\nname = "{package}"\nversion = "0.1.0"\n'
            f'\n[build]\nsource = "src"\n{dependency}',
            encoding="utf-8")

    provider_source = library / "src" / "library.moss"
    provider_source.write_text(
        "module x1_v10_library\n"
        "fn helper(value: Int) -> Int:\n  return value + 1\n"
        "export fn value() -> Int:\n  return helper(1)\n"
        "export fn identity(value):\n  return value\n",
        encoding="utf-8")
    (app / "src" / "main.moss").write_text(
        "module x1_v10_app\nimport x1_v10_library\n"
        "fn main():\n"
        "  echo x1_v10_library.value()\n"
        "  echo x1_v10_library.identity(7)\n",
        encoding="utf-8")

    env = dict(os.environ)
    env["MOSS"] = str(COMPILER)
    env["MOSS_STAGED_ABI"] = "v10"
    initial = build_check(app, env)
    if set(initial.get("checked", [])) != {"x1_v10_library", "x1_v10_app"}:
        raise AssertionError(f"initial ABI-v10 graph was not checked: {initial}")

    interface = library / "build" / "check-debug" / "x1_v10_library.mossi"
    first_fields, first_body_hash, first_payload = section(interface)
    for key in ("provider_contract_digest", "semantic_contract_hash",
                "binary_contract_hash", "interface_hash"):
        if key not in first_fields:
            raise AssertionError(f"ABI-v10 Section 1 field is missing: {key}")
    fingerprint_state = json.loads(
        (interface.parent / ".margo-fingerprint.json").read_text())
    first_fingerprint = fingerprint_state["fingerprint"]
    if first_payload.splitlines()[0] != f"package_fingerprint {first_fingerprint}":
        raise AssertionError("Section 2 is not bound to Margo's package fingerprint")
    if first_payload.splitlines()[1] != (
            "interface_digest " + first_fields["semantic_contract_hash"].removeprefix("sha256:")):
        raise AssertionError("Section 2 is not bound to the v10 Section 1 digest")
    if '"x1_v10_library__helper"' not in first_payload:
        raise AssertionError("reachable private helper body is missing from Section 2")

    provider_source.write_text(provider_source.read_text().replace(
        "return value + 1", "return value + 2", 1), encoding="utf-8")
    edited = build_check(app, env)
    if set(edited.get("checked", [])) != {"x1_v10_library"}:
        raise AssertionError(f"ABI-v10 non-generic E1 rechecked consumers: {edited}")
    second_fields, second_body_hash, second_payload = section(interface)
    for key in ("provider_contract_digest", "semantic_contract_hash",
                "binary_contract_hash", "interface_hash"):
        if first_fields[key] != second_fields[key]:
            raise AssertionError(f"non-generic E1 changed Section 1 {key}")
    if first_body_hash == second_body_hash or first_payload == second_payload:
        raise AssertionError("non-generic E1 did not update ABI-v10 Section 2")
    fingerprint_state = json.loads(
        (interface.parent / ".margo-fingerprint.json").read_text())
    second_fingerprint = fingerprint_state["fingerprint"]
    if second_payload.splitlines()[0] != f"package_fingerprint {second_fingerprint}":
        raise AssertionError("updated Section 2 does not match Margo's fingerprint")
    if second_payload.splitlines()[1] != (
            "interface_digest " + second_fields["semantic_contract_hash"].removeprefix("sha256:")):
        raise AssertionError("updated Section 2 does not match the v10 Section 1 digest")

    execution_env = env.copy()
    execution_env["MOSS_MODULE_PATH"] = str(interface.parent)
    executed = run([str(COMPILER), "run", "--interp",
                    str(app / "src" / "main.moss")],
                   cwd=app, env=execution_env)
    if executed.stdout != "3\n7\n":
        raise AssertionError(f"ABI-v10 Section 2/generic execution mismatch: {executed.stdout!r}")
    print("ABI-v10 Section 2 binding, private helper execution, generic Section 1, and E1 provider-only recheck passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
