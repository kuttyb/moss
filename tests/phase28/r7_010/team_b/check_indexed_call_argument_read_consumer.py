#!/usr/bin/env python3
"""Exercise Native's exact checked direct-index call-argument READ consumer."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
FIXTURE = ROOT / "tests/phase28/r7_010/team_a/indexed_call_argument_read.moss"
BASE = ROOT / "tmp/team-b/indexed-call-argument-read-consumer"
HOOKS = (
    "MOSS_TEST_CONSUMER_DROP_INDEXED_CALL_ARGUMENT_READ_FACT",
    "MOSS_TEST_CONSUMER_DUPLICATE_INDEXED_CALL_ARGUMENT_READ_FACT",
    "MOSS_TEST_CONSUMER_PERTURB_INDEXED_CALL_ARGUMENT_READ_FACT",
    "MOSS_TEST_CONSUMER_DROP_INDEXED_CALL_ARGUMENT_READ_REQUIREMENT",
    "MOSS_TEST_CONSUMER_DUPLICATE_INDEXED_CALL_ARGUMENT_READ_REQUIREMENT",
    "MOSS_TEST_CONSUMER_PERTURB_INDEXED_CALL_ARGUMENT_READ_REQUIREMENT",
)


def make_package(name: str) -> Path:
    package = BASE / name
    if package.exists():
        shutil.rmtree(package)
    source = package / "src/main.moss"
    source.parent.mkdir(parents=True)
    source.write_bytes(FIXTURE.read_bytes())
    (package / "Moss.toml").write_text(
        '[package]\nname = "moss.team-b-indexed-call-read"\n'
        'version = "0.1.0"\n\n[build]\nsource = "src"\n',
        encoding="utf-8")
    return package


def invoke(margo: Path, compiler: Path, package: Path,
           mode: str | None = None,
           extra_env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env["MOSS"] = str(compiler)
    if extra_env:
        env.update(extra_env)
    mode_args = [f"--{mode}"] if mode else []
    return subprocess.run(
        [str(margo), "test", *mode_args, "--root", str(package), "--json"],
        cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False)


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit(
            "usage: check_indexed_call_argument_read_consumer.py COMPILER")
    compiler = Path(sys.argv[1])
    if not compiler.is_absolute():
        compiler = (ROOT / compiler).resolve()
    margo = ROOT / "margo"
    BASE.mkdir(parents=True, exist_ok=True)

    for mode in (None, "release", "interp"):
        package = make_package(mode or "debug")
        result = invoke(margo, compiler, package, mode=mode)
        try:
            response = json.loads(result.stdout)
        except json.JSONDecodeError as error:
            raise AssertionError(
                f"{mode or 'debug'} Margo test did not return JSON: "
                f"{result.stdout!r}\n{result.stderr}") from error
        if result.returncode or not response.get("ok"):
            raise AssertionError(
                f"{mode or 'debug'} direct indexed READ failed: "
                f"{response!r}\n{result.stderr}")
        if mode == "interp":
            continue
        generated = "\n".join(
            path.read_text(encoding="utf-8")
            for path in (package / "build").rglob("*.rs")
            if "/target/" not in path.as_posix())
        expected = "let __moss_assert_actual_0 = &((parts)[(0_i64) as usize]);"
        if expected not in generated:
            raise AssertionError(
                f"{mode or 'debug'} Native did not borrow the exact indexed place")
        actual_line = next(line for line in generated.splitlines()
                           if "let __moss_assert_actual_0" in line)
        if ".clone()" in actual_line:
            raise AssertionError("direct indexed READ unexpectedly cloned a Moss value")

    for hook in HOOKS:
        package = make_package(hook.lower())
        corrupted = invoke(margo, compiler, package,
                           extra_env={hook: "1"})
        if corrupted.returncode == 0 or \
                "MOSS_INTERNAL_INVARIANT" not in corrupted.stderr:
            raise AssertionError(
                f"{hook} did not fail closed during native lowering:\n"
                f"stdout={corrupted.stdout}\nstderr={corrupted.stderr}")
        if "indexed call argument" not in corrupted.stderr.lower() and \
                "indexed call-argument" not in corrupted.stderr.lower():
            raise AssertionError(
                f"{hook} failed for an unrelated reason:\n{corrupted.stderr}")

    print("Direct indexed call-argument READ: PASS (Native Debug/Optimized, Fast Debug, 6 corruption cases)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
