#!/usr/bin/env python3
"""Focused checker authority and fail-closed tests for Phase 28 oracle paths."""
from __future__ import annotations

import json
import hashlib
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / "tmp/phase28-r7-010-team-a-oracle-followup/authority-facts"
TMP.mkdir(parents=True, exist_ok=True)
MOSS = ROOT / "moss"

RETURN_SOURCE = TMP / "return_value.moss"
RETURN_SOURCE.write_text(
    "type Parcel:\n"
    "  text: String\n\n"
    "fn transfer(value: Parcel) -> Parcel:\n"
    "  return value\n\n"
    "fn main():\n"
    "  parcel = Parcel(text: \"ready\")\n"
    "  result = transfer(parcel)\n"
    "  echo result.text\n",
    encoding="utf-8",
)

NONPRIMITIVE_INDEX_RETURN_SOURCE = TMP / "nonprimitive_index_return_rejected.moss"
NONPRIMITIVE_INDEX_RETURN_SOURCE.write_text(
    "type Resource:\n"
    "  tag: String\n\n"
    "fn first(items: Vector):\n"
    "  return items[0]\n\n"
    "fn main():\n"
    "  items = [Resource(tag = \"x\")]\n"
    "  value = first(items)\n"
    "  echo value.tag\n",
    encoding="utf-8",
)

PROJECTED_READ_MINIMAL_SOURCE = TMP / "projected_read_minimal.moss"
PROJECTED_READ_MINIMAL_SOURCE.write_text(
    "type Bucket:\n"
    "  items: Vector[Int]\n\n"
    "fn main():\n"
    "  var m = Map()\n"
    "  m[\"first\"] = Bucket(items = [1, 2])\n"
    "  echo (m[\"first\"].items |> count)\n",
    encoding="utf-8",
)

PROJECTED_READ_EXPLICIT_COPY_SOURCE = TMP / "projected_read_explicit_copy.moss"
PROJECTED_READ_EXPLICIT_COPY_SOURCE.write_text(
    "type Bucket:\n"
    "  items: Vector[Int]\n\n"
    "fn main():\n"
    "  var m = Map()\n"
    "  m[\"first\"] = Bucket(items = [1, 2])\n"
    "  echo (copy(m[\"first\"].items) |> count)\n",
    encoding="utf-8",
)

PROJECTED_READ_ALIAS_REJECTED_SOURCE = TMP / "projected_read_alias_rejected.moss"
PROJECTED_READ_ALIAS_REJECTED_SOURCE.write_text(
    "type Bucket:\n"
    "  items: Vector[Int]\n\n"
    "fn main():\n"
    "  var m = Map()\n"
    "  m[\"first\"] = Bucket(items = [1, 2])\n"
    "  items = m[\"first\"].items\n"
    "  echo (items |> count)\n",
    encoding="utf-8",
)

CASES = {
    "conditional": ROOT / "tests/phase28/oracle_r7_005_1/fixtures/oracle_07_catchable_collection_errors.moss",
    "return": RETURN_SOURCE,
    "view": ROOT / "tests/phase28/oracle_r7_005_1/fixtures/oracle_38_map_values_borrowed_view.moss",
    "projected_read": ROOT / "tests/phase28/oracle_r7_005_1/fixtures/oracle_20_take_nested_map_vector.moss",
    "return_transfer": ROOT / "tests/phase28/oracle_r7_005_1/fixtures/oracle_13_assignment_consume_return.moss",
    "return_specialization": ROOT / "tests/phase28/oracle_r7_005_1/fixtures/oracle_58_specialization_identity_collision.moss",
}


def run(source: Path, extra: dict[str, str] | None = None,
        mode: str = "check") -> tuple[dict, str]:
    env = os.environ.copy()
    env["MOSS_TEST_VALIDATE_PHASE28_AUTHORITY_FACTS"] = "1"
    env["MOSS_TEST_DUMP_PHASE28_AUTHORITY_FACTS"] = "1"
    if extra:
        env.update(extra)
    if mode == "check":
        command = [str(MOSS), "check", str(source), "--json"]
    elif mode == "interp":
        command = [str(MOSS), "run", "--interp", str(source)]
    elif mode == "native":
        hook_key = hashlib.sha256(json.dumps(extra or {}, sort_keys=True).encode()).hexdigest()[:12]
        output = TMP / f"{source.stem}-{hook_key}.rs"
        output.unlink(missing_ok=True)
        command = [str(MOSS), str(source), "-o", str(output)]
    else:
        raise AssertionError(f"unknown execution mode: {mode}")
    completed = subprocess.run(
        command,
        cwd=ROOT,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    try:
        response = json.loads(completed.stdout) if mode == "check" else {
            "ok": completed.returncode == 0,
            "error": {"message": completed.stdout + completed.stderr},
        }
    except json.JSONDecodeError as error:
        raise AssertionError(f"invalid Moss JSON for {source}: {completed.stdout!r}\n{completed.stderr}") from error
    return response, completed.stderr


def expect_count(source: Path, field: str, minimum: int = 1) -> None:
    response, stderr = run(source)
    if not response.get("ok"):
        raise AssertionError(f"authority baseline rejected {source}: {response.get('error')}\n{stderr}")
    match = re.search(r"PHASE28_AUTHORITY_FACTS ([^\n]+)", stderr)
    if not match:
        raise AssertionError(f"checker did not dump authority counts for {source}: {stderr}")
    counts = dict(item.split("=", 1) for item in match.group(1).split())
    if int(counts.get(field, "0")) < minimum:
        raise AssertionError(f"{source.name}: expected {field}>={minimum}, got {counts}")
    if field == "conditional":
        operation_counts = [int(value) for value in counts["conditional_ops"].split(",")]
        if len(operation_counts) != 4 or any(value == 0 for value in operation_counts):
            raise AssertionError(
                f"{source.name}: expected checked Map index read/write-through, get, and remove facts; got {operation_counts}"
            )
    print(f"PASS {field}: {source.name} ({counts[field]})")


def expect_diagnostic(source: Path, expected_code: str) -> None:
    response, stderr = run(source)
    error = response.get("error") or {}
    if response.get("ok") or error.get("code") != expected_code:
        raise AssertionError(
            f"{source.name}: expected {expected_code}, got {error}\n{stderr}"
        )
    print(f"PASS rejected return diagnostic: {source.name} ({expected_code})")


def expect_projected_read_absent(source: Path, native_success: bool) -> None:
    response, stderr = run(source)
    if not response.get("ok"):
        raise AssertionError(f"unsupported projected-read baseline rejected {source}: {response.get('error')}\n{stderr}")
    match = re.search(r"PHASE28_AUTHORITY_FACTS ([^\n]+)", stderr)
    counts = dict(item.split("=", 1) for item in match.group(1).split()) if match else {}
    if counts.get("projected_read") != "0":
        raise AssertionError(f"unsupported shape unexpectedly received projected-read authority: {counts}")
    if native_success:
        output, native_stderr = run(source, mode="native")
        if not output.get("ok"):
            raise AssertionError(f"explicit-copy unsupported shape failed native generation: {output}\n{native_stderr}")
        hook_key = hashlib.sha256(json.dumps({}, sort_keys=True).encode()).hexdigest()[:12]
        native_artifact = TMP / f"{source.stem}-{hook_key}.rs"
        native_artifact.unlink(missing_ok=True)
    print(f"PASS projected-read authority absent: {source.name}")


def expect_specialized_returns(source: Path) -> None:
    response, stderr = run(source, {"MOSS_EST_FACTS_DUMP": "1"})
    if not response.get("ok"):
        raise AssertionError(f"specialized return authority rejected {source}: {response.get('error')}\n{stderr}")
    match = re.search(r"PHASE28_AUTHORITY_FACTS ([^\n]+)", stderr)
    counts = dict(item.split("=", 1) for item in match.group(1).split()) if match else {}
    if int(counts.get("return", "0")) != 2 or int(counts.get("return_specializations", "0")) != 2:
        raise AssertionError(f"expected distinct checker return specialization identities for {source.name}, got {counts}")
    if set(counts.get("return_types", "").split(",")) != {"First", "Second"}:
        raise AssertionError(f"expected closed First/Second return facts for {source.name}, got {counts}")
    effect_rows = re.findall(
        r"PHASE28_RETURN_EFFECT specialization=([^ ]+) types=([^ ]+) effects=([^\n]+)",
        stderr,
    )
    specialized = {types.rstrip(","): effects.rstrip(",") for _, types, effects in effect_rows}
    if specialized != {"First": "2", "Second": "2"}:
        raise AssertionError(
            f"expected exact Consume(2) effect for both concrete return specializations, got {specialized}"
        )
    return_fact_rows = [
        dict(re.findall(r'(\w+)=([^ ]+)', row))
        for row in re.findall(r"PHASE28_RETURN_FACT ([^\n]+)", stderr)
    ]
    if len(return_fact_rows) != 2:
        raise AssertionError(
            f"expected two exact specialized ReturnValue facts, got {return_fact_rows}"
        )
    by_type = {row.get("type"): row for row in return_fact_rows}
    if set(by_type) != {"First", "Second"}:
        raise AssertionError(f"wrong specialized return types: {by_type}")
    if len({row.get("specialization") for row in return_fact_rows}) != 2:
        raise AssertionError(f"specialized return identities collided: {return_fact_rows}")
    if any(row.get("est_context") != "fn:inspect" or
           row.get("context") != f"fn:inspect<{row.get('type')}>" or
           row.get("access") != "2" or row.get("action") != "0"
           for row in return_fact_rows):
        raise AssertionError(
            f"specialized ReturnValue fact has incorrect context/access: {return_fact_rows}"
        )
    if len({(row.get("arena"), row.get("expr"), row.get("statement"))
            for row in return_fact_rows}) != 1:
        raise AssertionError(
            f"First/Second facts do not refer to the same checked return occurrence: {return_fact_rows}"
        )
    generic_expr_ids = set(re.findall(
        r'MOSS_EST_FACT expr=(\d+) context="fn:inspect" type="[^\"]+"', stderr
    ))
    if by_type["First"].get("expr") not in generic_expr_ids:
        raise AssertionError(
            f"specialized return ExprId lacks generic EST readback: {generic_expr_ids}, {by_type}"
        )
    print(f"PASS specialized return identities/types: {source.name} ({counts['return_types']})")


def expect_closed(source: Path, hook: str) -> None:
    for mode in ("check", "interp", "native"):
        response, stderr = run(source, {hook: "1"}, mode)
        error = response.get("error") or {}
        if response.get("ok") or "fact" not in str(error).lower():
            raise AssertionError(f"{hook} did not fail closed in {mode} for {source.name}: {response}\n{stderr}")
        if mode == "native":
            hook_key = hashlib.sha256(json.dumps({hook: "1"}, sort_keys=True).encode()).hexdigest()[:12]
            output = TMP / f"{source.stem}-{hook_key}.rs"
            if output.exists():
                raise AssertionError(f"{hook} emitted Rust artifact despite failure: {output}")
    print(f"PASS {hook}: check/interpreter/native generation")


def main() -> int:
    expect_count(CASES["conditional"], "conditional")
    expect_count(CASES["return"], "return")
    expect_count(CASES["return_transfer"], "return")
    expect_specialized_returns(CASES["return_specialization"])
    expect_count(CASES["view"], "view", 2)
    expect_count(CASES["projected_read"], "projected_read")
    expect_count(PROJECTED_READ_MINIMAL_SOURCE, "projected_read")
    expect_projected_read_absent(PROJECTED_READ_EXPLICIT_COPY_SOURCE, native_success=False)
    expect_diagnostic(
        NONPRIMITIVE_INDEX_RETURN_SOURCE,
        "OWNERSHIP_COLLECTION_ELEMENT_CONSUME",
    )
    expect_diagnostic(
        PROJECTED_READ_ALIAS_REJECTED_SOURCE,
        "OWNERSHIP_ILLEGAL_CONSUME",
    )

    for source, hook in (
        (CASES["conditional"], "MOSS_TEST_DROP_CONDITIONAL_COLLECTION_ERROR_FACT"),
        (CASES["conditional"], "MOSS_TEST_PERTURB_CONDITIONAL_COLLECTION_ERROR_FACT"),
        (CASES["conditional"], "MOSS_TEST_DUPLICATE_CONDITIONAL_COLLECTION_ERROR_FACT"),
        (CASES["return"], "MOSS_TEST_DROP_RETURN_VALUE_FACT"),
        (CASES["return"], "MOSS_TEST_PERTURB_RETURN_VALUE_FACT"),
        (CASES["return"], "MOSS_TEST_DUPLICATE_RETURN_VALUE_FACT"),
        (CASES["return_specialization"], "MOSS_TEST_PERTURB_RETURN_VALUE_EST_FACT_CONTEXT"),
        (CASES["view"], "MOSS_TEST_DROP_COLLECTION_VIEW_FACT"),
        (CASES["view"], "MOSS_TEST_PERTURB_COLLECTION_VIEW_FACT"),
        (CASES["view"], "MOSS_TEST_DUPLICATE_COLLECTION_VIEW_FACT"),
        (CASES["projected_read"], "MOSS_TEST_DROP_PROJECTED_COLLECTION_READ_FACT"),
        (CASES["projected_read"], "MOSS_TEST_PERTURB_PROJECTED_COLLECTION_READ_FACT"),
        (CASES["projected_read"], "MOSS_TEST_DUPLICATE_PROJECTED_COLLECTION_READ_FACT"),
        (CASES["projected_read"], "MOSS_TEST_PERTURB_PROJECTED_MAP_INDEX_OCCURRENCE"),
        (CASES["projected_read"], "MOSS_TEST_PERTURB_PROJECTED_SPECIALIZATION_IDENTITY"),
        (CASES["projected_read"], "MOSS_TEST_PERTURB_PROJECTED_COLLECTION_TYPE"),
        (CASES["projected_read"], "MOSS_TEST_DROP_PROJECTED_PROVENANCE"),
        (CASES["projected_read"], "MOSS_TEST_PERTURB_PROJECTED_PROVENANCE"),
        (CASES["projected_read"], "MOSS_TEST_PERTURB_PROJECTED_MODULE"),
        (CASES["projected_read"], "MOSS_TEST_CLEAR_PROJECTED_MODULE_BOTH"),
    ):
        expect_closed(source, hook)
    print("Phase 28 oracle authority facts: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"Phase 28 oracle authority facts: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
