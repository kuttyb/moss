#!/usr/bin/env python3
"""Produce the line-level raw-field audit for frozen CP-E candidate 6b926fc7."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[4]
FILES = (
    "src/semantic_checker.cpp", "src/fileio_semantics.inc",
    "src/file_chunk_lowering.inc", "src/executor_invoke_lowering.inc",
    "src/static_bounds.inc", "src/native_generator.cpp",
    "src/file_chunk_codegen.inc", "src/borrowed_views.inc",
    "src/handler_lowering.inc",
)
FIELD = re.compile(r"\b([A-Za-z_]\w*)\s*(?:\.|->)\s*(text|a|b|args|result_expression)\b")
RAW = re.compile(r"\b(?:starts_with|ends_with|trim|rfind|find|substr|front|back)\s*\(|\.front\s*\(|\.back\s*\(")
READER = re.compile(r"\b(?:tree_|est::|parse_|split_|normalize_pipeline|expression_mentions_identifier)")

def classify(line: str, field: str) -> str:
    if RAW.search(line):
        return "raw-string-operation-review"
    if READER.search(line):
        return "EST-or-expression-reader-call"
    if re.search(r"(?:==|!=)\s*\"[^\"]*\"|\"[^\"]*\"\s*(?:==|!=)", line):
        return "literal-comparison-review"
    if field in ("args", "text", "b", "result_expression"):
        return "expression-value-use-review"
    return "mixed-name-or-expression-field-review"

def main():
    rows = []
    for relative in FILES:
        for number, line in enumerate((ROOT / relative).read_text().splitlines(), 1):
            for match in FIELD.finditer(line):
                var, field = match.groups()
                rows.append((relative, number, var, field, classify(line, field), line.strip()))
    print("path\tline\tbase\tfield\tclassification\tsource")
    for row in rows:
        print("\t".join(map(str, row)))
    print(f"# field_access_occurrences={len(rows)}", file=sys.stderr)
    print("# tree_expression_text_calls=" + str(sum(
        len(re.findall(r"\btree_expression_text\s*\(",
                       (ROOT / relative).read_text())) for relative in FILES)), file=sys.stderr)

if __name__ == "__main__":
    main()
