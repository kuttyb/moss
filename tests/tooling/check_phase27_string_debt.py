#!/usr/bin/env python3
"""Reject additions to legacy string-expression consumers after Phase 27 baseline."""
import collections
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASELINE = ROOT / "docs/phase27/STRING_DEBT_BASELINE.md"
BASELINE_REV = "4d6d0c8"
APIS = ("parse_simple_call", "split_top_level",
        "strip_redundant_outer_parentheses", "parse_enum_case_expression")
PATTERN = re.compile(r"\b(" + "|".join(APIS) + r")\s*\(")
CONSUMERS = {
    "checker": ("src/semantic_checker.cpp",),
    "emitter": ("src/native_generator.cpp",),
    "reach_walk": ("src/moss.cpp",),
    "fast_debug": ("src/interpreter.hpp", "src/interpreter_domains.inc",
                   "src/interpreter_phase20.inc"),
}


def read_source(path, commit=None):
    if commit:
        return subprocess.check_output(["git", "show", f"{commit}:{path}"],
                                       cwd=ROOT, text=True)
    return (ROOT / path).read_text()


def sites(commit=None):
    found = collections.Counter()
    for consumer, paths in CONSUMERS.items():
        for relative in paths:
            source = read_source(relative, commit)
            for line_no, line in enumerate(source.splitlines(), 1):
                for match in PATTERN.finditer(line):
                    if relative == "src/moss.cpp" and re.match(
                            r"\s*(?:bool|string|vector<string>)\s+" +
                            re.escape(match[1]) + r"\s*\(", line):
                        continue
                    found[(consumer, relative, line_no, line.strip(), match[1])] += 1
                if consumer == "fast_debug":
                    # Count evaluator invocations, excluding its method definition.
                    if re.search(r"\beval\s*\(", line) and not re.search(r"\bValue\s+eval\s*\(", line):
                        found[(consumer, relative, line_no, line.strip(), "eval")] += len(
                            re.findall(r"\beval\s*\(", line))
    return found


def counts(data):
    result = {name: collections.Counter() for name in CONSUMERS}
    for (consumer, _path, _line, _text, api), value in data.items():
        result[consumer][api] += value
    return result


def write_baseline():
    data = sites(BASELINE_REV)
    by_consumer = counts(data)
    apis = (*APIS, "eval")
    lines = [
        "# Phase 27 string-debt baseline",
        "",
        f"Measured at `{BASELINE_REV}` (the required 4d6d0c8 baseline).",
        "A site is a source occurrence of a listed legacy helper in its consumer file;",
        "Fast Debug counts calls to the interpreter `eval(string, ...)` and excludes its definition.",
        "",
        "Command: `python3 tests/tooling/check_phase27_string_debt.py --write-baseline`",
        "The generator reads the four consumer files below from `git show 4d6d0c8:<path>`.",
        "",
        "| Consumer | parse_simple_call | split_top_level | strip_redundant_outer_parentheses | parse_enum_case_expression | interpreter eval(string) |",
        "| --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for consumer in CONSUMERS:
        values = [by_consumer[consumer][api] for api in apis]
        lines.append("| " + consumer + " | " + " | ".join(map(str, values)) + " |")
    serial = {"source_commit": BASELINE_REV,
              "sites": [[*key, value] for key, value in sorted(data.items())]}
    lines.extend(["", "<!-- machine-readable baseline", json.dumps(serial, separators=(",", ":")),
                  "end machine-readable baseline -->", ""])
    BASELINE.write_text("\n".join(lines))


def load_baseline():
    text = BASELINE.read_text()
    match = re.search(r"<!-- machine-readable baseline\s*(.*?)\s*end machine-readable baseline -->",
                      text, re.S)
    if not match:
        raise SystemExit("string-debt baseline has no machine-readable site list")
    data = json.loads(match[1])
    return {tuple(row[:5]): row[5] for row in data["sites"]}


def main():
    if "--write-baseline" in sys.argv:
        write_baseline()
        return 0
    baseline = load_baseline()
    actual = sites()
    added = [(key, value - baseline.get(key, 0)) for key, value in actual.items()
             if value > baseline.get(key, 0)]
    for key, amount in added:
        print(f"new string parser call site ({amount}): {key}", file=sys.stderr)
    return 1 if added else 0


if __name__ == "__main__":
    sys.exit(main())
