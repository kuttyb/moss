#!/usr/bin/env python3
"""Check exact checker EST authority for local specialized ReturnValue nodes."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/tooling/fixtures/generic_specialization/local_pipeline_tuples.moss"
TMP = ROOT / "tmp"
CONSUMER_HOOKS = (
    "MOSS_TEST_CONSUMER_DROP_RETURN_VALUE_REQUIREMENT",
    "MOSS_TEST_CONSUMER_DUPLICATE_RETURN_VALUE_REQUIREMENT",
    "MOSS_TEST_CONSUMER_DROP_RETURN_VALUE_FACT",
    "MOSS_TEST_CONSUMER_DUPLICATE_RETURN_VALUE_FACT",
    "MOSS_TEST_CONSUMER_PERTURB_RETURN_VALUE_FACT",
    "MOSS_TEST_CONSUMER_WRONG_RETURN_VALUE_CONTEXT",
    "MOSS_TEST_CONSUMER_WRONG_RETURN_VALUE_SPECIALIZATION",
)


def run(args: list[str], env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=ROOT, env=env, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          check=False)


def assert_fail_closed(result: subprocess.CompletedProcess[str], label: str):
    output = result.stdout + result.stderr
    if result.returncode == 0 or "MOSS_INTERNAL_INVARIANT" not in output or \
            not any(term in output.lower()
                    for term in ("return-value", "returnvalue", "return value")):
        raise AssertionError(f"{label} did not fail closed:\n{output}")


def mode_environment(mode: str) -> dict[str, str]:
    env = os.environ.copy()
    for key in list(env):
        if key.startswith("MOSS_TEST_") or key == "MOSS_EST":
            env.pop(key)
    if mode == "shadow":
        env["MOSS_EST"] = "shadow"
    return env


def run_mode(compiler: str, mode: str) -> int:
    env = mode_environment(mode)
    dump = run([compiler, "check", "--json", str(FIXTURE)], {
        **env, "MOSS_TEST_DUMP_PHASE28_AUTHORITY_FACTS": "1"})
    response = json.loads(dump.stdout)
    if dump.returncode or not response.get("ok"):
        raise AssertionError(f"{mode}: local generic ReturnValue check failed: "
                             f"{dump.stdout}\n{dump.stderr}")
    rows = re.findall(r"PHASE28_RETURN_FACT ([^\n]+)", dump.stderr)
    local = [row for row in rows if "context=fn:size<string>" in row]
    if len(local) != 1 or "type=int" not in local[0] or \
            "est_context=fn:size<string>" not in local[0] or \
            "specialization=specialization-v2:" not in local[0]:
        raise AssertionError(f"{mode}: ReturnValue lacks exact local specialization TypeRef: "
                             f"{rows!r}\n{dump.stderr}")

    TMP.mkdir(exist_ok=True)
    outdir = Path(tempfile.mkdtemp(prefix="phase28-local-return-", dir=TMP))
    try:
        for suffix, extra in (("debug", []),
                              ("optimized", ["-Oshared-memory"])):
            rust = outdir / f"local-return-{mode}-{suffix}.rs"
            generated = run([compiler, *extra, str(FIXTURE), "-o", str(rust)], env)
            if generated.returncode:
                raise AssertionError(f"{mode}/{suffix}: Native generation failed:\n"
                                     f"{generated.stderr}")
            source = rust.read_text(encoding="utf-8")
            start = source.find("fn __moss_specialize_size_")
            end = source.find("// Moss tooling end|fn:size<string>", start)
            if start < 0 or end < 0:
                raise AssertionError(f"{mode}/{suffix}: specialized function not emitted")
            body = source[start:end]
            if ".clone()" in body or ".to_owned()" in body:
                raise AssertionError(f"{mode}/{suffix}: local return emitted an implicit copy")
            binary = outdir / f"local-return-{mode}-{suffix}"
            compiled = run(["rustc", "--edition=2021", "-D", "warnings",
                            str(rust), "-o", str(binary)], env)
            if compiled.returncode:
                raise AssertionError(f"{mode}/{suffix}: rustc failed:\n"
                                     f"{compiled.stdout}\n{compiled.stderr}")
            native = run([str(binary)], env)
            if native.returncode or native.stdout != "3\n":
                raise AssertionError(f"{mode}/{suffix}: native output mismatch: "
                                     f"{native.stdout!r}\n{native.stderr}")

        fast = run([compiler, "run", "--interp", str(FIXTURE)], env)
        if fast.returncode or fast.stdout != "3\n":
            raise AssertionError(f"{mode}: Fast Debug output mismatch: "
                                 f"{fast.stdout!r}\n{fast.stderr}")

        for hook in ("MOSS_TEST_DROP_LOCAL_SPECIALIZED_RETURN_EST_FACT",
                     "MOSS_TEST_CONTRADICT_EXISTING_LOCAL_SPECIALIZED_RETURN_EST_FACT",
                     "MOSS_TEST_PERTURB_LOCAL_SPECIALIZED_RETURN_EST_FACT"):
            failed = run([compiler, "check", "--json", str(FIXTURE)],
                         {**env, hook: "1",
                          "MOSS_TEST_SELECT_LOCAL_SPECIALIZED_RETURN_EST_FACT": "size"})
            assert_fail_closed(failed, f"{mode}/{hook}")

        for hook in CONSUMER_HOOKS:
            corrupted_env = {**env, hook: "1"}
            artifact = outdir / f"corrupt-{mode}-{hook}.rs"
            artifact.unlink(missing_ok=True)
            native_failure = run([compiler, str(FIXTURE), "-o", str(artifact)],
                                 corrupted_env)
            assert_fail_closed(native_failure, f"{mode}/Native/{hook}")
            if artifact.exists():
                raise AssertionError(f"{mode}/{hook}: failed generation left Rust output")
            fast_failure = run([compiler, "run", "--interp", str(FIXTURE)],
                               corrupted_env)
            assert_fail_closed(fast_failure, f"{mode}/FastDebug/{hook}")
    finally:
        shutil.rmtree(outdir)
    return 1 + 3 + 1 + 3 + len(CONSUMER_HOOKS) * 2


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_phase28_local_return_authority.py COMPILER")
    compiler = str(Path(sys.argv[1]).resolve())
    scenarios = sum(run_mode(compiler, mode) for mode in ("default", "shadow"))
    print(f"Phase 28 local ReturnValue EST authority: {scenarios} scenarios PASS")


if __name__ == "__main__":
    main()
