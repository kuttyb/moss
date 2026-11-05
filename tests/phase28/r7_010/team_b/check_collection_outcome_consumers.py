#!/usr/bin/env python3
"""Focused native/Fast Debug checks for checker-proven collection outcomes."""
from __future__ import annotations
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[4]
QUEUE = ROOT / "tests/phase28/r7_010/team_b/collection_outcome_queue.moss"
MAP = ROOT / "tests/phase28/oracle_r7_005_1/fixtures/oracle_53_map_remove_missing.moss"
SOURCE_NEVER = ROOT / "tests/phase28/team_a_ownership/internal_never_not_source_type.moss"


def run(args: list[str], *, env: dict[str, str] | None = None,
        cwd: pathlib.Path = ROOT, ok: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(args, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if (result.returncode == 0) != ok:
        raise AssertionError(
            f"unexpected status {result.returncode}: {' '.join(args)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}")
    return result


def main() -> None:
    compiler = str(pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "./moss").resolve())
    output_dir = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else "tmp/team-b/collection-outcome-gate")
    output_dir = (ROOT / output_dir).resolve() if not output_dir.is_absolute() else output_dir
    output_dir.mkdir(parents=True, exist_ok=True)
    expected = {QUEUE: "empty_queue\n", MAP: "missing_key\n"}
    for index, (source, output) in enumerate(expected.items()):
        for mode, flags in (("debug", []), ("optimized", ["-Oshared-memory"])):
            rust = output_dir / f"outcome-{index}-{mode}.rs"
            binary = output_dir / f"outcome-{index}-{mode}"
            run([compiler, *flags, str(source), "-o", str(rust)])
            contents = rust.read_text(encoding="utf-8")
            if "std::convert::Infallible" not in contents:
                raise AssertionError(f"{source.name}: checked Never payload lacks Infallible carrier")
            run(["rustc", "--edition=2021", str(rust), "-o", str(binary)])
            native = run([str(binary)])
            fast = run([compiler, "run", "--interp", str(source)])
            if native.stdout != output or fast.stdout != output:
                raise AssertionError(
                    f"{source.name}/{mode}: expected {output!r}, native={native.stdout!r}, "
                    f"Fast Debug={fast.stdout!r}")

    corruption_hooks = (
        "MOSS_TEST_CONSUMER_DROP_COLLECTION_OUTCOME_REQUIREMENT",
        "MOSS_TEST_CONSUMER_DUPLICATE_COLLECTION_OUTCOME_REQUIREMENT",
        "MOSS_TEST_CONSUMER_PERTURB_COLLECTION_OUTCOME_REQUIREMENT",
        "MOSS_TEST_CONSUMER_DROP_COLLECTION_OUTCOME_FACT",
        "MOSS_TEST_CONSUMER_DUPLICATE_COLLECTION_OUTCOME_FACT",
        "MOSS_TEST_CONSUMER_PERTURB_COLLECTION_OUTCOME_FACT",
    )
    for hook in corruption_hooks:
        env = os.environ.copy()
        env[hook] = "1"
        run([compiler, str(QUEUE), "-o", str(output_dir / f"corrupt-{hook}.rs")],
            env=env, ok=False)
        run([compiler, "run", "--interp", str(QUEUE)], env=env, ok=False)

    source_type = run([compiler, "check", str(SOURCE_NEVER), "--json"], ok=False)
    if "never" not in source_type.stdout.lower() and "never" not in source_type.stderr.lower():
        raise AssertionError("source-level never type rejection did not identify `never`")
    print("checked Queue.pop/Map.remove Native Debug+Optimized and Fast Debug parity: PASS")
    print("collection outcome missing/duplicate/perturbed authority corruption gates: PASS")
    print("source-level never remains rejected: PASS")


if __name__ == "__main__":
    main()
