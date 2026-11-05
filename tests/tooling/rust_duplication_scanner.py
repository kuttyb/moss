"""Shared lightweight Rust token and duplication-operator scanner.

This scanner reports syntax candidates only. It does not expand macros, resolve
traits, inspect MIR, or establish whether a Moss value was duplicated.
"""

from __future__ import annotations

import re


OPERATORS = {
    "dot_clone": re.compile(r"\.\s*clone\s*\(\s*\)"),
    "dot_clone_from": re.compile(r"\.\s*clone_from\s*\("),
    "dot_cloned": re.compile(r"\.\s*cloned\s*\(\s*\)"),
    "dot_copied": re.compile(r"\.\s*copied\s*\(\s*\)"),
    "dot_to_owned": re.compile(r"\.\s*to_owned\s*\(\s*\)"),
    "dot_to_string": re.compile(r"\.\s*to_string\s*\(\s*\)"),
    "dot_to_vec": re.compile(r"\.\s*to_vec\s*\(\s*\)"),
    "qualified_clone": re.compile(r"\b(?:Clone|Arc|Rc)::clone\s*\("),
    "mem_replace": re.compile(r"\b(?:std::)?mem::replace\s*\("),
    "mem_take": re.compile(r"\b(?:std::)?mem::take\s*\("),
    "ptr_read": re.compile(r"\b(?:std::)?ptr::read\s*\("),
    "string_or_vec_from": re.compile(r"\b(?:String|Vec)::from\s*\("),
}

RAW_STRING_START = re.compile(r"(?:br|cr|r)(#+)?\"")
CHAR_LITERAL = re.compile(r"'(?:\\.|[^'\\])'")
TOKEN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*|::|[^\s]")


def mask_non_code(source: str, comments: list[dict] | None = None) -> str:
    """Blank comments and literals while preserving offsets and line breaks."""
    chars = list(source)
    i = 0
    n = len(source)
    while i < n:
        if source.startswith("//", i):
            end = source.find("\n", i)
            end = n if end < 0 else end
            if comments is not None:
                comments.append({"kind": "line", "start": i, "end": end,
                                 "text": source[i + 2:end]})
            for pos in range(i, end):
                chars[pos] = " "
            i = end
            continue
        if source.startswith("/*", i):
            start = i
            i += 2
            depth = 1
            while i < n and depth:
                if source.startswith("/*", i):
                    depth += 1
                    i += 2
                elif source.startswith("*/", i):
                    depth -= 1
                    i += 2
                else:
                    i += 1
            if comments is not None:
                comments.append({"kind": "block", "start": start, "end": i,
                                 "text": source[start + 2:max(start + 2, i - 2)]})
            for pos in range(start, i):
                if chars[pos] != "\n":
                    chars[pos] = " "
            continue

        raw_start = RAW_STRING_START.match(source, i)
        if raw_start:
            hashes = raw_start.group(1) or ""
            terminator = '"' + hashes
            end = source.find(terminator, raw_start.end())
            end = n if end < 0 else end + len(terminator)
            for pos in range(i, end):
                if chars[pos] != "\n":
                    chars[pos] = " "
            i = end
            continue

        quote = i
        if source.startswith(("b\"", "c\""), i):
            quote = i + 1
        if source[quote] == '"':
            start = i
            i = quote + 1
            while i < n:
                if source[i] == "\\":
                    i += 2
                elif source[i] == '"':
                    i += 1
                    break
                else:
                    i += 1
            for pos in range(start, i):
                if chars[pos] != "\n":
                    chars[pos] = " "
            continue

        if source[i] == "'":
            char = CHAR_LITERAL.match(source, i)
            if char:
                for pos in range(i, char.end()):
                    chars[pos] = " "
                i = char.end()
                continue
        i += 1
    return "".join(chars)


def code_tokens(masked_source: str) -> list[tuple[str, int, int]]:
    """Return significant code tokens and their character offsets."""
    return [(match.group(), match.start(), match.end())
            for match in TOKEN.finditer(masked_source)]


def operator_spans(masked_source: str, operator: str) -> list[tuple[int, int]]:
    """Find operator spans in already-masked text; dot methods use tokens."""
    if operator not in OPERATORS:
        raise KeyError(operator)
    if operator in {"dot_clone", "dot_cloned", "dot_copied"}:
        method = {"dot_clone": "clone", "dot_cloned": "cloned",
                  "dot_copied": "copied"}[operator]
        tokens = code_tokens(masked_source)
        spans = []
        for index in range(len(tokens) - 3):
            if (tokens[index][0] == "." and tokens[index + 1][0] == method and
                    tokens[index + 2][0] == "(" and tokens[index + 3][0] == ")"):
                spans.append((tokens[index][1], tokens[index + 3][2]))
        return spans
    return [(match.start(), match.end())
            for match in OPERATORS[operator].finditer(masked_source)]
