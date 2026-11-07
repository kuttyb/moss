#!/usr/bin/env python3
"""Run the Phase 28 L8 identity reproductions through both engines."""
from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/tooling/fixtures/phase28_l8"
EXPECTED = {
    "nest_spec": "10\n3.5",
    "dom_spec": "10",
    "meth_spec": "4\n2.5",
    "try_spec": "5\n1.5",
    "cb_spec": "12",
    "dead_tail2": "1\n0",
    "o15b": "3",
    "field_read": "3",
}
CALLBACK_CORPUS = (
    "examples/functional_dataflow.moss",
    "examples/functional_static_callables.moss",
    "tests/phase105_static_callable_effects.moss",
    "tests/phase45_cross_let_result.moss",
    "tests/phase45_divergence_fusion.moss",
    "tests/phase4_hof_smoke.moss",
    "tests/phase4_functional.moss",
    "tests/phase5_tooling.moss",
    "tests/swarm_023_pipeline_context_identity.moss",
)


def run(args: list[str], *, env=None, expected=0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(args, cwd=ROOT, env=env, text=True,
                            capture_output=True, timeout=90)
    if expected == 0 and result.returncode != 0:
        raise RuntimeError(
            f"command failed ({result.returncode}): {args}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}")
    if expected != 0 and result.returncode == 0:
        raise RuntimeError(f"command unexpectedly succeeded: {args}")
    return result


def native_output(compiler: str, source: Path, out: Path) -> str:
    rust = out / f"{source.stem}.rs"
    binary = out / source.stem
    run([compiler, str(source), "-o", str(rust)])
    run(["rustc", "--edition=2021", "-D", "warnings", str(rust), "-o",
         str(binary)])
    result = run([str(binary)])
    return result.stdout.strip()


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_phase28_l8_identity_repros.py COMPILER", file=sys.stderr)
        return 2
    compiler = str(Path(sys.argv[1]).resolve())
    scratch_root = ROOT / "tmp"
    scratch_root.mkdir(exist_ok=True)
    out = scratch_root / f"phase28-l8-identity-{os.getpid()}"
    out.mkdir()
    try:
        for name, expected in EXPECTED.items():
            source = FIXTURES / f"{name}.moss"
            fast = run([compiler, "run", "--interp", str(source)]).stdout.strip()
            native = native_output(compiler, source, out)
            if fast != expected or native != expected or fast != native:
                raise RuntimeError(
                    f"{name}: expected {expected!r}; Fast Debug={fast!r}; "
                    f"Native={native!r}")
            print(f"PASS {name}: {fast!r}")

        rejected = FIXTURES / "field_consume.moss"
        fast_error = run([compiler, "run", "--interp", str(rejected)],
                         expected=1)
        native_error = run([compiler, str(rejected), "-o",
                            str(out / "field_consume.rs")], expected=1)
        diagnostic = re.compile(
            r"moss:9: error: \[OWNERSHIP_ILLEGAL_CONSUME\] "
            r"plain field access cannot consume or transfer a non-Copy field; use take or copy")
        for mode, result in (("Fast Debug", fast_error), ("Native", native_error)):
            if not diagnostic.search(result.stderr + result.stdout):
                raise RuntimeError(
                    f"field_consume {mode} diagnostic mismatch:\n"
                    f"{result.stderr}{result.stdout}")
        print("PASS field_consume: rejected at line 9 in both engines")

        callback = FIXTURES / "cb_spec.moss"
        for hook, marker in (
            ("MOSS_TEST_CONSUMER_DROP_DYNAMIC_CALLBACK_FACT",
             "checked pipeline callback classification is missing"),
            ("MOSS_TEST_CONSUMER_PERTURB_DYNAMIC_CALLBACK_FACT",
             "static callback fact contradicts its checked value"),
        ):
            env = os.environ.copy()
            env[hook] = "1"
            result = run([compiler, "run", "--interp", str(callback)],
                         env=env, expected=1)
            if "MOSS_INTERNAL_INVARIANT" not in result.stderr + result.stdout or \
                    marker not in result.stderr + result.stdout:
                raise RuntimeError(
                    f"{hook} did not fail closed as expected:\n"
                    f"{result.stderr}{result.stdout}")
            print(f"PASS {hook}: fail closed")

        for relative in CALLBACK_CORPUS:
            source = ROOT / relative
            run([compiler, "run", "--interp", str(source)])
            rust = out / f"corpus-{len(list(out.glob('corpus-*.rs')))}.rs"
            run([compiler, str(source), "-o", str(rust)])
            run(["rustc", "--edition=2021", "-D", "warnings", str(rust),
                 "-o", str(rust.with_suffix(""))])
            print(f"PASS callback corpus: {relative}")
    finally:
        shutil.rmtree(out, ignore_errors=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"FAIL check_phase28_l8_identity_repros: {error}", file=sys.stderr)
        raise SystemExit(1)
