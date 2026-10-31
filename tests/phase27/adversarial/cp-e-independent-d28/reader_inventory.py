#!/usr/bin/env python3
"""Independent line-level inventory of checker/emitter Stmt text reads."""
from __future__ import annotations

import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[4]
FILES = (
    "src/semantic_checker.cpp", "src/fileio_semantics.inc",
    "src/file_chunk_lowering.inc", "src/executor_invoke_lowering.inc",
    "src/static_bounds.inc", "src/native_generator.cpp",
    "src/file_chunk_codegen.inc", "src/borrowed_views.inc",
    "src/handler_lowering.inc",
)
SLOTS = re.compile(r"\b([A-Za-z_]\w*)\.(text|a|b|args)\b")
TREE_TEXT = re.compile(r"\btree_expression_text\s*\(")
RAW_DIRECT_SHAPE = re.compile(
    r"\b[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*\.(?:a|b|text)\s*"
    r"\.\s*(?:find|rfind|find_first_of|find_first_not_of|front|back)\s*\(|"
    r"\b(?:starts_with|ends_with)\s*\(\s*(?:trim\s*\(\s*)?"
    r"[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*\.(?:a|b|text)")
RAW_DIRECT_COMPARE = re.compile(
    r"\b[A-Za-z_]\w*(?:\.[A-Za-z_]\w*)*\.(?:a|b|text)\s*(?:==|!=)\s*\"")
ALIAS_SEED = re.compile(
    r"\b(?:const\s+)?(?:string|auto)\s+([A-Za-z_]\w*)\s*=\s*"
    r"(?:trim\s*\(\s*)?([A-Za-z_]\w*)\.(a|b|text|args)\b")
ALIAS_SHAPE = re.compile(
    r"\b(?:starts_with|ends_with)\s*\(\s*([A-Za-z_]\w*)\b|"
    r"\b([A-Za-z_]\w*)\s*\.\s*(?:find|rfind|find_first_of|find_first_not_of|"
    r"front|back|substr|compare)\s*\(")


def function_at(lines: list[str], row: int) -> str:
    # Nearest preceding outdented function/declaration header is a review aid;
    # exact function bodies are manually confirmed for each reported reader.
    for i in range(row, -1, -1):
        line = lines[i].strip()
        if "(" in line and ")" in line and not line.startswith(("if ", "for ", "while ", "switch ", "catch ")):
            if re.search(r"\b(?:bool|void|int|size_t|string|auto|std::optional|vector|Value|Type)\b", line):
                return line[:180]
    return "<no nearby signature>"


def main() -> None:
    slot_counts: dict[str, int] = {}
    text_reads: list[tuple[str, int, str, str, str]] = []
    raw_direct: list[tuple[str, int, str]] = []
    aliases: list[tuple[str, int, str, str, str]] = []
    tree_calls: list[tuple[str, int, str]] = []
    for name in FILES:
        lines = (ROOT / name).read_text().splitlines()
        for row, line in enumerate(lines):
            for match in SLOTS.finditer(line):
                slot_counts[match[2]] = slot_counts.get(match[2], 0) + 1
                text_reads.append((name, row + 1, match[1], match[2], function_at(lines, row)))
            if TREE_TEXT.search(line):
                tree_calls.append((name, row + 1, line.strip()))
            if RAW_DIRECT_SHAPE.search(line) or RAW_DIRECT_COMPARE.search(line):
                raw_direct.append((name, row + 1, line.strip()))
            if seed := ALIAS_SEED.search(line):
                aliases.append((name, row + 1, seed[1], seed[2], seed[3]))
    print("DIRECT_SLOT_READS", len(text_reads), "BY_SLOT", dict(sorted(slot_counts.items())))
    print("TREE_EXPRESSION_TEXT_CALLS", len(tree_calls))
    for row in tree_calls:
        print("TREE_TEXT", *row)
    print("DIRECT_RAW_FIELD_SHAPE_OR_LITERAL_COMPARISON_LINES", len(raw_direct))
    for row in raw_direct:
        print("DIRECT_RAW_SHAPE", *row)
    print("LOCAL_STRING_ALIAS_SEEDS", len(aliases))
    alias_shape_uses = 0
    for name, line_no, variable, source_var, slot in aliases:
        lines=(ROOT/name).read_text().splitlines()
        uses=[]
        for use_no in range(line_no, min(len(lines), line_no+100)):
            if re.search(r"\b" + re.escape(variable) + r"\b", lines[use_no]) and ALIAS_SHAPE.search(lines[use_no]):
                uses.append((use_no+1, lines[use_no].strip()))
        alias_shape_uses += len(uses)
        print("ALIAS", name, line_no, variable, source_var+"."+slot,
              "shape_uses="+str(len(uses)), repr(uses))
    print("BOUNDED_LOCAL_ALIAS_RAW_SHAPE_USES", alias_shape_uses)
    for row in text_reads:
        name, line_no, object_name, slot, function = row
        code = (ROOT / name).read_text().splitlines()[line_no-1].strip()
        print("FIELD", name, line_no, object_name, slot, function, code)


if __name__ == "__main__":
    main()
