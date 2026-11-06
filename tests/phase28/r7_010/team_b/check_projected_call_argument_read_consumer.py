#!/usr/bin/env python3
"""Exercise Native's exact checked projected call-argument READ consumer."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
FIXTURE = ROOT / "tests/phase28/team_a_ownership/projected_call_argument_read.moss"
BASE = ROOT / "tmp/team-b/projected-call-argument-read-consumer"
HOOKS = (
    "MOSS_TEST_CONSUMER_DROP_PROJECTED_CALL_ARGUMENT_READ_FACT",
    "MOSS_TEST_CONSUMER_DUPLICATE_PROJECTED_CALL_ARGUMENT_READ_FACT",
    "MOSS_TEST_CONSUMER_PERTURB_PROJECTED_CALL_ARGUMENT_READ_FACT",
    "MOSS_TEST_CONSUMER_DROP_PROJECTED_CALL_ARGUMENT_READ_REQUIREMENT",
    "MOSS_TEST_CONSUMER_DUPLICATE_PROJECTED_CALL_ARGUMENT_READ_REQUIREMENT",
    "MOSS_TEST_CONSUMER_PERTURB_PROJECTED_CALL_ARGUMENT_READ_REQUIREMENT",
)


def invoke(margo: Path, compiler: Path, package: Path,
           extra_env: dict[str, str] | None = None,
           mode: str | None = None) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env["MOSS"] = str(compiler)
    if extra_env:
        env.update(extra_env)
    mode_args = [f"--{mode}"] if mode else []
    return subprocess.run(
        [str(margo), "test", *mode_args, "--root", str(package), "--json"],
        cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False)


def make_package(name: str) -> Path:
    package = BASE / name
    if package.exists():
        shutil.rmtree(package)
    source = package / "src/main.moss"
    source.parent.mkdir(parents=True)
    source.write_bytes(FIXTURE.read_bytes())
    (package / "Moss.toml").write_text(
        '[package]\nname = "moss.team-b-projected-call-read"\n'
        'version = "0.1.0"\n\n[build]\nsource = "src"\n',
        encoding="utf-8")
    return package


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit(
            "usage: check_projected_call_argument_read_consumer.py COMPILER")
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
                f"{mode or 'debug'} margo test did not return JSON: "
                f"{result.stdout!r}\n{result.stderr}") from error
        if result.returncode or not response.get("ok"):
            raise AssertionError(
                f"{mode or 'debug'} projected call-argument READ failed: "
                f"{response!r}\n{result.stderr}")
        if mode == "interp":
            continue
        rust_files = list((package / "build").rglob("*.rs"))
        generated = "\n".join(
            path.read_text(encoding="utf-8") for path in rust_files
            if "/target/" not in path.as_posix())
        expected = "let __moss_assert_actual_0 = &(((spans)[(0_i64) as usize]).text);"
        if expected not in generated:
            raise AssertionError(
                f"{mode or 'debug'} Native did not borrow the exact checked place")
        actual_line = next(line for line in generated.splitlines()
                           if "let __moss_assert_actual_0" in line)
        if ".clone()" in actual_line:
            raise AssertionError(
                f"{mode or 'debug'} projected call-argument READ cloned a Moss value")

    for hook in HOOKS:
        package = make_package(hook.lower())
        corrupted = invoke(margo, compiler, package, {hook: "1"})
        if corrupted.returncode == 0 or "MOSS_INTERNAL_INVARIANT" not in corrupted.stderr:
            raise AssertionError(
                f"{hook} did not fail closed during native lowering:\n"
                f"stdout={corrupted.stdout}\nstderr={corrupted.stderr}")
        if "projected call argument" not in corrupted.stderr.lower() and \
                "projected call-argument" not in corrupted.stderr.lower():
            raise AssertionError(
                f"{hook} failed for an unrelated reason:\n{corrupted.stderr}")

    print("Projected call-argument READ: PASS (Native Debug/Optimized, Fast Debug, 6 corruption cases)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
