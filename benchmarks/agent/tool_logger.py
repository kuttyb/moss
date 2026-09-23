#!/usr/bin/env python3
"""Transparent Moss/Margo invocation logger for isolated agent baselines."""

from __future__ import annotations

import fcntl
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from typing import Any


def diagnostic_codes(value: Any) -> list[str]:
    codes: list[str] = []
    if isinstance(value, dict):
        code = value.get("code")
        if isinstance(code, str) and code and code.upper() == code:
            codes.append(code)
        for child in value.values():
            codes.extend(diagnostic_codes(child))
    elif isinstance(value, list):
        for child in value:
            codes.extend(diagnostic_codes(child))
    return codes


def extract_codes(stdout: str) -> list[str]:
    found: list[str] = []
    try:
        return list(dict.fromkeys(diagnostic_codes(json.loads(stdout))))
    except json.JSONDecodeError:
        pass
    for line in stdout.splitlines():
        try:
            found.extend(diagnostic_codes(json.loads(line)))
        except json.JSONDecodeError:
            continue
    return list(dict.fromkeys(found))


def next_sequence(path: Path) -> int:
    path.parent.mkdir(parents=True, exist_ok=True)
    counter_path = path.with_suffix(path.suffix + ".sequence")
    lock_path = path.with_suffix(path.suffix + ".lock")
    with lock_path.open("a", encoding="utf-8") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        try:
            sequence = int(counter_path.read_text(encoding="utf-8")) + 1
        except (OSError, ValueError):
            sequence = 1
        counter_path.write_text(str(sequence), encoding="utf-8")
        return sequence


def append_record(path: Path, record: dict[str, Any]) -> None:
    lock_path = path.with_suffix(path.suffix + ".lock")
    with lock_path.open("a", encoding="utf-8") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        with path.open("a", encoding="utf-8") as output:
            output.write(json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n")


def main() -> int:
    tool = Path(sys.argv[0]).name
    if tool not in {"moss", "margo"}:
        print(f"baseline logger: unsupported tool name {tool!r}", file=sys.stderr)
        return 127
    real_variable = "MOSS_BASELINE_REAL_" + tool.upper()
    real = os.environ.get(real_variable)
    log_name = os.environ.get("MOSS_BASELINE_TOOL_LOG")
    if not real or not log_name:
        print("baseline logger: required environment is missing", file=sys.stderr)
        return 127

    environment = dict(os.environ)
    origin = environment.get("MOSS_BASELINE_TOOL_ORIGIN", "agent")
    sequence = next_sequence(Path(log_name))
    if tool == "margo":
        environment["MOSS_BASELINE_TOOL_ORIGIN"] = "margo-internal"
    started = time.monotonic()
    try:
        process = subprocess.run(
            [real, *sys.argv[1:]], text=True, capture_output=True,
            check=False, env=environment,
        )
        exit_code = process.returncode
        stdout, stderr = process.stdout, process.stderr
    except OSError as error:
        exit_code, stdout, stderr = 127, "", str(error) + "\n"
    duration_ms = round((time.monotonic() - started) * 1000)

    sys.stdout.write(stdout)
    sys.stdout.flush()
    sys.stderr.write(stderr)
    sys.stderr.flush()

    workspace = Path(environment.get("MOSS_BASELINE_WORKSPACE", "/")).resolve()
    cwd = Path.cwd().resolve()
    try:
        relative_cwd = cwd.relative_to(workspace).as_posix() or "."
    except ValueError:
        relative_cwd = str(cwd)
    append_record(Path(log_name), {
        "sequence": sequence,
        "tool": tool,
        "origin": origin,
        "argv": sys.argv[1:],
        "cwd": relative_cwd,
        "exit_code": exit_code,
        "duration_ms": duration_ms,
        "diagnostic_codes": extract_codes(stdout),
    })
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
