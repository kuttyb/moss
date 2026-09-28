#!/usr/bin/env python3
"""Phase 22.4 Fast Debug/native/rustc and module-project parity regressions."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile


def fail(message: str) -> None:
    raise SystemExit(f"Phase 22.4 native parity test failed: {message}")


if len(sys.argv) != 2:
    fail("usage: check_phase224_native.py <moss-compiler>")

root = pathlib.Path.cwd().resolve()
compiler = pathlib.Path(sys.argv[1]).resolve()
margo = root / "margo"
scratch_root = root / "tmp"
scratch_root.mkdir(exist_ok=True)


def run(
    args: list[str | pathlib.Path],
    *,
    cwd: pathlib.Path = root,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(arg) for arg in args],
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def require_status(
    completed: subprocess.CompletedProcess[str], expected: int, context: str
) -> None:
    if completed.returncode != expected:
        fail(
            f"{context} exited {completed.returncode}, expected {expected}:\n"
            + completed.stdout
            + completed.stderr
        )


def require_needles(
    completed: subprocess.CompletedProcess[str], needles: tuple[str, ...], context: str
) -> None:
    output = completed.stdout + completed.stderr
    for needle in needles:
        if needle not in output:
            fail(f"{context} omitted {needle!r}:\n{output}")


for tool in ("rustc", "cargo"):
    version = run([tool, "--version"])
    require_status(version, 0, f"{tool} availability")


passing = [
    ("nested", root / "tests/phase224_debug_nested.moss"),
    ("specialization", root / "tests/phase224_debug_specialization.moss"),
]
failing = [
    (
        "domain",
        root / "tests/phase224_debug_domain.moss",
        ("actual=7", "expected=6"),
    ),
    (
        "failure",
        root / "tests/phase224_debug_failure.moss",
        ("actual=8", "expected=5"),
    ),
    (
        "local_mutation",
        root / "benchmarks/agent_debugging/cases/local_mutation.moss",
        ("actual=13", "expected=12"),
    ),
    (
        "branch_bug",
        root / "benchmarks/agent_debugging/cases/branch_bug.moss",
        ("actual=0", "expected=1"),
    ),
    (
        "loop_bug",
        root / "benchmarks/agent_debugging/cases/loop_bug.moss",
        ("actual=28", "expected=36"),
    ),
    (
        "domain_message_bug",
        root / "benchmarks/agent_debugging/cases/domain_message_bug.moss",
        ("actual=9", "expected=8"),
    ),
    (
        "helper_state_bug",
        root / "benchmarks/agent_debugging/cases/helper_state_bug.moss",
        ("actual=9", "expected=7"),
    ),
]


with tempfile.TemporaryDirectory(prefix="phase224-native-", dir=scratch_root) as temp:
    output = pathlib.Path(temp)
    for name, source in passing:
        checked = run([compiler, "check", source, "--json"])
        require_status(checked, 0, f"{name} Moss check")
        try:
            document = json.loads(checked.stdout)
        except json.JSONDecodeError as error:
            fail(f"{name} Moss check did not emit JSON: {error}")
        if not document.get("ok"):
            fail(f"{name} Moss check returned a failure envelope")

        interpreted = run([compiler, "run", "--interp", source])
        require_status(interpreted, 0, f"{name} Fast Debug execution")

        rust = output / f"{name}.rs"
        binary = output / name
        lowered = run([compiler, source, "-o", rust])
        require_status(lowered, 0, f"{name} native lowering")
        compiled = run(
            ["rustc", "--edition=2021", "-D", "warnings", rust, "-o", binary]
        )
        require_status(compiled, 0, f"{name} rustc compilation")
        native = run([binary])
        require_status(native, 0, f"{name} native execution")

    for name, source, needles in failing:
        checked = run([compiler, "check", source, "--json"])
        require_status(checked, 0, f"{name} Moss check")

        interpreted = run([compiler, "run", "--interp", source])
        require_status(interpreted, 1, f"{name} intended Fast Debug failure")
        require_needles(interpreted, needles, f"{name} Fast Debug failure")

        rust = output / f"{name}.rs"
        binary = output / name
        lowered = run([compiler, source, "-o", rust])
        require_status(lowered, 0, f"{name} native lowering")
        compiled = run(
            ["rustc", "--edition=2021", "-D", "warnings", rust, "-o", binary]
        )
        require_status(compiled, 0, f"{name} rustc compilation")
        native = run([binary])
        if native.returncode == 0:
            fail(f"{name} native execution did not reach its intended assertion")
        require_needles(native, needles, f"{name} native failure")


project = root / "benchmarks/agent_debugging/multimodule"
project_build = run([margo, "build"], cwd=project)
require_status(project_build, 0, "multi-module Margo build")
project_test = run([margo, "test"], cwd=project)
require_status(project_test, 0, "multi-module Margo test")
require_needles(project_test, ("1 passed", "0 failed"), "multi-module Margo test")

project_debug = run([margo, "debug"], cwd=project)
require_status(project_debug, 1, "multi-module intended Fast Debug failure")
require_needles(
    project_debug, ("actual=14", "expected=12"), "multi-module Fast Debug failure"
)
project_native = run([margo, "run"], cwd=project)
if project_native.returncode == 0:
    fail("multi-module native execution did not reach its intended assertion")
require_needles(
    project_native, ("actual=14", "expected=12"), "multi-module native failure"
)

print("Phase 22.4 native/rustc and Margo parity checks passed")
