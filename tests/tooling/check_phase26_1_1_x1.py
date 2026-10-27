#!/usr/bin/env python3
"""Exercise `.mossi` Section 2 checkstop and Section 1 digest boundaries."""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
MARGO = ROOT / "margo"
MOSS = ROOT / "build/compiler/moss-opt"
WORK = ROOT / "tmp/phase26-1-1-x1" / str(time.time_ns())


def run(env: dict[str, str]) -> dict:
    result = subprocess.run([str(MARGO), "build", "--check", "--json"],
                            cwd=WORK / "app", env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(f"margo build --check failed:\n{result.stdout}\n{result.stderr}")
    return json.loads(result.stdout)


def section(path: Path) -> tuple[dict[str, str], str, str]:
    text = path.read_text(encoding="utf-8")
    marker = "execution_body_version "
    if marker not in text:
        raise AssertionError(f"{path} has no Section 2")
    prefix, suffix = text.split(marker, 1)
    header = {}
    for line in prefix.splitlines():
        key, separator, value = line.partition(" ")
        if separator:
            header[key] = value
    lines = (marker + suffix).splitlines()
    payload_at = lines.index("execution_body_payload")
    body_hash = next(line.split(" ", 1)[1] for line in lines
                     if line.startswith("execution_body_hash "))
    payload = "\n".join(lines[payload_at + 1:-1]) + "\n"
    if len(payload.encode("utf-8")) != int(next(line.split()[1] for line in lines
                                                       if line.startswith("execution_body_length "))):
        raise AssertionError("Section 2 declared payload length is incorrect")
    return header, body_hash, payload


def checked(report: dict) -> set[str]:
    return set(report.get("checked", []))


def main() -> int:
    library = WORK / "library"
    app = WORK / "app"
    for root, name in ((library, "x1_library"), (app, "x1_app")):
        (root / "src").mkdir(parents=True, exist_ok=True)
        (root / "Moss.toml").write_text(
            f'[package]\nname = "{name}"\nversion = "0.1.0"\n' +
            ('\n[dependencies]\nx1_library = { path = "../library" }\n'
             if root == app else ''), encoding="utf-8")
    lib_source = library / "src" / "x1_library.moss"
    lib_source.write_text(
        "module x1_library\n"
        "export fn value() -> Int:\n  return 1\n"
        "export fn identity(value):\n  return value\n", encoding="utf-8")
    (app / "src" / "main.moss").write_text(
        "module x1_app\nimport x1_library\nfn main():\n  echo x1_library.value()\n"
        "  echo x1_library.identity(7)\n", encoding="utf-8")
    env = os.environ.copy()
    env["MOSS"] = str(MOSS)

    initial = run(env)
    interface = library / "build" / "check-debug" / "x1_library.mossi"
    initial_header, initial_body_hash, initial_payload = section(interface)
    if checked(initial) != {"x1_library", "x1_app"}:
        raise AssertionError(f"initial check did not build both packages: {initial}")

    lib_source.write_text(lib_source.read_text().replace("return 1", "return 2", 1),
                          encoding="utf-8")
    non_generic = run(env)
    body_header, body_hash, body_payload = section(interface)
    interface_keys = ("concrete_interface_hash", "generic_semantic_hash",
                      "interface_hash", "provider_contract_digest")
    if any(initial_header.get(key) != body_header.get(key) for key in interface_keys):
        raise AssertionError("non-generic body edit changed the Section 1 digest")
    if initial_body_hash == body_hash or initial_payload == body_payload:
        raise AssertionError("non-generic body edit did not rewrite Section 2")
    if checked(non_generic) != {"x1_library"}:
        raise AssertionError(f"non-generic E1 did not check only provider: {non_generic}")

    lib_source.write_text(lib_source.read_text().replace("return value", "return (value)", 1),
                          encoding="utf-8")
    generic = run(env)
    generic_header, _, _ = section(interface)
    if generic_header.get("generic_semantic_hash") == body_header.get("generic_semantic_hash"):
        raise AssertionError("generic body edit did not change the Section 1 digest")
    if checked(generic) != {"x1_library", "x1_app"}:
        raise AssertionError(f"generic edit did not recheck its consumer: {generic}")
    print(json.dumps({"initial": sorted(checked(initial)),
                      "non_generic_e1": sorted(checked(non_generic)),
                      "generic_body_edit": sorted(checked(generic)), "ok": True},
                     sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
