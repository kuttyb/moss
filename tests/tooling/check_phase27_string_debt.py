#!/usr/bin/env python3
"""Track expression-text readers by the consumer that calls them."""
import collections
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASELINE = ROOT / "docs/phase27/STRING_DEBT_BASELINE.md"
BASELINE_REV = "a4f4790"
# Helpers that inspect or split an expression string. `trim_compare` below also
# catches expression-text trim calls used as a textual classifier.
APIS = (
    "parse_simple_call", "split_top_level",
    "strip_redundant_outer_parentheses", "parse_enum_case_expression",
    "parse_functional_pipeline", "word_occurrences", "plain_identifier",
    "simple_identifier", "parse_index", "parse_member_call",
    "parse_vector_literal", "parse_named_argument",
    "parse_bound_method_callable", "parse_executor_chain",
    "parse_fileio_open", "parse_fileio_chunks_source",
    "expression_mentions_identifier", "parse_binary_expression",
    "parse_call", "split_arguments", "split_binary",
    "generated_split_binary", "normalize_pipeline",
    "strip_expression_parens", "top_level_member",
    "top_level_assignment", "top_level_colon", "identifier",
)
PATTERN = re.compile(r"\b(" + "|".join(map(re.escape, APIS)) + r")\s*\(")
CONSUMER_PATHS = {
    "checker": ("semantic_checker.cpp", "fileio_semantics.inc",
                "file_chunk_lowering.inc", "executor_invoke_lowering.inc",
                "static_bounds.inc"),
    "emitter": ("native_generator.cpp", "file_chunk_codegen.inc",
                "borrowed_views.inc", "handler_lowering.inc"),
    "reach_walk": (),
    "fast_debug": ("interpreter.hpp", "interpreter_domains.inc",
                   "interpreter_phase20.inc"),
    "optimizer": ("functional_optimizer.cpp",),
    "parser": ("parser.cpp",),
    "compiler_frontend": ("moss.cpp",),
}
EXTENSIONS = (".cpp", ".hpp", ".inc")
REACH_FUNCTIONS = (
    "has_expression_slot", "lhs_root_name", "expression_arena_for",
    "callable_spelling", "callable_leaf_spelling",
    "imported_module_qualified_name", "visit_expression_handles",
    "unqualified_callable_name", "functional_stage_kind",
    "add_expression_references", "add_statement_references",
    "add_callable_argument_references", "add_callable_value_reference",
    "add_bare_callable_handle_reference", "add_statement_callable_references",
    "scope_program_to_file", "execution_body_callable_closure",
)


def read_source(path, commit=None):
    if commit:
        return subprocess.check_output(["git", "show", f"{commit}:{path}"],
                                       cwd=ROOT, text=True)
    return (ROOT / path).read_text()


def consumer_for(path):
    name = pathlib.Path(path).name
    for consumer, names in CONSUMER_PATHS.items():
        if name in names:
            return consumer
    return None


def reach_lines(source):
    lines = source.splitlines()
    selected = set()
    for name in REACH_FUNCTIONS:
        start = next((i for i, line in enumerate(lines)
                      if re.match(r"\s*static\b", line) and
                      re.search(r"\b" + re.escape(name) + r"\s*\(", line) and
                      "{" in "\n".join(lines[i:i + 8]) and
                      (";" not in "\n".join(lines[i:i + 8]) or
                       "{" in "\n".join(lines[i:i + 8]).split(";", 1)[0])), None)
        if start is None:
            continue
        depth = 0
        opened = False
        for index in range(start, len(lines)):
            depth += lines[index].count("{") - lines[index].count("}")
            opened |= "{" in lines[index]
            if opened and depth == 0:
                selected.update(range(start + 1, index + 2))
                break
    return selected


def paths(commit=None):
    if commit:
        raw = subprocess.check_output(["git", "ls-tree", "-r", "--name-only",
                                       commit, "src"], cwd=ROOT, text=True)
        candidates = raw.splitlines()
    else:
        candidates = [p.relative_to(ROOT).as_posix() for p in (ROOT / "src").rglob("*")
                      if p.is_file()]
    return [p for p in candidates if p.endswith(EXTENSIONS) and consumer_for(p)]


def sites(commit=None):
    found = collections.Counter()
    for relative in paths(commit):
        consumer = consumer_for(relative)
        source = read_source(relative, commit)
        reach = reach_lines(source) if relative == "src/moss.cpp" else set()
        for line_no, line in enumerate(source.splitlines(), 1):
            # Definitions are not consumer call sites. (moss.cpp owns many
            # helper definitions while also containing the reach walk.)
            for match in PATTERN.finditer(line):
                api = match[1]
                site_consumer = ("reach_walk" if line_no in reach else consumer)
                # The source parser's local identifier predicate validates
                # declaration names; expression identifier reads are tracked
                # in the checker, emitter, reach walk, and interpreter.
                if api == "identifier" and site_consumer == "parser":
                    continue
                if re.match(r"\s*(?:static\s+)?(?:bool|string|size_t|vector<.*?>|"
                            r"optional<.*?>|std::optional<.*?>)\s+" +
                            re.escape(api) + r"\s*\(", line):
                    continue
                found[(site_consumer, relative, line_no, line.strip(), api)] += 1
            if line_no in reach:
                for match in re.finditer(
                        r"\b(?:statement|function|method)\.(text|a|b|args|result_expression)\b",
                        line):
                    field = "Stmt." + match[1]
                    found[("reach_walk", relative, line_no,
                           line.strip(), field)] += 1
            if consumer == "fast_debug":
                calls = re.findall(r"\beval\s*\(", line)
                if calls and not re.search(r"\bValue\s+eval\s*\(", line):
                    found[(consumer, relative, line_no, line.strip(), "eval(string)")] += len(calls)
            # Trimming followed by a comparison is a string classification
            # operation. Count at the call site, attributed to its caller.
            comparisons = list(re.finditer(r"(?:==|!=|<=|>=|(?<!-)[<>])", line))
            if ("trim(" in line and any(line[match.end():].strip()
                                         for match in comparisons)):
                found[(site_consumer, relative, line_no, line.strip(), "trim_compare")] += line.count("trim(")
    return found


def write_baseline():
    data = sites(BASELINE_REV)
    grouped = collections.defaultdict(collections.Counter)
    for (consumer, _path, _line, _text, api), count in data.items():
        grouped[consumer][api] += count
    apis = sorted({api for *_rest, api in data})
    consumers = tuple(CONSUMER_PATHS)
    columns = ["consumer", *apis]
    lines = ["# Phase 27 widened string-debt baseline", "",
             f"Measured at `{BASELINE_REV}`.",
             "Counts are call sites attributed to the source file that invokes the reader; helper definitions are excluded.",
             "The inventory covers expression parsers, identifier classifiers, expression identifier scans, pipeline parsing, optimizer word scans, trim-and-compare classifiers, and Fast Debug eval(string).",
             "",
             "Command: `python3 tests/tooling/check_phase27_string_debt.py --write-baseline` (the script reads `git show a4f4790:<path>`).",
             "", "| " + " | ".join(columns) + " |",
             "| " + " | ".join(["---"] + ["---:"] * len(apis)) + " |"]
    for consumer in consumers:
        lines.append("| " + " | ".join([consumer] + [str(grouped[consumer][api]) for api in apis]) + " |")
    serial = {"source_commit": BASELINE_REV,
              "sites": [[*key, count] for key, count in sorted(data.items())]}
    lines.extend(["", "<!-- machine-readable baseline", json.dumps(serial, separators=(",", ":")),
                  "end machine-readable baseline -->", ""])
    BASELINE.write_text("\n".join(lines))


def load_baseline():
    match = re.search(r"<!-- machine-readable baseline\s*(.*?)\s*end machine-readable baseline -->",
                      BASELINE.read_text(), re.S)
    if not match:
        raise SystemExit("string-debt baseline has no machine-readable site list")
    baseline = collections.Counter()
    for consumer, _path, _line, _text, api, amount in json.loads(match[1])["sites"]:
        baseline[(consumer, api)] += amount
    return baseline


def main():
    if "--write-baseline" in sys.argv:
        write_baseline()
        return 0
    baseline = load_baseline()
    actual = collections.Counter()
    for (consumer, _path, _line, _text, api), amount in sites().items():
        actual[(consumer, api)] += amount
    added = [(key, value - baseline.get(key, 0)) for key, value in actual.items()
             if value > baseline.get(key, 0)]
    for (consumer, api), amount in sorted(added):
        print(f"new expression-text reader call count ({amount}): {consumer}/{api}", file=sys.stderr)
    field_reads = [(path, line, text, api)
                   for (consumer, path, line, text, api), amount in sites().items()
                   if consumer == "reach_walk" and api.startswith("Stmt.") and amount]
    for path, line, text, api in field_reads:
        print(f"reach walk expression field read {api} at {path}:{line}: {text}",
              file=sys.stderr)
    added.extend((("reach_walk", api), 1) for _path, _line, _text, api in field_reads)
    return 1 if added else 0


if __name__ == "__main__":
    sys.exit(main())
