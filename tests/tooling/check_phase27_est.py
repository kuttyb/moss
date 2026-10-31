#!/usr/bin/env python3
"""Focused EST expression dump and mode-independent tree checks."""
import os
import json
import pathlib
import re
import resource
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPILER = pathlib.Path(sys.argv[1]).resolve()
CASES = {
    "message_expression.moss": ("message#", '"Run"', 'literal#'),
    "bitwise_shift.moss": ('binary#', '"<<"', '"&"'),
    "numeric_member.moss": ('member#', '"to_string"', 'literal#'),
    "syntax_27_1b.moss": ('pipeline#', 'placeholder#', 'collection#',
                           'constructor#', 'named-argument#'),
}

def run(path, mode, **extra):
    env = os.environ.copy()
    env.update(extra)
    env["MOSS_EST"] = mode
    return subprocess.run([str(COMPILER), "check", str(path), "--json"], cwd=ROOT,
                          env=env, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=False)

def check_rejected_source_parity():
    # These are Phase 27.1 REJECTED-SOURCE controls. Keep the baseline
    # diagnostics stable in both EST modes while expression reads migrate.
    expected = {
        "unsupported_exponent.moss":
            ("MOSS_COMPILE_ERROR", "invalid arithmetic expression", 2, 1),
        "unsupported_operators.moss":
            ("MOSS_COMPILE_ERROR", "invalid arithmetic expression", 2, 1),
        "membership_operator.moss":
            ("UNSUPPORTED_EXPRESSION_OPERATOR",
             "binary 'in' expression is not supported; use Map.get, key iteration, or an explicit search", 2, 1),
        "symbolic_boolean.moss":
            ("UNSUPPORTED_EXPRESSION_OPERATOR",
             "binary '&&' expression is not supported", 2, 1),
        "split_fragment.moss":
            ("MOSS_COMPILE_ERROR", "indentation jumps more than one block level", 3, 1),
        "incomplete_expression.moss":
            ("MOSS_COMPILE_ERROR", "invalid arithmetic expression", 2, 1),
        "unresolved_collection_result.moss":
            ("MOSS_COMPILE_ERROR", "unbound open result type for variable 'value'; heterogeneous or unresolved collection element type", 2, 1),
        "incomplete_vector.moss":
            ("MOSS_COMPILE_ERROR", "unbound open result type for variable 'value'; heterogeneous or unresolved collection element type", 2, 1),
        "juxtaposed_operands.moss":
            ("INVALID_EXPRESSION",
             "invalid expression '2 3': '2' is followed by '3' with no operator between them (if, while, and for are statements, not expressions)",
             2, 1),
    }
    for name, signature in expected.items():
        path = ROOT / "tests/phase27/est/repro" / name
        for mode in ("off", "shadow"):
            result = run(path, mode)
            try:
                payload = json.loads(result.stdout)
            except json.JSONDecodeError as error:
                raise SystemExit(f"{mode} did not return JSON for {name}: {result.stdout}{result.stderr}") from error
            diagnostic = payload.get("error", {})
            actual = (diagnostic.get("code"), diagnostic.get("message"),
                      diagnostic.get("line"), diagnostic.get("column"))
            if result.returncode == 0 or payload.get("ok") or actual != signature:
                raise SystemExit(f"{mode} rejected-source mismatch for {name}: {actual}, expected {signature}")

    # These Phase 27.1 source fixtures put a comparison suffix after a
    # pipeline without grouping. Moss parses that suffix as part of the final
    # pipeline stage and rejects the unknown stage name. The legacy text
    # splitter mistakes it for a top-level comparison; shadow mode must retain
    # the baseline diagnostic instead of asserting on the known parse delta.
    pipeline_precedence = {
        "examples/swarm/Surface/repros/swarm_067_pipe_comparison_diagnostic.moss":
            "unknown identifier 'count'",
        "examples/swarm/control_flow_2026/iterative_planner/failed_attempts/02_untyped_vector_parameters.moss":
            "unknown local function 'count'",
        "examples/swarm/control_flow_2026/iterative_planner/failed_attempts/03_pipeline_count_in_condition.moss":
            "unknown local function 'count'",
        "examples/swarm/control_flow_2026/simulation_engine/failed_attempts/02_while_pipeline_condition.moss":
            "unknown local function 'count'",
        "examples/swarm/expression_surface_2026/numerical_tool/failed_attempts/08_pipeline_comparison_precedence.moss":
            "unknown identifier 'sum'",
    }
    for relative, message in pipeline_precedence.items():
        path = ROOT / relative
        payloads = []
        for mode in ("off", "shadow"):
            result = run(path, mode)
            try:
                payload = json.loads(result.stdout)
            except json.JSONDecodeError as error:
                raise SystemExit(f"{mode} pipeline diagnostic did not return JSON for {relative}: {result.stdout}{result.stderr}") from error
            error = payload.get("error", {})
            if result.returncode == 0 or payload.get("ok") or \
                    error.get("code") != "UNKNOWN_SYMBOL_OR_TYPE" or \
                    error.get("message") != message:
                raise SystemExit(f"{mode} pipeline precedence diagnostic changed for {relative}: {error}")
            payloads.append(payload)
        if payloads[0] != payloads[1]:
            raise SystemExit(f"off/shadow pipeline diagnostic payload differs for {relative}")

    rejected_fragments = {
        "examples/swarm/control_flow_2026/iterative_planner/failed_attempts/01_multiline_task_literal.moss":
            ("MOSS_COMPILE_ERROR", "indentation jumps more than one block level", 45, 1),
        "examples/swarm/expression_surface_2026/numerical_tool/failed_attempts/01_len_method.moss":
            ("UNTYPED_FIELD_ACCESS", "field access 'sample.observed' requires a concrete receiver type; annotate the parameter or expose the behavior through a method/trait", 14, 1),
        "examples/swarm/expression_surface_2026/parser_evaluator/failed_attempts/01_multiline_vector_literal.moss":
            ("MOSS_COMPILE_ERROR", "indentation jumps more than one block level", 12, 1),
        "examples/swarm/expression_surface_2026/validation_engine/failed_attempts/01_multiline_constructor.moss":
            ("UNTYPED_FIELD_ACCESS", "field access 'rule.value' requires a concrete receiver type; annotate the parameter or expose the behavior through a method/trait", 27, 1),
        "examples/swarm/expression_surface_2026/validation_engine/failed_attempts/02_multiline_vector.moss":
            ("MOSS_COMPILE_ERROR", "indentation jumps more than one block level", 4, 1),
        "examples/swarm/expression_surface_2026/validation_engine/failed_attempts/15_multiline_test_vector.moss":
            ("MOSS_COMPILE_ERROR", "indentation jumps more than one block level", 3, 1),
    }
    for relative, signature in rejected_fragments.items():
        path = ROOT / relative
        for mode in ("off", "shadow"):
            result = run(path, mode)
            payload = json.loads(result.stdout)
            diagnostic = payload.get("error", {})
            actual = (diagnostic.get("code"), diagnostic.get("message"),
                      diagnostic.get("line"), diagnostic.get("column"))
            if result.returncode == 0 or payload.get("ok") or actual != signature:
                raise SystemExit(f"{mode} fragment rejection mismatch for {relative}: {actual}, expected {signature}")

    # The inventory has && but no isolated || probe. Verify both the
    # unsupported operator and a trailing arithmetic operator independently.
    with tempfile.TemporaryDirectory(prefix="phase27-rejected-", dir=ROOT / "tmp") as scratch:
        cases = {
            "symbolic_or.moss": ("fn main():\n  echo true || false\n",
                ("UNSUPPORTED_EXPRESSION_OPERATOR", "binary '||' expression is not supported", 2, 1)),
            "incomplete_subtraction.moss": ("fn main():\n  echo 2 -\n",
                ("MOSS_COMPILE_ERROR", "invalid arithmetic expression", 2, 1)),
        }
        for name, (source, signature) in cases.items():
            path = pathlib.Path(scratch) / name
            path.write_text(source, encoding="utf-8")
            for mode in ("off", "shadow"):
                result = run(path, mode)
                payload = json.loads(result.stdout)
                diagnostic = payload.get("error", {})
                actual = (diagnostic.get("code"), diagnostic.get("message"),
                          diagnostic.get("line"), diagnostic.get("column"))
                if result.returncode == 0 or payload.get("ok") or actual != signature:
                    raise SystemExit(f"{mode} rejected-source mismatch for {name}: {actual}, expected {signature}")

    package_fixture = ROOT / "tests/phase27/adversarial/cp-e-rawshape-rerun/package_incomplete"
    package_open_collection = ROOT / "tests/phase27/est/repro/package_open_collection"
    with tempfile.TemporaryDirectory(prefix="phase27-incomplete-package-", dir=ROOT / "tmp") as scratch:
        for fixture, error_message in (
                (package_fixture, "invalid arithmetic expression"),
                (package_open_collection,
                 "unbound open result type for variable 'value'; heterogeneous or unresolved collection element type")):
            package = pathlib.Path(scratch) / fixture.name
            shutil.copytree(fixture, package)
            for mode in ("off", "shadow"):
                env = os.environ.copy()
                env.update({"MOSS": str(COMPILER), "MOSS_EST": mode,
                            "MARGO_HOME": str(pathlib.Path(scratch) / f"margo-{fixture.name}-{mode}")})
                result = subprocess.run(
                    [str(ROOT / "margo"), "build", "--check", "--json"],
                    cwd=package, env=env, text=True, stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE, check=False)
                try:
                    payload = json.loads(result.stdout)
                except json.JSONDecodeError as error:
                    raise SystemExit(f"package {mode} check did not return JSON: {result.stdout}{result.stderr}") from error
                details = payload.get("error", "")
                if result.returncode == 0 or payload.get("ok") or \
                        '"code": "MOSS_COMPILE_ERROR"' not in details or \
                        f'"message": "{error_message}"' not in details:
                    raise SystemExit(f"package {mode} changed the baseline rejection for {fixture.name}: {payload}")

def main():
    check_rejected_source_parity()
    for name, pieces in CASES.items():
        path = ROOT / "tests/phase27/est" / name
        result = run(path, "shadow", MOSS_EST_DUMP="1",
                     MOSS_EST_COMPARE_STATS="1")
        if result.returncode != 0:
            raise SystemExit(f"shadow check failed for {name}: {result.stdout}{result.stderr}")
        comparisons = re.findall(
            r"MOSS_EST_SHADOW_COMPARE attempts=(\d+) compared=(\d+) "
            r"string_path_unparsed=(\d+)", result.stderr)
        if not comparisons or not any(int(compared) > 0
                                      for _, compared, _ in comparisons):
            raise SystemExit(f"legacy string shape was not compared for {name}")
        dumps = [line for line in result.stderr.splitlines() if line.startswith("MOSS_EST_DUMP ")]
        if not dumps or not all(piece in "\n".join(dumps) for piece in pieces):
            raise SystemExit(f"missing expected tree fields for {name}: {dumps}")
        for dump in dumps:
            ids = [int(x) for x in re.findall(r"#(\d+)", dump)]
            if len(ids) != len(set(ids)):
                raise SystemExit(f"ExprId reused within one occurrence in {name}: {ids}")
    tuple_probe = ROOT / "tests/tooling/fixtures/phase20_fileio_runtime_integration.moss"
    tuple_tree = run(tuple_probe, "shadow", MOSS_EST_DUMP="1",
                     MOSS_EST_COMPARE_STATS="1")
    if tuple_tree.returncode != 0 or "(tuple#" not in tuple_tree.stderr:
        raise SystemExit(f"tuple collection elements were not represented: {tuple_tree.stderr}")
    if not re.search(r"MOSS_EST_SHADOW_COMPARE .* compared=[1-9]", tuple_tree.stderr):
        raise SystemExit("tuple/string legacy shape was not compared")
    facts = run(ROOT / "tests/phase27/est/side_facts.moss", "shadow",
                MOSS_EST_FACTS_DUMP="1")
    if facts.returncode != 0:
        raise SystemExit(f"checker fact dump failed: {facts.stdout}{facts.stderr}")
    if not re.search(r'MOSS_EST_FACT expr=\d+ context="[^"]+" type="int"',
                     facts.stderr):
        raise SystemExit("checker did not retain a typed fact on the expression tree")
    if not re.search(r'MOSS_EST_EFFECT_FACT expr=\d+ context="[^"]+" effect=\d+',
                     facts.stderr):
        raise SystemExit("effect analysis did not retain a context-scoped EST fact")
    if not re.search(r'MOSS_EST_FACT expr=\d+ context="[^"]+" type="int" '
                     r'specialization="" target="callable-v1:[^"]+"',
                     facts.stderr):
        raise SystemExit("resolved direct-call target was not retained on its EST node")
    failure_arm = run(
        ROOT / "tests/tooling/phase20_review/probes/guard_domain_field_lifecycle.moss",
        "shadow", MOSS_EST_DUMP="1")
    if failure_arm.returncode != 0 or "unexpected FileIO error" not in failure_arm.stderr:
        raise SystemExit(
            f"failure-arm expression was not built into EST: {failure_arm.stdout}{failure_arm.stderr}")
    mismatch = run(ROOT / "tests/phase27/est/bitwise_shift.moss", "shadow",
                   MOSS_EST_SHADOW_MISMATCH_TEST="1")
    if mismatch.returncode == 0 or "injected mismatch" not in mismatch.stdout:
        raise SystemExit("shadow structural comparator did not reject an injected mismatch")
    pipeline_shapes = run(ROOT / "tests/phase27/est/pipeline_shape.moss", "shadow",
                          MOSS_EST_PIPELINE_SHAPE_TEST="1")
    if pipeline_shapes.returncode != 0:
        raise SystemExit(f"pipeline structural-shape cases failed: {pipeline_shapes.stdout}{pipeline_shapes.stderr}")
    probe = ROOT / "tests/phase27/est/bitwise_shift.moss"
    result = run(probe, "off", MOSS_EST_STATS="1")
    if result.returncode != 0 or not re.search(
            r"MOSS_EST_STATS arena_size=[1-9]\d* handle_count=[1-9]\d*",
            result.stderr):
        raise SystemExit(f"off did not build the compiler expression tree: {result.stdout}{result.stderr}")
    raw = ROOT / "tests/phase27/est/raw_places.moss"
    result = run(raw, "shadow", MOSS_EST_DUMP="1", MOSS_EST_CONTRACT_TEST="1")
    if "MOSS_EST_RAW" not in result.stderr:
        raise SystemExit(f"shadow raw/contract check did not reach EST: {result.stdout}{result.stderr}")
    for form in ("Assign", "CompoundAssign", "ExprStmt"):
        if f"form={form}" not in result.stderr:
            raise SystemExit(f"missing {form} classification")
    lines = [line for line in result.stderr.splitlines() if line.startswith("MOSS_EST_DUMP")]
    chained = [line for line in lines if "owner=1 " in line and "slot=LhsPlace" in line]
    if not chained or not all(piece in chained[0] for piece in
                               ('"a"', '"b"', '"i"', '"c"', '"j"')):
        raise SystemExit(f"projection chain was not preserved: {chained}")
    all_index_ids = re.findall(r'identifier#(\d+) "[ij]"', chained[0])
    if len(all_index_ids) != 2 or all_index_ids[0] == all_index_ids[1]:
        raise SystemExit(f"place index expressions lack distinct ExprIds: {chained}")
    if not any('owner=2 slot=LhsPlace' in line and '"total"' in line for line in lines) or \
       not any('owner=2 slot=RhsValue' in line and '"f"' in line and '"k"' in line for line in lines):
        raise SystemExit("compound assignment place/value binding is incomplete")
    if not any('owner=3 slot=Receiver' in line and '"f"' in line for line in lines) or \
       not any('owner=3 slot=Argument' in line and '"x"' in line for line in lines):
        raise SystemExit("expression-statement call is incomplete")
    lhs_rhs = [line for line in lines if "owner=4 " in line and
               ("slot=LhsPlace" in line or "slot=RhsValue" in line)]
    u_ids = [re.search(r'identifier#(\d+) "u"', line).group(1)
             for line in lhs_rhs if re.search(r'identifier#(\d+) "u"', line)]
    if len(set(u_ids)) != 2 or len(u_ids) < 2:
        raise SystemExit(f"assignment index occurrences share an ExprId: {lhs_rhs}")
    stale = subprocess.run([str(COMPILER), "check", str(raw), "--json"], cwd=ROOT,
                           env={**os.environ, "MOSS_EST": "shadow",
                                "MOSS_EST_CONTRACT_TEST": "1", "MOSS_EST_STALE_TEST": "1"},
                           text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           check=False, preexec_fn=lambda: resource.setrlimit(
                               resource.RLIMIT_CORE, (0, 0)))
    if stale.returncode == 0 or "source_fingerprint" not in stale.stderr:
        raise SystemExit("mutating source did not trip the shadow staleness assertion")
    print("phase27 EST focused checks passed (4 syntax dumps; raw places; copy/rebuild; off builds expression arena; stale guard)")

if __name__ == "__main__":
    main()
