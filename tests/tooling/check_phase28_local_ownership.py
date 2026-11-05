#!/usr/bin/env python3
"""Exercise the Phase 28 checker-owned EST local ownership slice."""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/phase28/team_a_ownership"


def check(compiler: str, fixture: str, expected_effect: int | None = None,
          expected_error: str | None = None, hooks: tuple[str, ...] = (),
          expected_records: tuple[int, int, int] | None = None,
          expected_unsupported: tuple[int, int] | None = None,
          expected_join_predecessors: int = 0,
          expected_rebinding: bool = False,
          expected_fresh_owned_rebind: bool = False) -> None:
    env = os.environ.copy()
    env["MOSS_EST_FACTS_DUMP"] = "1"
    env.pop("MOSS_TEST_DROP_OWNERSHIP_FACT", None)
    env.pop("MOSS_TEST_PERTURB_OWNERSHIP_FACT", None)
    env.pop("MOSS_TEST_DROP_FRESH_OWNERSHIP_FACT", None)
    env.pop("MOSS_TEST_PERTURB_FRESH_OWNERSHIP_FACT", None)
    for hook in hooks:
        env[hook] = "1"
    proc = subprocess.run(
        [compiler, "check", str(FIXTURES / fixture), "--json"],
        cwd=ROOT, text=True, capture_output=True, env=env, check=False,
    )
    try:
        result = json.loads(proc.stdout)
    except json.JSONDecodeError as error:
        raise AssertionError(
            f"{fixture}: invalid structured checker output: {proc.stdout!r}\n"
            f"stderr: {proc.stderr}") from error
    error = result.get("error", {}).get("code")
    if expected_error:
        if result.get("ok") or error != expected_error:
            raise AssertionError(
                f"{fixture}: expected {expected_error}, got {result!r}")
        return
    if not result.get("ok"):
        raise AssertionError(f"{fixture}: expected accepted, got {result!r}")
    if expected_effect is not None:
        facts = re.findall(
            r"MOSS_EST_OWNERSHIP_FACT .*? expr=(\d+) .*? access=(\d+) "
            r"availability=(\d+)", proc.stderr)
        expected = (expected_effect, 1)  # 1 == Present (0 == NotRequired)
        if not any((int(access), int(availability)) == expected
                   for _, access, availability in facts):
            raise AssertionError(
                f"{fixture}: expected EST ownership fact {expected}, "
                f"found {facts!r}; stderr: {proc.stderr}")
    records = re.findall(
        r"MOSS_EST_OWNERSHIP_RECORD source_kind=(\d+) .*? access=(\d+) "
        r"inference_status=(\d+) unsupported_reason=(\d+) availability=(\d+)",
        proc.stderr)
    if expected_records:
        source_kind, effect, count = expected_records
        matches = [row for row in records
                   if tuple(map(int, row))[0:3] ==
                   (source_kind, effect, 0) and int(row[4]) == 1]
        if len(matches) < count:
            raise AssertionError(
                f"{fixture}: expected {count} inferred source-kind {source_kind} "
                f"records with effect {effect}, found {records!r}")
    if expected_unsupported:
        source_kind, reason = expected_unsupported
        if not any(int(row[0]) == source_kind and int(row[2]) == 1 and
                   int(row[3]) == reason and int(row[4]) == 0
                   for row in records):
            raise AssertionError(
                f"{fixture}: expected unsupported source-kind {source_kind} "
                f"reason {reason}, found {records!r}")
    if expected_join_predecessors:
        join_records = re.findall(
            r"MOSS_EST_OWNERSHIP_RECORD source_kind=0 definition=.*? "
            r"join=\"([^\"]+)\" .*? inference_status=0 .*? availability=1",
            proc.stderr)
        if max((join_records.count(identity) for identity in set(join_records)),
               default=0) < expected_join_predecessors:
            raise AssertionError(
                f"{fixture}: expected {expected_join_predecessors} inferred "
                f"predecessor facts sharing a join identity, got {join_records!r}")
    if expected_rebinding:
        definitions = re.findall(
            r'MOSS_EST_OWNERSHIP_FACT definition="([^"]+)" source="[^"]+" '
            r'binding="([^"]+)" killed="([^"]*)" expr=(\d+) '
            r'context="[^"]+" access=(\d+)',
            proc.stderr)
        if len(definitions) != 2 or definitions[0][0] == definitions[1][0]:
            raise AssertionError(
                f"expected two exact assignment definitions, got {definitions!r}")
        if definitions[0][1] != definitions[1][1] or \
                definitions[1][2] != definitions[0][0]:
            raise AssertionError(
                f"rebind did not preserve lexical identity and kill prior definition: "
                f"{definitions!r}")
        if [int(row[4]) for row in definitions] != [0, 2]:
            raise AssertionError(
                f"independent definitions must remain READ then CONSUME: {definitions!r}")
    if expected_fresh_owned_rebind:
        definitions = re.findall(
            r'MOSS_EST_OWNERSHIP_FACT definition="([^"]+)" source="([^"]*)" '
            r'binding="([^"]*)" killed="([^"]*)" expr=(\d+) '
            r'context="([^"]+)" access=(\d+) availability=(\d+) '
            r'source_kind=(\d+) type="([^"]*)"', proc.stderr)
        borrowed = [row for row in definitions
                    if row[1] and int(row[8]) == 0]
        fresh = [row for row in definitions if int(row[8]) == 2]
        if len(borrowed) != 1 or len(fresh) != 1:
            raise AssertionError(
                f"expected one borrowed source and one FreshOwnedValue fact, "
                f"got {definitions!r}; stderr: {proc.stderr}")
        old_definition = borrowed[0]
        new_definition = fresh[0]
        if (new_definition[1] or not new_definition[2] or
                new_definition[3] != old_definition[0] or
                new_definition[2] != old_definition[2] or
                int(new_definition[6]) != 0 or int(new_definition[7]) != 1 or
                not new_definition[9].startswith("type-key-v1:")):
            raise AssertionError(
                "FreshOwnedValue fact did not preserve binding identity, kill "
                f"the exact prior definition, and carry a closed type: {definitions!r}")


def check_take_contract(compiler: str) -> None:
    fixture = FIXTURES / "take_positive.moss"
    env = os.environ.copy()
    env["MOSS_EST_FACTS_DUMP"] = "1"
    env["MOSS_EST_DUMP"] = "1"
    env["MOSS_TEST_VALIDATE_TAKE_CONTRACT"] = "1"
    env.pop("MOSS_TEST_DROP_TAKE_FACT", None)
    env.pop("MOSS_TEST_PERTURB_TAKE_FACT", None)
    proc = subprocess.run([compiler, "check", str(fixture), "--json"],
                          cwd=ROOT, text=True, capture_output=True,
                          env=env, check=False)
    result = json.loads(proc.stdout)
    if not result.get("ok"):
        raise AssertionError(
            f"take positive cases rejected: {result!r}\n{proc.stderr}")
    if len(re.findall(r"\(take#\d+", proc.stderr)) < 10:
        raise AssertionError("parser/EST did not retain take as its own node")
    if not re.search(r'\(call#\d+ \(member#\d+ .*"take"\)', proc.stderr):
        raise AssertionError("ordinary receiver.take(n) was confused with take(place)")
    facts = re.findall(
        r"MOSS_EST_TAKE_FACT arena=(\d+) take=(\d+) place=(\d+) "
        r"statement=(\d+) context=\"([^\"]+)\" specialization=\"([^\"]*)\" "
        r"root=\"([^\"]+)\" binding=\"([^\"]+)\" type=(\S+) "
        r"replacement=(\d+) access=(\d+) transfer=(\d+) exclusive=(\d+) "
        r"projections=(\d+) availability=(\d+)", proc.stderr)
    if len(facts) != 10:
        raise AssertionError(
            f"expected 10 direct, nested, repeated, domain, and raised take facts; got {facts!r}")
    if not any(row[4].startswith("handler:") and row[6] == "store"
               for row in facts):
        raise AssertionError("domain-state take did not retain a handler-root fact")
    if {int(row[9]) for row in facts} != {0, 1, 2, 3}:
        raise AssertionError("take replacement kinds do not cover all four types")
    if any(int(row[10]) != 1 or int(row[11]) != 1 or
           int(row[12]) != 1 or int(row[14]) != 0 for row in facts):
        raise AssertionError(f"take authority/access record incomplete: {facts!r}")
    if not any(int(row[13]) > 1 for row in facts):
        raise AssertionError("nested take projection path was not retained")
    effect_ids = {int(match) for match in re.findall(
        r"MOSS_EST_EFFECT_FACT expr=(\d+) context=\"[^\"]+\" effect=1",
        proc.stderr)}
    if any(int(row[1]) not in effect_ids for row in facts):
        raise AssertionError("take EST occurrences lack checked WRITE effects")

    for name, code in (
        ("take_invalid_local.moss", "TAKE_REQUIRES_FIELD_PLACE"),
        ("take_invalid_element.moss", "TAKE_REQUIRES_FIELD_PLACE"),
        ("take_invalid_record.moss", "TAKE_INELIGIBLE_TERMINAL"),
        ("take_immutable.moss", "IMMUTABLE_MESSAGE_PAYLOAD"),
        ("take_borrow_conflict.moss", "MATCH_READ_BORROW_ACTIVE"),
        ("take_unsupported_context.moss", "TAKE_CONTEXT_UNSUPPORTED"),
    ):
        negative = subprocess.run(
            [compiler, "check", str(FIXTURES / name), "--json"],
            cwd=ROOT, text=True, capture_output=True, check=False)
        negative_result = json.loads(negative.stdout)
        if negative_result.get("ok") or \
                negative_result.get("error", {}).get("code") != code:
            raise AssertionError(
                f"{name}: expected {code}, got {negative_result!r}")

    for hook, message in (
        ("MOSS_TEST_DROP_TAKE_FACT", "required checked take fact missing"),
        ("MOSS_TEST_PERTURB_TAKE_FACT",
         "required checked take fact contradicts requirement"),
        ("MOSS_TEST_PERTURB_TAKE_IDENTITY",
         "required checked take fact contradicts requirement"),
        ("MOSS_TEST_DUPLICATE_TAKE_FACT",
         "duplicate or contradictory checked take facts"),
        ("MOSS_TEST_DUPLICATE_TAKE_REQUIREMENT",
         "duplicate or contradictory required take identity"),
    ):
        hooked = os.environ.copy()
        hooked["MOSS_TEST_VALIDATE_TAKE_CONTRACT"] = "1"
        hooked[hook] = "1"
        rejected = subprocess.run(
            [compiler, "check", str(fixture), "--json"], cwd=ROOT,
            text=True, capture_output=True, env=hooked, check=False)
        rejected_result = json.loads(rejected.stdout)
        if rejected_result.get("ok") or message not in \
                rejected_result.get("error", {}).get("message", ""):
            raise AssertionError(
                f"{hook} did not fail closed as expected: {rejected_result!r}")

    raised = subprocess.run(
        [compiler, "check", str(FIXTURES / "take_before_raise.moss"), "--json"],
        cwd=ROOT, text=True, capture_output=True,
        env={**os.environ, "MOSS_EST_FACTS_DUMP": "1",
             "MOSS_TEST_VALIDATE_TAKE_CONTRACT": "1"}, check=False)
    raised_result = json.loads(raised.stdout)
    if not raised_result.get("ok") or len(re.findall(
            r"MOSS_EST_TAKE_FACT .*? availability=0", raised.stderr)) != 1:
        raise AssertionError(
            f"take before a typed raise lacked its pre-raise fact: {raised_result!r}")


def check_loop_ownership_contract(compiler: str) -> None:
    env = os.environ.copy()
    env["MOSS_EST_FACTS_DUMP"] = "1"
    env["MOSS_TEST_VALIDATE_LOOP_OWNERSHIP_CONTRACT"] = "1"
    fixture = FIXTURES / "loop_reinitialize.moss"
    facts: list[tuple[str, ...]] = []
    for name in ("loop_reinitialize.moss", "for_loop_reinitialize.moss",
                 "loop_borrow_last_use.moss"):
        source = FIXTURES / name
        proc = subprocess.run([compiler, "check", str(source), "--json"],
                              cwd=ROOT, text=True, capture_output=True,
                              env=env, check=False)
        result = json.loads(proc.stdout)
        if proc.returncode != 0 or not result.get("ok"):
            raise AssertionError(
                f"{name} loop fact contract failed: {result!r}\n{proc.stderr}")
        observed = re.findall(
            r'MOSS_EST_LOOP_OWNERSHIP_FACT loop=(\d+) source=(\d+) target=(\d+) '
            r'definition="([^"]+)" binding="([^"]+)" edge=(\d+) access=(\d+) '
            r'availability=(\d+) state=(\d+)', proc.stderr)
        if not observed or {int(row[5]) for row in observed} != {0, 1, 2, 3}:
            raise AssertionError(
                f"{name}: expected entry/backedge/zero/converged facts, got {observed!r}")
        if any(not row[3] or not row[4] or int(row[7]) != 0 or
               int(row[8]) == 3
               for row in observed):
            raise AssertionError(
                f"{name}: loop facts lack exact identity/availability: {observed!r}")
        if name == "loop_reinitialize.moss":
            facts = observed
    zero_exit_facts = [row for row in facts if int(row[5]) == 2]
    backedge_facts = [row for row in facts if int(row[5]) == 1]
    zero_exit_defs = {row[3] for row in zero_exit_facts}
    backedge_defs = {row[3] for row in backedge_facts}
    if any("consumed" in identity or "@13:" in identity
           for identity in zero_exit_defs):
        raise AssertionError(
            f"zero-iteration exit included loop-created definitions: {zero_exit_defs!r}")
    if not any("@13:" in identity for identity in backedge_defs):
        raise AssertionError(
            f"backedge omitted the reinitialized loop-carried definition: {backedge_defs!r}")
    if not any(row[3].endswith(":slot") and int(row[8]) == 0
               for row in zero_exit_facts):
        raise AssertionError(
            f"zero-iteration path did not preserve an available initial value: {zero_exit_facts!r}")
    if not any("@13:" in row[3] and int(row[8]) == 0
               for row in backedge_facts):
        raise AssertionError(
            f"successful loop reinitialization did not restore availability: {backedge_facts!r}")

    for hook in ("MOSS_TEST_DROP_LOOP_OWNERSHIP_FACT",
                 "MOSS_TEST_PERTURB_LOOP_OWNERSHIP_FACT"):
        hooked = {**env, hook: "1"}
        rejected = subprocess.run(
            [compiler, "check", str(fixture), "--json"], cwd=ROOT,
            text=True, capture_output=True, env=hooked, check=False)
        rejected_result = json.loads(rejected.stdout)
        if rejected_result.get("ok") or "loop ownership fact missing or contradictory" not in \
                rejected_result.get("error", {}).get("message", ""):
            raise AssertionError(
                f"{hook} did not fail closed as expected: {rejected_result!r}")

    loop_tmp = ROOT / "tmp/phase28_loop_ownership"
    loop_tmp.mkdir(parents=True, exist_ok=True)
    for name in ("loop_reinitialize.moss", "for_loop_reinitialize.moss",
                 "loop_borrow_last_use.moss"):
        source = FIXTURES / name
        expected = "1\n2" if name == "loop_borrow_last_use.moss" else "4"
        fast = subprocess.run([compiler, "run", "--interp", str(source)],
                              cwd=ROOT, text=True, capture_output=True,
                              check=False)
        if fast.returncode != 0 or fast.stdout.strip() != expected:
            raise AssertionError(
                f"{name}: Fast Debug expected {expected!r}, got {fast.stdout!r} "
                f"{fast.stderr!r}")
        rust = loop_tmp / f"{source.stem}.rs"
        binary = loop_tmp / source.stem
        emitted = subprocess.run([compiler, str(source), "-o", str(rust)],
                                 cwd=ROOT, text=True, capture_output=True,
                                 check=False)
        if emitted.returncode != 0:
            raise AssertionError(f"{name}: native emission failed: {emitted.stderr}")
        compiled = subprocess.run(["rustc", str(rust), "-o", str(binary)],
                                  cwd=ROOT, text=True, capture_output=True,
                                  check=False)
        if compiled.returncode != 0:
            raise AssertionError(f"{name}: rustc failed: {compiled.stderr}")
        native = subprocess.run([str(binary)], cwd=ROOT, text=True,
                                capture_output=True, check=False)
        if native.returncode != 0 or native.stdout.strip() != fast.stdout.strip():
            raise AssertionError(
                f"{name}: Fast Debug/native mismatch: {fast.stdout!r} vs "
                f"{native.stdout!r}; {native.stderr}")




def check_exceptional_ownership_contract(compiler: str) -> None:
    source = FIXTURES / "exceptional_recover_read.moss"
    env = os.environ.copy()
    env["MOSS_EST_FACTS_DUMP"] = "1"
    env["MOSS_TEST_VALIDATE_EXCEPTIONAL_OWNERSHIP_CONTRACT"] = "1"
    proc = subprocess.run([compiler, "check", str(source), "--json"],
                          cwd=ROOT, text=True, capture_output=True,
                          env=env, check=False)
    result = json.loads(proc.stdout)
    if proc.returncode != 0 or not result.get("ok"):
        raise AssertionError(
            f"exceptional recover ownership fact failed: {result!r}\n{proc.stderr}")
    facts = re.findall(
        r'MOSS_EST_EXCEPTIONAL_OWNERSHIP_FACT source=(\d+) recovery=(\d+) '
        r'arena=(\d+) expr=(\d+) slot=([^ ]+) arg=(\d+) '
        r'variant="([^"]+)" '
        r'context="([^"]+)" '
        r'specialization="([^"]*)" provenance="([^"]+)" state=(\d+) '
        r'availability=(\d+) mutations=(\d+) consumes=(\d+) borrows=(\d+)',
        proc.stderr)
    if len(facts) != 1 or facts[0][4] != "RaisedValue" or \
            facts[0][5] != "0" or facts[0][6] != "enum:Failure:Bad" or \
            int(facts[0][2]) == 2**32 - 1 or int(facts[0][3]) == 2**32 - 1 or \
            int(facts[0][10]) < 1 or int(facts[0][11]) != 0 or \
            int(facts[0][14]) < 1:
        raise AssertionError(
            f"expected exact typed variant, EST and state-at-raise fact: {facts!r}")
    records = re.findall(
        r'MOSS_EST_OWNERSHIP_RECORD source_kind=0 definition="[^"]+" '
        r'join="[^"]*" source="[^"]+" expr=\d+ context="[^"]+" '
        r'specialization="[^"]*" statement=\d+ access=(\d+) '
        r'inference_status=(\d+) unsupported_reason=(\d+) availability=(\d+)',
        proc.stderr)
    if not any(tuple(map(int, row)) == (0, 0, 0, 1) for row in records):
        raise AssertionError(
            f"recovery READ did not infer at its source assignment: {records!r}")

    joined_borrows = subprocess.run(
        [compiler, "check", str(FIXTURES /
         "exceptional_borrow_join_provenance.moss"), "--json"],
        cwd=ROOT, text=True, capture_output=True, env=env, check=False)
    joined_result = json.loads(joined_borrows.stdout)
    joined_counts = [int(value) for value in re.findall(
        r'MOSS_EST_EXCEPTIONAL_OWNERSHIP_FACT .*? borrows=(\d+)',
        joined_borrows.stderr)]
    if not joined_result.get("ok") or not joined_counts or max(joined_counts) < 2:
        raise AssertionError(
            f"recovery join omitted one possible borrow origin: "
            f"{joined_result!r} {joined_counts!r}")

    mutated = subprocess.run(
        [compiler, "check", str(FIXTURES / "exceptional_mutation_preserved.moss"),
         "--json"], cwd=ROOT, text=True, capture_output=True, env=env,
        check=False)
    mutated_result = json.loads(mutated.stdout)
    if not mutated_result.get("ok") or not re.search(
            r'MOSS_EST_EXCEPTIONAL_OWNERSHIP_FACT .*? mutations=[1-9]',
            mutated.stderr):
        raise AssertionError(
            f"completed take mutation did not survive into recover fact: "
            f"{mutated_result!r}\\n{mutated.stderr}")

    negative = subprocess.run(
        [compiler, "check", str(FIXTURES / "exceptional_recover_consume.moss"),
         "--json"], cwd=ROOT, text=True, capture_output=True, env=env,
        check=False)
    rejected = json.loads(negative.stdout)
    if rejected.get("ok") or rejected.get("error", {}).get("code") != \
            "OWNERSHIP_USE_AFTER_CONSUME":
        raise AssertionError(
            f"recovery consume failed to invalidate its source: {rejected!r}")

    rebinding = subprocess.run(
        [compiler, "check", str(FIXTURES /
         "exceptional_rebind_after_last_borrow.moss"), "--json"],
        cwd=ROOT, text=True, capture_output=True, env=env, check=False)
    if not json.loads(rebinding.stdout).get("ok"):
        raise AssertionError(
            f"rebind after recover borrow's final use was rejected: {rebinding.stdout}")

    echo_arguments = subprocess.run(
        [compiler, "check", str(FIXTURES /
         "exceptional_echo_argument_origin.moss"), "--json"],
        cwd=ROOT, text=True, capture_output=True, env=env, check=False)
    echo_result = json.loads(echo_arguments.stdout)
    echo_facts = re.findall(
        r'MOSS_EST_EXCEPTIONAL_OWNERSHIP_FACT .*? slot=Argument arg=(\d+) '
        r'variant="enum:Failure:Bad"', echo_arguments.stderr)
    if not echo_result.get("ok") or echo_facts != ["1"]:
        raise AssertionError(
            f"raising second echo argument lost exact EST identity: "
            f"{echo_result!r} {echo_facts!r}\n{echo_arguments.stderr}")
    message_arguments = subprocess.run(
        [compiler, "check", str(FIXTURES /
         "exceptional_message_nested_argument.moss"), "--json"],
        cwd=ROOT, text=True, capture_output=True, env=env, check=False)
    message_result = json.loads(message_arguments.stdout)
    message_facts = re.findall(
        r'MOSS_EST_EXCEPTIONAL_OWNERSHIP_FACT .*? arena=(\d+) expr=(\d+) '
        r'slot=Argument arg=(\d+) variant="enum:Failure:Bad"',
        message_arguments.stderr)
    raise_call_nodes = re.findall(
        r'MOSS_EST_FACT expr=(\d+) context="main" .*?'
        r'target="callable-v1:[^"]*::main:fn:fail_value"',
        message_arguments.stderr)
    if not message_result.get("ok") or len(message_facts) != 1 or \
            message_facts[0][2] != "0" or len(raise_call_nodes) != 1 or \
            message_facts[0][1] != raise_call_nodes[0]:
        raise AssertionError(
            f"nested raising message argument lost exact EST occurrence: "
            f"{message_result!r} {message_facts!r} {raise_call_nodes!r}\n"
            f"{message_arguments.stderr}")
    pipeline_callback = subprocess.run(
        [compiler, "check", str(FIXTURES /
         "exceptional_pipeline_callback.moss"), "--json"],
        cwd=ROOT, text=True, capture_output=True, env=env, check=False)
    pipeline_result = json.loads(pipeline_callback.stdout)
    pipeline_facts = re.findall(
        r'MOSS_EST_EXCEPTIONAL_OWNERSHIP_FACT .*? arena=(\d+) expr=(\d+) '
        r'slot=RhsValue arg=0 variant="enum:Failure:Bad"',
        pipeline_callback.stderr)
    if not pipeline_result.get("ok") or len(pipeline_facts) != 1 or \
            int(pipeline_facts[0][0]) == 2**32 - 1 or \
            int(pipeline_facts[0][1]) == 2**32 - 1:
        raise AssertionError(
            f"raising checked pipeline callback lost exact stage EST identity: "
            f"{pipeline_result!r} {pipeline_facts!r}\n{pipeline_callback.stderr}")
    overlapping = subprocess.run(
        [compiler, "check", str(FIXTURES /
         "exceptional_rebind_while_borrowed.moss"), "--json"],
        cwd=ROOT, text=True, capture_output=True, env=env, check=False)
    overlapping_result = json.loads(overlapping.stdout)
    if overlapping_result.get("ok") or overlapping_result.get("error", {}).get(
            "code") != "OWNERSHIP_SOURCE_BORROWED":
        raise AssertionError(
            f"overlapping borrow through recover was not rejected: {overlapping_result!r}")

    aborting = subprocess.run(
        [compiler, "check", str(FIXTURES / "panic_not_recovered.moss"), "--json"],
        cwd=ROOT, text=True, capture_output=True, env=env, check=False)
    aborting_result = json.loads(aborting.stdout)
    if not aborting_result.get("ok") or \
            "MOSS_EST_EXCEPTIONAL_OWNERSHIP_FACT" in aborting.stderr:
        raise AssertionError(
            f"panic-like operation fabricated a typed recover edge: {aborting_result!r}")

    for hook in ("MOSS_TEST_DROP_EXCEPTIONAL_OWNERSHIP_FACT",
                 "MOSS_TEST_PERTURB_EXCEPTIONAL_OWNERSHIP_FACT"):
        corrupted = subprocess.run(
            [compiler, "check", str(source), "--json"], cwd=ROOT,
            text=True, capture_output=True,
            env={**env, hook: "1"}, check=False)
        corrupted_result = json.loads(corrupted.stdout)
        if corrupted_result.get("ok") or "exceptional ownership fact missing or contradictory" not in \
                corrupted_result.get("error", {}).get("message", ""):
            raise AssertionError(
                f"{hook} did not fail closed: {corrupted_result!r}")

def check_collection_outcome_contract(compiler: str) -> None:
    fixtures = (
        (ROOT / "tests/phase28/oracle_r7_005_1/fixtures/oracle_07_catchable_collection_errors.moss",
         "fresh-empty Queue.pop"),
        (ROOT / "tests/phase28/oracle_r7_005_1/fixtures/oracle_53_map_remove_missing.moss",
         "fresh-empty Map.remove"),
    )
    for fixture, operation in fixtures:
        dump_env = os.environ.copy()
        dump_env["MOSS_EST_FACTS_DUMP"] = "1"
        accepted = subprocess.run(
            [compiler, "check", str(fixture), "--json"], cwd=ROOT,
            text=True, capture_output=True, env=dump_env, check=False)
        accepted_result = json.loads(accepted.stdout)
        if not accepted_result.get("ok"):
            raise AssertionError(
                f"{operation} should be accepted with checked Never authority: "
                f"{accepted_result!r}")
        rows = re.findall(
            r'MOSS_COLLECTION_OUTCOME_FACT operation=(\d+) outcome=(\d+) '
            r'receiver="([^"]+)" definition="([^"]+)" expr=(\d+) '
            r'span=(\d+):(\d+) error="([^"]+)" availability=(\d+)',
            accepted.stderr)
        expected_operation = 0 if "Queue.pop" in operation else 1
        if not any(int(row[0]) == expected_operation and int(row[1]) == 0 and
                   row[2] and row[3] and row[4].isdigit() and
                   int(row[6]) > int(row[5]) and int(row[8]) == 0
                   for row in rows):
            raise AssertionError(
                f"{operation}: exact checked AlwaysRaises fact missing: {rows!r}")
        for hook in (
            "MOSS_TEST_DROP_COLLECTION_OUTCOME_FACT",
            "MOSS_TEST_PERTURB_COLLECTION_OUTCOME_FACT",
            "MOSS_TEST_DUPLICATE_COLLECTION_OUTCOME_FACT",
            "MOSS_TEST_DUPLICATE_COLLECTION_OUTCOME_REQUIREMENT",
        ):
            env = os.environ.copy()
            env[hook] = "1"
            rejected = subprocess.run(
                [compiler, "check", str(fixture), "--json"], cwd=ROOT,
                text=True, capture_output=True, env=env, check=False)
            rejected_result = json.loads(rejected.stdout)
            if rejected_result.get("ok") or \
                    rejected_result.get("error", {}).get("code") != "MOSS_INTERNAL_OR_IO_ERROR":
                raise AssertionError(
                    f"{operation} {hook}: expected fail-closed collection outcome authority; "
                    f"got {rejected_result!r}")


def check_match_join_regressions(compiler: str) -> None:
    fixtures = ROOT / "tests/phase28/oracle_r7_005_1/fixtures"
    expected = {
        "oracle_31_join_promoted_consume.moss": None,
        "oracle_32_match_payload_write_borrow.moss": None,
        "oracle_33_match_payload_last_use_overwrite.moss": None,
        "oracle_34_match_payload_overwrite_while_borrowed_rejected.moss":
            "MATCH_READ_BORROW_ACTIVE",
        "oracle_35_match_ordinary_consume_rejected.moss":
            "BORROWED_ENUM_PAYLOAD_ACCESS",
    }
    for fixture, expected_error in expected.items():
        proc = subprocess.run(
            [compiler, "check", str(fixtures / fixture), "--json"],
            cwd=ROOT, text=True, capture_output=True, check=False)
        result = json.loads(proc.stdout)
        error = result.get("error", {}).get("code")
        if expected_error is None and not result.get("ok"):
            raise AssertionError(f"{fixture}: expected acceptance, got {result!r}")
        if expected_error and (result.get("ok") or error != expected_error):
            raise AssertionError(
                f"{fixture}: expected {expected_error}, got {result!r}")
        if fixture.startswith("oracle_35") and "borrow" not in \
                result.get("error", {}).get("message", "").lower():
            raise AssertionError(
                f"{fixture}: expected a borrowed-payload diagnostic, got {result!r}")


def check_match_arm_corruption(compiler: str) -> None:
    fixture = ROOT / "tests/phase28/oracle_r7_005_1/fixtures/oracle_32_match_payload_write_borrow.moss"
    output = ROOT / "tmp/team-a/match-arm-corruption"
    output.mkdir(parents=True, exist_ok=True)
    fields = (
        "case_name", "payload_binding_name", "payload_binding_identity",
        "payload_type", "payload_access", "transfers_ownership",
    )
    modes = {
        "check": [compiler, "check", str(fixture), "--json"],
        "fast": [compiler, "run", "--interp", str(fixture)],
    }
    native_rust = output / "corrupted.rs"
    modes["native"] = [compiler, str(fixture), "-o", str(native_rust)]
    for field in fields:
        for mode, command in modes.items():
            env = os.environ.copy()
            env["MOSS_TEST_PERTURB_MATCH_ARM_FIELD"] = field
            env["MOSS_TEST_VALIDATE_MATCH_CONTRACT"] = "1"
            proc = subprocess.run(
                command, cwd=ROOT, text=True, capture_output=True,
                env=env, check=False)
            combined = proc.stdout + "\n" + proc.stderr
            if proc.returncode == 0 or not (
                    "MOSS_INTERNAL_OR_IO_ERROR" in combined or
                    "required checked match arm fact contradicts requirement" in combined):
                raise AssertionError(
                    f"{mode} accepted perturbed match arm {field}; "
                    f"exit={proc.returncode}\n{combined}")


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_phase28_local_ownership.py COMPILER")
    compiler = sys.argv[1]
    check_match_join_regressions(compiler)
    check_match_arm_corruption(compiler)
    borrowed_state_snapshot = subprocess.run(
        [compiler, "check", str(ROOT / "tests/phase106d1_borrowed_reads.moss"),
         "--json"], cwd=ROOT, text=True, capture_output=True, check=False)
    borrowed_state_result = json.loads(borrowed_state_snapshot.stdout)
    if not borrowed_state_result.get("ok"):
        raise AssertionError(
            "explicit copied domain-state reply should pass checker: "
            f"{borrowed_state_result!r}")
    check(compiler, "internal_never_not_source_type.moss",
          expected_error="UNKNOWN_SYMBOL_OR_TYPE")
    check_collection_outcome_contract(compiler)
    check(compiler, "read_alias.moss", expected_effect=0)
    check(compiler, "write_alias.moss", expected_effect=1)
    check(compiler, "consume_transfer.moss", expected_effect=2)
    check(compiler, "rebinding_definitions.moss",
          expected_records=(0, 2, 1), expected_rebinding=True)
    check(compiler, "rebind_after_borrow.moss", expected_effect=0)
    check(compiler, "callee_read_alias.moss", expected_effect=0)
    check(compiler, "callee_consume_alias.moss",
          expected_error="OWNERSHIP_USE_AFTER_CONSUME")
    check(compiler, "callee_specialized_consume_alias.moss",
          expected_error="OWNERSHIP_USE_AFTER_CONSUME")
    check(compiler, "rebind_rhs_raise.moss")
    check(compiler, "fileio_rebinding.moss",
          expected_error="FILEIO_PINNED_OWNERSHIP")
    check(compiler, "loop_reinitialize.moss")
    check(compiler, "for_loop_reinitialize.moss")
    check(compiler, "loop_zero_iteration_preserves.moss")
    check(compiler, "loop_borrow_last_use.moss")
    check(compiler, "loop_borrow_still_live.moss",
          expected_error="OWNERSHIP_SOURCE_BORROWED")
    check(compiler, "loop_missing_reinitialize.moss",
          expected_error="LOOP_OUTER_BINDING_CONSUMED")
    check(compiler, "for_loop_unused_owner_move.moss",
          expected_error="LOOP_OUTER_BINDING_CONSUMED")
    check_loop_ownership_contract(compiler)
    check_exceptional_ownership_contract(compiler)
    check(compiler, "use_after_move.moss",
          expected_error="OWNERSHIP_USE_AFTER_CONSUME")
    check(compiler, "surviving_field_demand.moss",
          expected_error="MOSS_COMPILE_ERROR")
    check(compiler, "if_else_live_join.moss",
          expected_records=(0, 0, 2), expected_join_predecessors=2)
    check(compiler, "if_else_live_write.moss",
          expected_records=(0, 1, 2), expected_join_predecessors=2)
    check(compiler, "if_else_live_consume.moss",
          expected_records=(0, 2, 2), expected_join_predecessors=2)
    check(compiler, "if_else_consume_after_join.moss",
          expected_error="OWNERSHIP_USE_AFTER_CONSUME")
    check(compiler, "nested_return_consume.moss",
          expected_error="OWNERSHIP_USE_AFTER_CONSUME")
    check(compiler, "copy_field_return.moss")
    check(compiler, "option_field_return_consume.moss",
          expected_error="OWNERSHIP_USE_AFTER_CONSUME")
    check(compiler, "control_flow_unsupported.moss",
          expected_records=(0, 0, 1))
    check(compiler, "vector_read_iteration.moss",
          expected_records=(1, 0, 1))
    check(compiler, "vector_write_iteration.moss",
          expected_records=(1, 1, 1))
    check(compiler, "vector_index_read_iteration.moss",
          expected_records=(1, 0, 1))
    check(compiler, "vector_consume_iteration.moss",
          expected_error="OWNERSHIP_VECTOR_ELEMENT_CONSUME")
    check(compiler, "implicit_method_argument_effect.moss")
    check(compiler, "fresh_owned_rebind.moss",
          expected_fresh_owned_rebind=True)
    check(compiler, "fresh_owned_rebind.moss",
          expected_error="MOSS_INTERNAL_INVARIANT",
          hooks=("MOSS_TEST_DROP_FRESH_OWNERSHIP_FACT",))
    check(compiler, "fresh_owned_rebind.moss",
          expected_error="MOSS_INTERNAL_INVARIANT",
          hooks=("MOSS_TEST_PERTURB_FRESH_OWNERSHIP_FACT",))
    # Slice B: General Ownership Joins and Early Exits
    check(compiler, "nested_branch_read.moss")
    check(compiler, "dead_branch_local.moss")
    check(compiler, "branch_write_join.moss")
    check(compiler, "branch_early_return.moss")
    check(compiler, "branch_early_return_unreachable_excluded.moss")
    check(compiler, "match_reaching_defs.moss")
    check(compiler, "rebind_branches.moss")
    check(compiler, "mixed_join_rejected.moss",
          expected_error="OWNERSHIP_INCOMPATIBLE_JOIN")
    check(compiler, "branch_write_unsatisfied.moss",
          expected_error="OWNERSHIP_JOIN_WRITE_UNSATISFIED")
    check(compiler, "branch_consume_unsatisfied.moss",
          expected_error="OWNERSHIP_INCOMPATIBLE_JOIN")
    check(compiler, "enum_payload_escape_rejected.moss",
          expected_error="BORROWED_ENUM_PAYLOAD_CONSUME")
    check(compiler, "conditional_consume_use.moss",
          expected_error="OWNERSHIP_USE_AFTER_CONSUME")
    check(compiler, "borrow_escapes_scope.moss",
          expected_error="OWNERSHIP_BORROW_ESCAPES_SCOPE")
    check(compiler, "read_alias.moss",
          expected_error="MOSS_INTERNAL_INVARIANT",
          hooks=("MOSS_TEST_DROP_OWNERSHIP_FACT",))
    check(compiler, "read_alias.moss",
          expected_error="MOSS_INTERNAL_INVARIANT",
          hooks=("MOSS_TEST_PERTURB_OWNERSHIP_FACT",))
    check_take_contract(compiler)
    contract = subprocess.run(
        [sys.executable,
         str(ROOT / "tests/tooling/check_phase28_specialized_ownership_contract.py"),
         compiler], cwd=ROOT, text=True, capture_output=True, check=False)
    if contract.returncode:
        raise AssertionError(
            f"specialized ownership contract failed:\n{contract.stdout}\n"
            f"{contract.stderr}")
    print("phase28 ownership facts: PASS (local effects, branch joins, while/for loop fixed points, Vector iteration, negatives, drop/perturb)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
