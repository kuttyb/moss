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
BASELINE_REV = "04be1a1"
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
# Raw Stmt slots are expression text unless the consumer knows, by node kind,
# that the slot carries a name/keyword. These remaining literal comparisons
# are audited NAME fields; expression-shaped literals (constructors and
# collections) are intentionally absent and must use EST queries.
RAW_NAME_COUNTS = {
    ("checker", "push"): 5, ("checker", "insert"): 2,
    ("checker", "invoke"): 4, ("checker", "join"): 3,
    ("checker", "consume"): 2,
    ("checker", "FileIO"): 1,
    ("checker", "pop"): 3, ("checker", "pop_front"): 3,
    ("checker", "assert"): 2, ("checker", "sort"): 1,
    ("checker", "pass"): 1,
    ("emitter", "consume"): 3, ("emitter", "_"): 1,
    ("emitter", "delete"): 1, ("emitter", "assert"): 1,
    ("emitter", "assertEqual"): 1, ("emitter", "sort"): 1,
    ("emitter", "pop"): 1, ("emitter", "write"): 1,
    ("emitter", "open"): 1, ("emitter", "sync"): 1,
    ("emitter", "close"): 1,
}
RAW_NAME_COMPARE = re.compile(
    r"\b(?:s|prior|statement|stmt)\.(?:a|b|text)\s*(?:==|!=)\s*\"([^\"\n]*)\"")
RAW_FIELD_VARIABLE_COMPARE = re.compile(
    r"\b[A-Za-z_]\w*(?:\([^()]*\))?(?:\.[A-Za-z_]\w*|\[[^]]+\])*"
    r"\.(a|b|text)\s*(?:==|!=)\s*[A-Za-z_]\w*"
    r"(?:\.[A-Za-z_]\w*|\[[^]]+\])*\b")
RAW_BINDING_COMPARE_COUNTS = {("checker", "a"): 9}
# Shape tests on expression-bearing Stmt fields are readers even when they do
# not call one of the legacy parser APIs. Keep this fail-closed for direct
# punctuation searches, first-character dispatch, and trimmed-prefix tests.
RAW_FIELD_SHAPE = re.compile(
    r"\b[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*\.(?:a|b|text)\s*"
    r"\.\s*(?:find|rfind|find_first_of|find_first_not_of|front|back)\s*\("
)
RAW_FIELD_PREFIX = re.compile(
    r"\b(?:starts_with|ends_with)\s*\(\s*(?:trim\s*\(\s*)?"
    r"[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*\.(?:a|b|text)"
)
RAW_FIELD_FRONT_DISPATCH = re.compile(
    r"\b[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*\.(?:a|b|text)\s*"
    r"\.\s*(?:size\s*\(\s*\)\s*&&\s*)?[^;\n]*\.front\s*\("
)
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
EXEMPTIONS = ROOT / "docs/phase27/27_3_STRING_READER_EXEMPTIONS.md"
EXEMPTION_ENTRY = re.compile(r"^- `([^`|]+) \| ([A-Za-z_][A-Za-z0-9_:]*) \| ([a-z_]+)` — (.+)$")


def load_exemptions():
    entries = {}
    if not EXEMPTIONS.exists():
        return entries
    for line_no, line in enumerate(EXEMPTIONS.read_text().splitlines(), 1):
        if not line.startswith("- `"):
            continue
        match = EXEMPTION_ENTRY.match(line)
        if not match:
            raise SystemExit(f"malformed expression-reader exemption at {EXEMPTIONS}:{line_no}")
        path, function, api, reason = match.groups()
        if not reason.strip():
            raise SystemExit(f"empty expression-reader exemption at {EXEMPTIONS}:{line_no}")
        key = (path.strip(), function, api)
        if key in entries:
            raise SystemExit(f"duplicate expression-reader exemption: {key}")
        entries[key] = reason
    return entries


def function_ranges(source, names):
    """Find named C++ function bodies for fail-closed exemption accounting."""
    ranges = {}
    for name in names:
        candidates = []
        for match in re.finditer(r"\b" + re.escape(name) + r"\s*\(", source):
            opening = source.find("(", match.start())
            depth = 0
            quote = None
            escaped = False
            closing = None
            for index in range(opening, len(source)):
                char = source[index]
                if quote:
                    if escaped: escaped = False
                    elif char == "\\": escaped = True
                    elif char == quote: quote = None
                    continue
                if char in ('"', "'"): quote = char
                elif char == "(": depth += 1
                elif char == ")":
                    depth -= 1
                    if depth == 0:
                        closing = index + 1
                        break
            if closing is None: continue
            cursor = closing
            while cursor < len(source) and source[cursor].isspace(): cursor += 1
            if source.startswith("const", cursor):
                cursor += len("const")
                while cursor < len(source) and source[cursor].isspace(): cursor += 1
            if cursor >= len(source) or source[cursor] != "{": continue
            body_depth = 0
            quote = None
            escaped = False
            end = None
            for index in range(cursor, len(source)):
                char = source[index]
                if quote:
                    if escaped: escaped = False
                    elif char == "\\": escaped = True
                    elif char == quote: quote = None
                    continue
                if char in ('"', "'"): quote = char
                elif char == "{": body_depth += 1
                elif char == "}":
                    body_depth -= 1
                    if body_depth == 0:
                        end = index + 1
                        break
            if end is not None:
                start_line = source.count("\n", 0, match.start()) + 1
                end_line = source.count("\n", 0, end) + 1
                candidates.append((start_line, end_line))
        if len(candidates) == 1:
            ranges[name] = candidates[0]
        elif candidates:
            raise SystemExit(f"ambiguous function exemption target {name}: {candidates}")
    return ranges


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


def trim_result_followed_by_comparison(line, start):
    opening = line.find("(", start)
    if opening < 0:
        return False
    depth = 0
    quote = None
    escaped = False
    closing = None
    for index in range(opening, len(line)):
        char = line[index]
        if quote:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            continue
        if char in ("\"", "'"):
            quote = char
        elif char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                closing = index + 1
                break
    if closing is None:
        return False
    remainder = line[closing:].lstrip()
    return re.match(r"(?:==|!=|<=|>=|(?<!-)[<>])", remainder) is not None


def sites(commit=None):
    found = collections.Counter()
    exemptions = load_exemptions() if commit is None else {}
    matched_exemptions = set()
    exemption_site_counts = collections.Counter()
    for relative in paths(commit):
        consumer = consumer_for(relative)
        source = read_source(relative, commit)
        exemption_names = {function for path, function, _api in exemptions
                           if path == relative} if commit is None else set()
        ranges = function_ranges(source, exemption_names)
        missing_functions = exemption_names - set(ranges)
        if missing_functions:
            raise SystemExit("missing exemption function(s): " + ", ".join(sorted(missing_functions)))
        reach = reach_lines(source) if relative == "src/moss.cpp" else set()
        for line_no, line in enumerate(source.splitlines(), 1):
            site_consumer = ("reach_walk" if line_no in reach else consumer)
            # Definitions are not consumer call sites. (moss.cpp owns many
            # helper definitions while also containing the reach walk.)
            for match in PATTERN.finditer(line):
                api = match[1]
                # The recovered vector projection keeps its legacy parser as
                # an assertion-only parity oracle in shadow mode. It does not
                # classify or consume an expression in the checker.
                if api == "parse_vector_literal" and \
                        "assert(moss::parse_vector_literal" in line:
                    continue
                function = next((name for name, (start, end) in ranges.items()
                                 if start <= line_no <= end), None)
                key = (relative, function, api)
                if function is not None and key in exemptions:
                    matched_exemptions.add(key)
                    exemption_site_counts[key] += 1
                    continue
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
                # `evaluate_expression_source` is the Fast Debug source boundary:
                # it resolves a retained EST handle (or parses one temporary EST
                # root for synthetic/declaration expressions) and dispatches to
                # evaluate_expression_tree. It is not the former recursive
                # eval(string) implementation. Keep counting the legacy API and
                # its old renamed spelling so a rename alone cannot clear debt.
                calls = re.findall(r"\b(?:eval|evaluate_expression)\s*\(", line)
                if calls and not re.search(
                        r"\bValue\s+(?:eval|evaluate_expression)\s*\(", line):
                    found[(consumer, relative, line_no, line.strip(), "eval(string)")] += len(calls)
            # Trimming followed by a comparison is a string classification
            # operation. Count at the call site, attributed to its caller.
            trim_calls = list(re.finditer(r"\btrim\s*\(", line))
            compared_trims = sum(
                trim_result_followed_by_comparison(line, trim_call.start())
                for trim_call in trim_calls)
            if compared_trims:
                found[(site_consumer, relative, line_no,
                       line.strip(), "trim_compare")] += compared_trims
    stale = sorted(set(exemptions) - matched_exemptions)
    if stale:
        raise SystemExit("stale expression-reader exemptions: " + ", ".join(map(str, stale)))
    duplicated = sorted(key for key, count in exemption_site_counts.items() if count != 1)
    if duplicated:
        raise SystemExit("exemption function gained multiple reader sites: " +
                         ", ".join(f"{key} ({exemption_site_counts[key]})" for key in duplicated))
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
    if "--require-zero" in sys.argv:
        index = sys.argv.index("--require-zero")
        if index + 1 >= len(sys.argv):
            raise SystemExit("--require-zero requires a consumer name")
        consumer = sys.argv[index + 1]
        remaining = collections.Counter()
        for (actual_consumer, _path, _line, _text, api), amount in sites().items():
            if actual_consumer == consumer:
                remaining[api] += amount
        if remaining:
            for api, amount in sorted(remaining.items()):
                print(f"remaining expression-text reader call count ({amount}): {consumer}/{api}", file=sys.stderr)
            return 1
        if consumer == "fast_debug":
            interpreter = (ROOT / "src/interpreter.hpp").read_text()
            adapter_start = interpreter.find("Value evaluate_expression_source(")
            node_start = interpreter.find("Value evaluate_expression_tree(", adapter_start)
            if adapter_start < 0 or node_start < 0:
                raise SystemExit("Fast Debug EST evaluator entry points are missing")
            adapter = interpreter[adapter_start:node_start]
            node_body = interpreter[node_start:interpreter.find("// Native lowering", node_start)]
            if "expression_tree(expression)" not in adapter or \
                    "evaluate_expression_tree(*tree, tree->root" not in adapter:
                raise SystemExit("Fast Debug source adapter does not dispatch through EST")
            if "evaluate_tree_node" not in node_body or \
                    "std::get_if<est::Binary>" not in node_body:
                raise SystemExit("Fast Debug evaluator is not recursively dispatching EST nodes")
    name_comparisons = collections.Counter()
    unclassified = []
    raw_field_variables = collections.Counter()
    raw_field_shapes = []
    for relative in paths():
        consumer = consumer_for(relative)
        if consumer not in ("checker", "emitter"):
            continue
        for line_no, line in enumerate(read_source(relative).splitlines(), 1):
            if (RAW_FIELD_SHAPE.search(line) or RAW_FIELD_PREFIX.search(line) or
                    RAW_FIELD_FRONT_DISPATCH.search(line)):
                raw_field_shapes.append(f"{relative}:{line_no}: {line.strip()}")
            for variable_match in RAW_FIELD_VARIABLE_COMPARE.finditer(line):
                raw_field_variables[(consumer, variable_match[1])] += 1
            for match in RAW_NAME_COMPARE.finditer(line):
                key = (consumer, match[1])
                if key not in RAW_NAME_COUNTS:
                    unclassified.append(f"{relative}:{line_no}: {line.strip()}")
                else:
                    name_comparisons[key] += 1
    if unclassified:
        raise SystemExit("unclassified raw Stmt comparison(s):\n" +
                         "\n".join(unclassified))
    if raw_field_shapes:
        raise SystemExit("raw expression-shape test on Stmt field(s):\n" +
                         "\n".join(raw_field_shapes))
    if raw_field_variables != collections.Counter(RAW_BINDING_COMPARE_COUNTS):
        raise SystemExit("audited raw Stmt binding comparison counts changed: " +
                         repr(dict(sorted(raw_field_variables.items()))))
    if name_comparisons != collections.Counter(RAW_NAME_COUNTS):
        raise SystemExit("audited raw Stmt NAME comparison counts changed: " +
                         repr(dict(sorted(name_comparisons.items()))))
    # The wrapper was removed from checker/emitter. Keep this explicit guard
    # so a later textual classifier cannot re-enter under the same spelling.
    tree_text_reads = [(path, line_no, line.strip())
                       for path in paths()
                       if consumer_for(path) in ("checker", "emitter")
                       for line_no, line in enumerate(read_source(path).splitlines(), 1)
                       if re.search(r"\btree_expression_text\s*\(", line)]
    if tree_text_reads:
        raise SystemExit("tree_expression_text comparison/read found: " +
                         repr(tree_text_reads))
    print("audited raw Stmt NAME comparisons: " + str(sum(name_comparisons.values())))
    print("audited raw Stmt binding comparisons: " +
          str(sum(raw_field_variables.values())))
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
