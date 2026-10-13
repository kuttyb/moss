#!/usr/bin/env python3
"""Differential tests for moss.json and moss.csv against Python stdlib.

Uses a margo package project to run tests via moss.json and moss.csv.
Runs seeded adversarial and well-formed inputs through both the Moss
SDK and Python's json/csv modules.
Performs semantic recursive AST comparisons for JSON and cell-by-cell
matching for CSV across 100+ cases.

Usage:
    python3 tests/differential/run_differential_tests.py [--verbose]

Exit code 0 = all differential tests pass.
"""

import csv
import io
import json
import os
import random
import shutil
import subprocess
import sys
import textwrap
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MARGO = ROOT / "margo"
WORK = ROOT / "tmp" / "differential-harness"
VERBOSE = "--verbose" in sys.argv
ENV = os.environ.copy()
ENV.update(
    MOSS=str(ROOT / "moss"),
    TMPDIR=str(ROOT / "tmp"),
    MARGO_HOME=str(ROOT / "tmp" / "margo-diff-harness"),
)


def setup_harness():
    if WORK.exists():
        shutil.rmtree(WORK)
    (WORK / "src").mkdir(parents=True)
    (WORK / "Moss.toml").write_text(
        f'[package]\nname = "diff_harness"\nversion = "0.1.0"\n\n'
        f'[build]\nsource = "src"\n\n[dependencies]\n'
        f'"moss.core" = {{ path = "{ROOT / "sdk" / "foundations" / "core"}" }}\n'
        f'"moss.text" = {{ path = "{ROOT / "sdk" / "text"}" }}\n'
        f'"moss.parse" = {{ path = "{ROOT / "sdk" / "parse"}" }}\n'
        f'"moss.fmt" = {{ path = "{ROOT / "sdk" / "fmt"}" }}\n'
        f'"moss.json" = {{ path = "{ROOT / "sdk" / "json"}" }}\n'
        f'"moss.csv" = {{ path = "{ROOT / "sdk" / "csv"}" }}\n',
        encoding="utf-8"
    )


def run_moss_probe(body: str) -> str:
    main_file = WORK / "src" / "main.moss"
    main_file.write_text(body, encoding="utf-8")
    result = subprocess.run(
        [str(MARGO), "debug"],
        cwd=WORK,
        capture_output=True,
        text=True,
        env=ENV,
    )
    if result.returncode != 0:
        return "__MOSS_ERROR__"
    return result.stdout.strip()


def moss_json_parse_ast(json_text: str):
    escaped = json_text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r")
    src = textwrap.dedent('''\
        import core
        import text
        import parse
        import fmt
        import json

        fn dump_doc_ast(doc: json.JsonDoc) -> String:
          var parts = Vector[String]()
          n = doc.kinds |> count
          parts.push("ROOT:" + fmt.format_int(doc.root) + ";NODES:" + fmt.format_int(n) + ";")
          var i = 0
          while i < n:
            k = json.node_kind(doc, i)
            parts.push("N" + fmt.format_int(i) + ":K" + fmt.format_int(k) + ":")
            if k == json.kind_null():
              parts.push("null;")
            if k == json.kind_bool():
              if json.get_bool(doc, i):
                parts.push("true;")
              else:
                parts.push("false;")
            if k == json.kind_number():
              parts.push(json.get_number_str(doc, i) + ";")
            if k == json.kind_string():
              parts.push(json.get_string(doc, i) + ";")
            if k == json.kind_array():
              alen = json.array_length(doc, i)
              parts.push("LEN" + fmt.format_int(alen) + ":[")
              var a = 0
              while a < alen:
                if a > 0:
                  parts.push(",")
                child_id = json.array_get(doc, i, a)
                parts.push(fmt.format_int(child_id))
                a = a + 1
              parts.push("];")
            if k == json.kind_object():
              olen = json.object_length(doc, i)
              parts.push("LEN" + fmt.format_int(olen) + ":{")
              var o = 0
              while o < olen:
                if o > 0:
                  parts.push(",")
                key = json.object_key(doc, i, o)
                val_id = json.object_val(doc, i, o)
                parts.push(key + "=" + fmt.format_int(val_id))
                o = o + 1
              parts.push("};")
            i = i + 1
          return "".join(parts)

        fn main():
          try:
            s = "__JSON_PAYLOAD__"
            doc = json.parse(s)
            echo dump_doc_ast(doc)
            echo "STRINGIFY:" + json.stringify(doc)
          recover:
            echo "__PARSE_ERROR__"
    ''').replace("__JSON_PAYLOAD__", escaped)
    out = run_moss_probe(src)
    if out == "__MOSS_ERROR__" or out == "__PARSE_ERROR__":
        return None
    stringify_marker = "\nSTRINGIFY:"
    if stringify_marker in out:
        ast_part, stringify_part = out.split(stringify_marker, 1)
        ast_line = ast_part.strip()
        stringify_line = stringify_part.strip()
    else:
        ast_line = out.strip()
        stringify_line = ""
    return parse_moss_json_ast_output(ast_line, stringify_line)


def parse_moss_json_ast_output(ast_text: str, stringify: str):
    if not ast_text.startswith("ROOT:"):
        return None
    try:
        parts = ast_text.split(";")
        meta = {}
        nodes = {}
        for part in parts:
            if not part:
                continue
            if part.startswith("ROOT:"):
                meta["root"] = int(part.split(":")[1])
            elif part.startswith("NODES:"):
                meta["nodes"] = int(part.split(":")[1])
            elif part.startswith("N"):
                # N0:K4:LEN2:[1,3]
                subparts = part.split(":", 2)
                node_id = int(subparts[0][1:])
                kind = int(subparts[1][1:])
                val_data = subparts[2] if len(subparts) > 2 else ""
                nodes[node_id] = (kind, val_data)

        def build_py_val(node_id):
            kind, val_data = nodes[node_id]
            if kind == 0:  # null
                return None
            elif kind == 1:  # bool
                return val_data == "true"
            elif kind == 2:  # number
                if "." in val_data or "e" in val_data or "E" in val_data:
                    return float(val_data)
                return int(val_data)
            elif kind == 3:  # string
                return val_data
            elif kind == 4:  # array
                # LEN2:[1,3]
                inner = val_data[val_data.index("[") + 1 : val_data.rindex("]")]
                if not inner:
                    return []
                child_ids = [int(x) for x in inner.split(",")]
                return [build_py_val(cid) for cid in child_ids]
            elif kind == 5:  # object
                # LEN1:{k=1}
                inner = val_data[val_data.index("{") + 1 : val_data.rindex("}")]
                if not inner:
                    return {}
                res = {}
                pairs = inner.split(",")
                for pair in pairs:
                    k, v_str = pair.split("=", 1)
                    res[k] = build_py_val(int(v_str))
                return res
            return None

        logical_val = build_py_val(meta["root"])
        return {"val": logical_val, "stringify": stringify}
    except Exception as e:
        if VERBOSE:
            print("Failed to decode Moss AST:", e, "raw:", ast_text)
        return None


def moss_csv_parse_rows(csv_text: str, delimiter: str = ","):
    escaped = csv_text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")
    src = textwrap.dedent('''\
        import core
        import text
        import parse
        import fmt
        import csv

        fn dump_csv_ast(rows: Vector[Vector[String]]) -> String:
          var parts = Vector[String]()
          num_rows = rows |> count
          parts.push("ROWS:" + fmt.format_int(num_rows) + ";")
          var r = 0
          while r < num_rows:
            row = rows[r]
            num_cols = row |> count
            parts.push("R" + fmt.format_int(r) + ":COLS" + fmt.format_int(num_cols) + ":[")
            var c = 0
            while c < num_cols:
              if c > 0:
                parts.push("|")
              parts.push(row[c])
              c = c + 1
            parts.push("];")
            r = r + 1
          return "".join(parts)

        fn main():
          try:
            s = "__CSV_PAYLOAD__"
            rows = csv.read_all(s, "__DELIM__")
            echo dump_csv_ast(rows)
            echo "WRITTEN:" + csv.write_all_lf(rows, "__DELIM__")
          recover:
            echo "__CSV_ERROR__"
    ''').replace("__CSV_PAYLOAD__", escaped).replace("__DELIM__", delimiter)
    out = run_moss_probe(src)
    if out == "__MOSS_ERROR__" or out == "__CSV_ERROR__":
        return None
    lines = out.split("\n")
    ast_line = lines[0].strip()
    written = "\n".join(lines[1:])
    if written.startswith("WRITTEN:"):
        written = written[len("WRITTEN:"):]
    return parse_moss_csv_ast_output(ast_line), written


def parse_moss_csv_ast_output(ast_text: str):
    if not ast_text.startswith("ROWS:"):
        return None
    try:
        parts = ast_text.split(";")
        rows = []
        for part in parts:
            if not part or part.startswith("ROWS:"):
                continue
            if part.startswith("R"):
                # R0:COLS2:[a|b]
                sub = part.split(":[", 1)[1]
                inner = sub[:-1]  # remove trailing ']'
                if not inner:
                    rows.append([])
                else:
                    rows.append(inner.split("|"))
        return rows
    except Exception as e:
        if VERBOSE:
            print("Failed to decode CSV AST:", e, "raw:", ast_text)
        return None


def generate_valid_json(rng: random.Random, depth: int = 0) -> str:
    if depth > 3:
        choices = ["null", "true", "false", str(rng.randint(-999, 999))]
        return rng.choice(choices)
    kind = rng.choice(["null", "bool", "int", "string", "array", "object"])
    if kind == "null":
        return "null"
    elif kind == "bool":
        return "true" if rng.random() > 0.5 else "false"
    elif kind == "int":
        return str(rng.randint(-9999, 9999))
    elif kind == "string":
        chars = "abcdefghijklmnopqrstuvwxyz_0123"
        return '"' + "".join(rng.choice(chars) for _ in range(rng.randint(0, 8))) + '"'
    elif kind == "array":
        n = rng.randint(0, 3)
        children = [generate_valid_json(rng, depth + 1) for _ in range(n)]
        return "[" + ",".join(children) + "]"
    else:
        n = rng.randint(0, 3)
        keys = set()
        pairs = []
        for _ in range(n):
            key = "k" + str(rng.randint(0, 99))
            if key not in keys:
                keys.add(key)
                val = generate_valid_json(rng, depth + 1)
                pairs.append(f'"{key}":{val}')
        return "{" + ",".join(pairs) + "}"


INVALID_JSON_CASES = [
    "",
    "{",
    "}",
    "[",
    "]",
    "{,}",
    "[1,]",
    "true extra",
    '"unclosed',
    "1.2.3",
    "[1, 2, 3",
    '{"key"}',
    '{"key": }',
]

VALID_CSV_CASES = [
    ("a,b,c\n1,2,3\n", [["a", "b", "c"], ["1", "2", "3"]]),
    ('"quoted","field"\n', [["quoted", "field"]]),
    ('"embedded,comma",normal\n', [["embedded,comma", "normal"]]),
    ('"double""quote",ok\n', [["double\"quote", "ok"]]),
    ("single\n", [["single"]]),
    ("col1,col2,col3\n1,,3\n", [["col1", "col2", "col3"], ["1", "", "3"]]),
]

INVALID_CSV_CASES = [
    '"unclosed',
    '"trailing"garbage',
    'ab"cd',
]


def compare_json_semantic(py_val, mo_val) -> bool:
    if type(py_val) != type(mo_val):
        # Allow int and float comparison if equal numeric value
        if isinstance(py_val, (int, float)) and isinstance(mo_val, (int, float)):
            return py_val == mo_val
        return False
    if isinstance(py_val, dict):
        if set(py_val.keys()) != set(mo_val.keys()):
            return False
        return all(compare_json_semantic(py_val[k], mo_val[k]) for k in py_val)
    elif isinstance(py_val, list):
        if len(py_val) != len(mo_val):
            return False
        return all(compare_json_semantic(p, m) for p, m in zip(py_val, mo_val))
    else:
        return py_val == mo_val


def run_tests() -> int:
    setup_harness()
    failures = 0
    total = 0
    rng = random.Random(42)

    # --- 1. JSON Semantic differential: 60 generated valid documents ---
    print("=== JSON semantic differential (60 valid seeded random documents) ===")
    for i in range(60):
        j_text = generate_valid_json(rng)
        py_val = json.loads(j_text)
        mo_res = moss_json_parse_ast(j_text)
        total += 1
        if mo_res is None:
            failures += 1
            print(f"  FAIL [{i}] Moss failed to parse valid JSON: {j_text[:60]!r}")
        else:
            mo_val = mo_res["val"]
            if not compare_json_semantic(py_val, mo_val):
                failures += 1
                print(f"  FAIL [{i}] Semantic mismatch:\n    Py:   {py_val}\n    Moss: {mo_val}\n    Src:  {j_text[:60]!r}")
            else:
                # Round-trip verification: re-parse serialized text
                re_py = json.loads(mo_res["stringify"])
                if not compare_json_semantic(py_val, re_py):
                    failures += 1
                    print(f"  FAIL [{i}] Round-trip serialization mismatch")
                elif VERBOSE:
                    print(f"  ok [{i}] matches Py {py_val}")

    # --- 2. JSON Structural Counterexamples and Edge Cases (20 cases) ---
    print("=== JSON structural counterexamples and edge cases ===")
    edge_cases = [
        "[[1], 2]",
        '{"a": {"x": 1}, "b": 2}',
        "[[1, 2], [3, 4], 5]",
        '{"a": [1, 2], "b": {"x": 3}, "c": 4}',
        "[]",
        "{}",
        "[[], [[]], [[], []]]",
        '{"empty_arr": [], "empty_obj": {}}',
        "[0, 1, -1, 42, -9223372036854775807]",
        '["", "hello", "line\\nbreak", "tab\\tspace"]',
        '{"nested": {"nested": {"nested": [100]}}}',
    ]
    for i, ec in enumerate(edge_cases):
        py_val = json.loads(ec)
        mo_res = moss_json_parse_ast(ec)
        total += 1
        if mo_res is None:
            failures += 1
            print(f"  FAIL edge [{i}] Moss failed to parse: {ec!r}")
        else:
            mo_val = mo_res["val"]
            if not compare_json_semantic(py_val, mo_val):
                failures += 1
                print(f"  FAIL edge [{i}] mismatch:\n    Py:   {py_val}\n    Moss: {mo_val}")
            elif VERBOSE:
                print(f"  ok edge [{i}] matches Py {py_val}")

    # --- 3. JSON validate differential: invalid inputs (13 cases) ---
    print("=== JSON validate differential (invalid inputs) ===")
    for i, j in enumerate(INVALID_JSON_CASES):
        mo_res = moss_json_parse_ast(j)
        total += 1
        if mo_res is not None:
            failures += 1
            print(f"  FAIL invalid [{i}] Moss accepted invalid JSON: {j!r}")
        elif VERBOSE:
            print(f"  ok invalid [{i}] correctly rejected")

    # --- 4. CSV semantic differential: valid cases ---
    print("=== CSV semantic differential (cell matching & round-trips) ===")
    for i, (c_src, expected_rows) in enumerate(VALID_CSV_CASES):
        parsed_tuple = moss_csv_parse_rows(c_src, ",")
        total += 1
        if parsed_tuple is None:
            failures += 1
            print(f"  FAIL CSV [{i}] Moss failed to parse: {c_src!r}")
        else:
            mo_rows, written = parsed_tuple
            if mo_rows != expected_rows:
                failures += 1
                print(f"  FAIL CSV [{i}] rows mismatch:\n    Expected: {expected_rows}\n    Got:      {mo_rows}")
            else:
                # Re-parse written CSV
                roundtrip_tuple = moss_csv_parse_rows(written, ",")
                if roundtrip_tuple is None or roundtrip_tuple[0] != expected_rows:
                    failures += 1
                    print(f"  FAIL CSV [{i}] round-trip mismatch")
                elif VERBOSE:
                    print(f"  ok CSV [{i}] matches {expected_rows}")

    # --- 5. CSV invalid cases ---
    print("=== CSV validate differential (invalid inputs) ===")
    for i, c in enumerate(INVALID_CSV_CASES):
        parsed_tuple = moss_csv_parse_rows(c, ",")
        total += 1
        if parsed_tuple is not None:
            failures += 1
            print(f"  FAIL invalid CSV [{i}] Moss accepted invalid CSV: {c!r}")
        elif VERBOSE:
            print(f"  ok invalid CSV [{i}] correctly rejected")

    # --- 6. CSV 10 random generated multi-row tables ---
    print("=== CSV random generated multi-row tables ===")
    for i in range(10):
        nrows = rng.randint(2, 6)
        ncols = rng.randint(2, 5)
        table = []
        for r in range(nrows):
            row = [f"cell_{r}_{c}_{rng.randint(0, 99)}" for c in range(ncols)]
            table.append(row)
        csv_io = io.StringIO()
        writer = csv.writer(csv_io, lineterminator="\n")
        writer.writerows(table)
        csv_str = csv_io.getvalue()
        parsed_tuple = moss_csv_parse_rows(csv_str, ",")
        total += 1
        if parsed_tuple is None:
            failures += 1
            print(f"  FAIL random CSV [{i}] Moss failed to parse")
        else:
            mo_rows, _ = parsed_tuple
            if mo_rows != table:
                failures += 1
                print(f"  FAIL random CSV [{i}] mismatch:\n    Expected: {table}\n    Got:      {mo_rows}")
            elif VERBOSE:
                print(f"  ok random CSV [{i}] matches")

    print(f"\n{total - failures}/{total} differential tests passed", end="")
    if failures:
        print(f" ({failures} FAILED)")
        return 1
    else:
        print(" — all OK")
        return 0


if __name__ == "__main__":
    sys.exit(run_tests())
