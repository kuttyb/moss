#!/usr/bin/env python3
"""Run the Phase 28 L8 identity reproductions through both engines."""
from __future__ import annotations

import os
import json
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
    "nested_method_member": "9",
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


def run(args: list[str], *, env=None, expected=0,
        cwd=ROOT) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(args, cwd=cwd, env=env, text=True,
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
    failures: list[str] = []
    try:
        provider = out / "mossi_provider"
        consumer = out / "mossi_consumer"
        (provider / "src").mkdir(parents=True)
        (consumer / "src").mkdir(parents=True)
        package = ('[project]\nname = "phase28_identity_provider"\n'
                   'version = "0.1.0"\n')
        (provider / "moss.toml").write_text(package, encoding="utf-8")
        (consumer / "moss.toml").write_text(
            '[project]\nname = "phase28_identity_consumer"\n'
            'version = "0.1.0"\n', encoding="utf-8")
        (provider / "src" / "provider.moss").write_text(
            "module Provider\nexport fn normalize(x):\n  return x\n",
            encoding="utf-8")
        imported_call = consumer / "src" / "main.moss"
        imported_call.write_text(
            "module App\nimport Provider\n"
            "fn pair(first, second):\n  echo first\n  echo second\n"
            "fn main():\n"
            "  pair(Provider.normalize(10), Provider.normalize(3.5))\n",
            encoding="utf-8")
        margo = str(ROOT / "margo")
        staged_env = {**os.environ, "MOSS": compiler,
                      "MOSS_STAGED_ABI": "v10"}
        try:
            provider_build = run([margo, "build", "--json"], env=staged_env,
                                 cwd=provider)
        except RuntimeError as error:
            provider_build = None
            failures.append(f"imported identity provider .mossi build failed: {error}")
        provider_artifacts = provider / "build" / "debug"
        provider_interface = provider_artifacts / "Provider.mossi"
        if provider_build is None or provider_build.returncode or not provider_interface.exists():
            failures.append("imported identity provider .mossi build failed: "
                            f"{provider_build.stdout if provider_build else ''}\n"
                            f"{provider_build.stderr if provider_build else ''}")
        else:
            imported_env = {**staged_env, "MOSS_MODULE_PATH":
                            str(provider_artifacts)}
            try:
                consumer_build = run([margo, "build", "--json"],
                                     env=imported_env, cwd=consumer)
            except RuntimeError as error:
                consumer_build = None
                failures.append(f"imported identity consumer build failed: {error}")
            if consumer_build is None or consumer_build.returncode:
                failures.append("imported identity consumer build failed: "
                                f"{consumer_build.stdout if consumer_build else ''}\n"
                                f"{consumer_build.stderr if consumer_build else ''}")
            else:
                build_info = json.loads(consumer_build.stdout)
                executable = Path(
                    build_info["artifacts"]["artifacts"]["executable"])
                imported_native = run([str(executable)], env=imported_env,
                                      cwd=consumer).stdout.strip()
                imported_fast = run([margo, "debug"], env=imported_env,
                                    cwd=consumer).stdout.strip()
                if imported_native != "10\n3.5" or imported_fast != "10\n3.5":
                    failures.append(
                        "imported nested-call identity output mismatch: "
                        f"Fast={imported_fast!r}, Native={imported_native!r}")
                else:
                    print("PASS imported nested-call identity: "
                          "Fast=Native='10\\n3.5'")

        for name, expected in EXPECTED.items():
            source = FIXTURES / f"{name}.moss"
            fast = native = None
            try:
                fast = run([compiler, "run", "--interp", str(source)]).stdout.strip()
            except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                failures.append(f"{name} Fast Debug: {error}")
            try:
                native = native_output(compiler, source, out)
            except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                failures.append(f"{name} Native: {error}")
            if fast == expected and native == expected and fast == native:
                print(f"PASS {name}: {fast!r}")
            else:
                print(f"FAIL {name}: expected={expected!r}; Fast={fast!r}; "
                      f"Native={native!r}", file=sys.stderr)
                if fast is not None and fast != expected:
                    failures.append(f"{name} Fast Debug output {fast!r}, wanted {expected!r}")
                if native is not None and native != expected:
                    failures.append(f"{name} Native output {native!r}, wanted {expected!r}")

        rejected = FIXTURES / "field_consume.moss"
        fast_error = native_error = None
        try:
            fast_error = run([compiler, "run", "--interp", str(rejected)],
                             expected=1)
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
            failures.append(f"field_consume Fast Debug: {error}")
        try:
            native_error = run([compiler, str(rejected), "-o",
                                str(out / "field_consume.rs")], expected=1)
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
            failures.append(f"field_consume Native: {error}")
        diagnostic = re.compile(
            r"moss:9: error: \[OWNERSHIP_ILLEGAL_CONSUME\] "
            r"plain field access cannot consume or transfer a non-Copy field; use take or copy")
        for mode, result in (("Fast Debug", fast_error), ("Native", native_error)):
            if result and not diagnostic.search(result.stderr + result.stdout):
                failures.append(f"field_consume {mode} diagnostic mismatch")
        if fast_error and native_error and not any(
                failure.startswith("field_consume") for failure in failures):
            print("PASS field_consume: rejected at line 9 in both engines")

        callback = FIXTURES / "cb_spec.moss"
        nested_method_member = FIXTURES / "nested_method_member.moss"
        call_edge_mutations = (
            "MOSS_TEST_CONSUMER_DROP_SEMANTIC_CALL_EDGE",
            "MOSS_TEST_CONSUMER_STALE_SEMANTIC_CALL_EDGE",
            "MOSS_TEST_CONSUMER_DUPLICATE_SEMANTIC_CALL_EDGE",
            "MOSS_TEST_CONSUMER_PERTURB_SEMANTIC_CALL_EDGE",
            "MOSS_TEST_CONSUMER_PERTURB_SEMANTIC_CALL_EDGE_TARGET",
            "MOSS_TEST_CONSUMER_PERTURB_SEMANTIC_CALL_EDGE_SPEC_ID",
            "MOSS_TEST_CONSUMER_PERTURB_SEMANTIC_CALL_EDGE_ARGUMENT_SPEC_ID",
        )
        # Mutation results count only when the exact unmutated input first
        # succeeds in both engines. Otherwise a pre-existing compiler failure
        # makes every mutation result vacuous.
        mutation_inputs = (
            ("callback", callback, EXPECTED["cb_spec"]),
            ("nested method", nested_method_member,
             EXPECTED["nested_method_member"]),
        )
        mutation_controls: dict[str, bool] = {}
        for label, source, expected in mutation_inputs:
            try:
                fast_control = run(
                    [compiler, "run", "--interp", str(source)]).stdout.strip()
            except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                fast_control = None
                failures.append(f"BLOCKED {label} mutations: Fast Debug positive control failed: {error}")
            try:
                native_control = native_output(compiler, source, out)
            except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                native_control = None
                failures.append(f"BLOCKED {label} mutations: Native positive control failed: {error}")
            mutation_controls[label] = (
                fast_control == expected and native_control == expected and
                fast_control == native_control)
            if mutation_controls[label]:
                print(f"PASS mutation positive control {label}: {expected!r}")
            else:
                failures.append(
                    f"BLOCKED {label} mutations: positive controls disagree or miss expected output; "
                    f"Fast={fast_control!r}, Native={native_control!r}, wanted={expected!r}")

        # These are the precise consumer invariant diagnostics for each path.
        # The current lookup hook normalizes Missing/Duplicate/Contradictory
        # into the consumer's fail-closed invariant at that call site.
        expected_mutation_diagnostic = {
            "Fast Debug": "MOSS_INTERNAL_INVARIANT exact checked call fact is missing, duplicated, or contradictory",
            "Native": "MOSS_INTERNAL_INVARIANT exact Native call edge is missing or duplicated",
            "Fast Debug nested method": "MOSS_INTERNAL_INVARIANT exact checked call fact is missing, duplicated, or contradictory",
            "Native nested method": "MOSS_INTERNAL_INVARIANT exact Native method edge is missing or contradictory",
        }
        for hook in call_edge_mutations:
            env = os.environ.copy()
            env[hook] = "1"
            for mode, input_label, command in (
                ("Fast Debug", "callback", [compiler, "run", "--interp", str(callback)]),
                ("Native", "callback", [compiler, str(callback), "-o",
                                         str(out / f"{hook}.rs")]),
                ("Fast Debug nested method", "nested method",
                 [compiler, "run", "--interp", str(nested_method_member)]),
                ("Native nested method", "nested method",
                 [compiler, str(nested_method_member), "-o",
                  str(out / f"{hook}-method.rs")]),
            ):
                if not mutation_controls[input_label]:
                    print(f"BLOCKED {hook} {mode}: positive control is red")
                    continue
                try:
                    result = run(command, env=env, expected=1)
                    output = result.stderr + result.stdout
                    expected_message = expected_mutation_diagnostic[mode]
                    if expected_message not in output:
                        failures.append(
                            f"{hook} {mode} diagnostic mismatch; expected exact "
                            f"{expected_message!r}, got:\n{output}")
                    else:
                        print(f"PASS {hook} {mode}: {expected_message}")
                except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                    failures.append(f"{hook} {mode}: {error}")

        for relative in CALLBACK_CORPUS:
            source = ROOT / relative
            try:
                run([compiler, "check", str(source)])
                rust = out / f"corpus-{len(list(out.glob('corpus-*.rs')))}.rs"
                run([compiler, str(source), "-o", str(rust)])
                run(["rustc", "--edition=2021", "-D", "warnings", str(rust),
                     "-o", str(rust.with_suffix(""))])
                print(f"PASS callback corpus: {relative}")
            except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                failures.append(f"callback corpus {relative}: {error}")
        if failures:
            print("\nFAILURES:", file=sys.stderr)
            for failure in failures:
                print(f"- {failure}", file=sys.stderr)
            return 1
    finally:
        shutil.rmtree(out, ignore_errors=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"FAIL check_phase28_l8_identity_repros: {error}", file=sys.stderr)
        raise SystemExit(1)
