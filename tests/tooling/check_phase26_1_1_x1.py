#!/usr/bin/env python3
"""Exercise `.mossi` Section 2 checkstop and Section 1 digest boundaries."""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
MARGO = ROOT / "margo"
MOSS = Path(sys.argv[1]).resolve()
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
        "enum Hidden:\n  Value(value: Int)\n"
        "type HiddenBox:\n  value: Int\n"
        "  fn increment(amount: Int) -> Int:\n    return amount + 1\n"
        "fn hidden_helper(value: Int) -> Int:\n  return value + 1\n"
        "enum GenericFailure:\n  Bad(code: Int)\n"
        "export fn value() -> Int:\n"
        "  hidden = HiddenBox(value: hidden_helper(1))\n"
        "  tag = Hidden.Value(value: hidden.increment(hidden_helper(1)))\n"
        "  return hidden.increment(hidden_helper(1))\n"
        "export fn identity(value):\n  return value\n"
        "export fn maybe(value) -> Int:\n"
        "  if value == 0:\n    raise GenericFailure.Bad(code: value)\n"
        "  return value\n", encoding="utf-8")
    lib_source.write_text(lib_source.read_text() +
        "export enum Failure:\n  Bad\n"
        "export domain Worker:\n"
        "  fn Run() -> Int:\n    raise Failure.Bad\n"
        "  on_fail Failure.Bad:\n    reply 9\n", encoding="utf-8")
    (app / "src" / "main.moss").write_text(
        "module x1_app\nimport x1_library\nfn main():\n"
        "  worker = x1_library.Worker()\n"
        "  echo x1_library.value()\n"
        "  echo x1_library.identity(7)\n"
        "  echo message worker.Run()\n", encoding="utf-8")
    env = os.environ.copy()
    env["MOSS"] = str(MOSS)

    initial = run(env)
    interface = library / "build" / "check-debug" / "x1_library.mossi"
    initial_header, initial_body_hash, initial_payload = section(interface)
    if checked(initial) != {"x1_library", "x1_app"}:
        raise AssertionError(f"initial check did not build both packages: {initial}")

    sidecar = interface.with_suffix(".locations")
    first_locations = sidecar.read_text(encoding="utf-8")
    lib_source.write_text("\n" + lib_source.read_text(encoding="utf-8"),
                          encoding="utf-8")
    blank_line = run(env)
    blank_header, blank_body_hash, blank_payload = section(interface)
    interface_keys = ("concrete_interface_hash", "generic_semantic_hash",
                      "interface_hash", "provider_contract_digest")
    for key in interface_keys:
        if initial_header.get(key) != blank_header.get(key):
            raise AssertionError(f"blank-line edit changed Section 1 {key}")
    if checked(blank_line) != {"x1_library"}:
        raise AssertionError(f"blank-line edit rechecked a consumer: {blank_line}")
    if first_locations == sidecar.read_text(encoding="utf-8"):
        raise AssertionError("blank-line edit did not update .locations")
    if initial_body_hash == blank_body_hash or initial_payload == blank_payload:
        raise AssertionError("blank-line edit did not refresh Section 2 line data")
    initial_header, initial_body_hash, initial_payload = (
        blank_header, blank_body_hash, blank_payload)

    lib_source.write_text(lib_source.read_text().replace(
        "fn hidden_helper(value: Int) -> Int:\n  return value + 1",
        "fn hidden_helper(value: Int) -> Int:\n  return value + 2", 1),
                          encoding="utf-8")
    non_generic = run(env)
    body_header, body_hash, body_payload = section(interface)
    if any(initial_header.get(key) != body_header.get(key) for key in interface_keys):
        raise AssertionError("non-generic body edit changed the Section 1 digest")
    if initial_body_hash == body_hash or initial_payload == body_payload:
        raise AssertionError("non-generic body edit did not rewrite Section 2")
    if checked(non_generic) != {"x1_library"}:
        raise AssertionError(f"non-generic E1 did not check only provider: {non_generic}")

    lib_source.write_text(lib_source.read_text().replace(
        "export fn identity(value):\n  return value",
        "export fn identity(value):\n  return (value)", 1),
                          encoding="utf-8")
    generic = run(env)
    generic_header, _, _ = section(interface)
    if generic_header.get("generic_semantic_hash") == body_header.get("generic_semantic_hash"):
        raise AssertionError("generic body edit did not change the Section 1 digest")
    if checked(generic) != {"x1_library", "x1_app"}:
        raise AssertionError(f"generic edit did not recheck its consumer: {generic}")

    native = subprocess.run([str(MARGO), "build", "--json"], cwd=app,
                            env=env, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    if native.returncode:
        raise RuntimeError(f"margo build failed:\n{native.stdout}\n{native.stderr}")
    def interpret(profile: str) -> tuple[subprocess.CompletedProcess[str], list[dict]]:
        execution_env = env.copy()
        execution_env.pop("MOSS_FAST_DEBUG_SOURCE_ROOTS", None)
        execution_env["MOSS_MODULE_PATH"] = str(library / "build" / profile)
        result = subprocess.run(
            [str(MOSS), "run", "--interp", "--trace",
             str(app / "src" / "main.moss")],
            cwd=app, env=execution_env, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
        events = [json.loads(line) for line in result.stderr.splitlines()
                  if line.startswith("{")]
        return result, events

    check_only_dir = library / "build" / "check-debug"
    check_only_rlibs = list(check_only_dir.rglob("*.rlib"))
    if check_only_rlibs:
        raise AssertionError(
            f"check-only profile unexpectedly emitted rlibs: {check_only_rlibs}")
    interpreted, trace = interpret("check-debug")
    native_interpreted, native_trace = interpret("debug")
    for profile, result in (("check-only", interpreted),
                            ("native", native_interpreted)):
        if result.returncode or result.stdout != "4\n7\n9\n":
            raise AssertionError(
                f"{profile} Section 2 Fast Debug output mismatch: "
                f"{result.returncode}, {result.stdout!r}, {result.stderr}")
    def normalized_trace(events: list[dict]) -> list[dict]:
        normalized = []
        for event in events:
            record = dict(event)
            source_file = record.get("source_file", "")
            if source_file.endswith(".mossi"):
                # ABI-v9 generic bodies retain their existing Section 1 source
                # spelling; X3 adds source locations for editor navigation.
                record["source_file"] = "<generic-section1>"
            normalized.append(record)
        return normalized

    if normalized_trace(trace) != normalized_trace(native_trace):
        raise AssertionError("check-only and native dependency traces differ")
    # Section 2 is not semantic input or part of the interface fingerprint.
    # A damaged execution payload must leave `moss check`'s verdict alone but
    # must fail closed when the Fast Debug interpreter requests it.
    corrupt_dir = library / "build" / "corrupt-debug"
    corrupt_dir.mkdir(parents=True, exist_ok=True)
    interface_text = interface.read_text(encoding="utf-8")
    section_marker = "execution_body_hash sha256:"
    if section_marker not in interface_text:
        raise AssertionError("cannot corrupt missing Section 2 hash")
    marker_at = interface_text.index(section_marker) + len(section_marker)
    corrupt_text = (interface_text[:marker_at] + "0" * 64 +
                    interface_text[marker_at + 64:])
    (corrupt_dir / interface.name).write_text(corrupt_text, encoding="utf-8")
    corrupt_env = env.copy()
    corrupt_env["MOSS_MODULE_PATH"] = str(corrupt_dir)
    semantic = subprocess.run(
        [str(MOSS), "check", str(app / "src" / "main.moss"), "--json"],
        cwd=app, env=corrupt_env, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE)
    if semantic.returncode or not json.loads(semantic.stdout).get("ok"):
        raise AssertionError(
            f"Section 2 corruption changed semantic check: "
            f"{semantic.returncode}, {semantic.stdout}, {semantic.stderr}")
    execution_env = corrupt_env.copy()
    fast_debug = subprocess.run(
        [str(MOSS), "run", "--interp", str(app / "src" / "main.moss")],
        cwd=app, env=execution_env, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE)
    if fast_debug.returncode == 0 or \
            "SOURCE_FREE_EXECUTION_BODY_INVALID" not in (
                fast_debug.stdout + fast_debug.stderr):
        raise AssertionError(
            f"Fast Debug accepted corrupt Section 2: "
            f"{fast_debug.returncode}, {fast_debug.stdout}, {fast_debug.stderr}")
    provider_events = [event for event in trace
                       if event.get("function", "").endswith("value")]
    if not provider_events or any(
            event.get("source_file") != str(lib_source)
            for event in provider_events):
        raise AssertionError(f"dependency trace lost source locations: {provider_events}")
    executed = {event.get("function") for event in trace
                if event.get("event") in {"FunctionEnter", "MethodEnter"}}
    required_execution = {"x1_library__hidden_helper",
                          "x1_library__HiddenBox.increment"}
    if not required_execution.issubset(executed):
        raise AssertionError(
            f"source-free execution missed helper/method bodies: {executed}")
    if not any(event.get("event") == "on_fail" for event in trace):
        raise AssertionError("source-free execution missed handler failure-arm body")
    source_backed_callables = {
        "x1_library__hidden_helper",
        "x1_library__HiddenBox.increment",
        "x1_library__Worker.Run",
        "x1_library__Worker.Run$fail",
    }
    source_backed_events = [event for event in trace
                            if event.get("function") in source_backed_callables]
    if not source_backed_events or any(
            event.get("source_file") != str(lib_source)
            for event in source_backed_events):
        raise AssertionError(
            f"Section 2 callable trace locations disagree with source: "
            f"{source_backed_events}")
    _, _, executable_section = section(interface)
    if ('private_object "x1_library__HiddenBox"' not in executable_section or
            'private_enum "x1_library__Hidden"' not in executable_section):
        raise AssertionError("Section 2 omitted private nominal declarations")
    if 'private_enum "x1_library__GenericFailure"' in executable_section or \
            'generic_error_enum "GenericFailure"' not in interface.read_text():
        raise AssertionError(
            "private generic error enum was not reused from Section 1")
    print(json.dumps({"initial": sorted(checked(initial)),
                      "non_generic_e1": sorted(checked(non_generic)),
                      "generic_body_edit": sorted(checked(generic)),
                      "fast_debug_trace_events": len(trace), "ok": True},
                     sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
