#!/usr/bin/env python3
"""Focused EST expression dump and off-allocation regression checks."""
import os
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
COMPILER = pathlib.Path(sys.argv[1]).resolve()
CASES = {
    "message_expression.moss": ("message#", '"Run"', 'literal#'),
    "bitwise_shift.moss": ('binary#', '"<<"', '"&"'),
    "numeric_member.moss": ('member#', '"to_string"', 'literal#'),
}

def run(path, mode, **extra):
    env = os.environ.copy()
    env.update(extra)
    env["MOSS_EST"] = mode
    return subprocess.run([str(COMPILER), "check", str(path), "--json"], cwd=ROOT,
                          env=env, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=False)

def main():
    for name, pieces in CASES.items():
        path = ROOT / "tests/phase27/est" / name
        result = run(path, "shadow", MOSS_EST_DUMP="1")
        if result.returncode != 0:
            raise SystemExit(f"shadow check failed for {name}: {result.stdout}{result.stderr}")
        dumps = [line for line in result.stderr.splitlines() if line.startswith("MOSS_EST_DUMP ")]
        if not dumps or not all(piece in "\n".join(dumps) for piece in pieces):
            raise SystemExit(f"missing expected tree fields for {name}: {dumps}")
        for dump in dumps:
            ids = [int(x) for x in re.findall(r"#(\d+)", dump)]
            if len(ids) != len(set(ids)):
                raise SystemExit(f"ExprId reused within one occurrence in {name}: {ids}")
    probe = ROOT / "tests/phase27/est/bitwise_shift.moss"
    result = run(probe, "off", MOSS_EST_STATS="1")
    if result.returncode != 0 or "MOSS_EST_STATS arena_size=0 handle_count=0" not in result.stderr:
        raise SystemExit(f"off allocated EST storage: {result.stdout}{result.stderr}")
    raw = ROOT / "tests/phase27/est/raw_places.moss"
    result = run(raw, "shadow", MOSS_EST_DUMP="1")
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
    print("phase27 EST focused checks passed (3 dumps; raw places; off arena size 0)")

if __name__ == "__main__":
    main()
