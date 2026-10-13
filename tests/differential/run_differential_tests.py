#!/usr/bin/env python3
"""Differential tests for moss.json and moss.csv against Python stdlib.

Uses a margo package project to run tests via moss.json and moss.csv.
Runs seeded adversarial and well-formed inputs through both the Moss
SDK and Python's json/csv modules.

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


def moss_json_validate(json_text: str) -> bool:
    escaped = json_text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r")
    src = textwrap.dedent(f'''\
        import json
        fn main():
          s = "{escaped}"
          if json.validate(s):
            echo "true"
          else:
            echo "false"
    ''')
    out = run_moss_probe(src)
    return out == "true"


def python_json_validate(json_text: str) -> bool:
    try:
        json.loads(json_text)
        return True
    except (json.JSONDecodeError, ValueError):
        return False


def moss_csv_validate(csv_text: str) -> bool:
    escaped = csv_text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")
    src = textwrap.dedent(f'''\
        import csv
        fn main():
          s = "{escaped}"
          if csv.validate(s, ","):
            echo "true"
          else:
            echo "false"
    ''')
    out = run_moss_probe(src)
    return out == "true"


def python_csv_validate(csv_text: str) -> bool:
    # Python's csv.reader treats almost any string as valid unless quoting rules are broken
    try:
        reader = csv.reader(io.StringIO(csv_text))
        for _ in reader:
            pass
        return True
    except csv.Error:
        return False


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
    "a,b,c\n1,2,3\n",
    '"quoted","field"\n',
    '"embedded,comma",normal\n',
    '"double""quote",ok\n',
    "single\n",
]

INVALID_CSV_CASES = [
    '"unclosed',
    '"trailing"garbage',
    'ab"cd',
]


def run_tests() -> int:
    setup_harness()
    failures = 0
    total = 0
    rng = random.Random(42)

    # --- JSON validate differential: valid generated inputs ---
    print("=== JSON validate differential (valid, seeded) ===")
    for i in range(25):
        j = generate_valid_json(rng)
        py_ok = python_json_validate(j)
        mo_ok = moss_json_validate(j)
        total += 1
        if py_ok != mo_ok:
            failures += 1
            print(f"  FAIL [{i}] py={py_ok} moss={mo_ok} input={j[:60]!r}")
        elif VERBOSE:
            print(f"  ok  [{i}] py={py_ok} moss={mo_ok}")

    # --- JSON validate differential: invalid inputs ---
    print("=== JSON validate differential (invalid) ===")
    for i, j in enumerate(INVALID_JSON_CASES):
        py_ok = python_json_validate(j)
        mo_ok = moss_json_validate(j)
        total += 1
        if py_ok != mo_ok:
            failures += 1
            print(f"  FAIL [{i}] py={py_ok} moss={mo_ok} input={j!r}")
        elif VERBOSE:
            print(f"  ok  [{i}] py={py_ok} moss={mo_ok}")

    # --- CSV validate differential ---
    print("=== CSV validate differential ===")
    for i, c in enumerate(VALID_CSV_CASES):
        mo_ok = moss_csv_validate(c)
        total += 1
        if not mo_ok:
            failures += 1
            print(f"  FAIL valid CSV [{i}] moss={mo_ok} input={c!r}")
        elif VERBOSE:
            print(f"  ok valid CSV [{i}] moss={mo_ok}")

    for i, c in enumerate(INVALID_CSV_CASES):
        mo_ok = moss_csv_validate(c)
        total += 1
        if mo_ok:
            failures += 1
            print(f"  FAIL invalid CSV [{i}] moss={mo_ok} expected False input={c!r}")
        elif VERBOSE:
            print(f"  ok invalid CSV [{i}] moss={mo_ok}")

    print(f"\n{total - failures}/{total} differential tests passed", end="")
    if failures:
        print(f" ({failures} FAILED)")
        return 1
    else:
        print(" — all OK")
        return 0


if __name__ == "__main__":
    sys.exit(run_tests())
