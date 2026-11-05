#!/usr/bin/env python3
"""Inventory Rust duplication candidates and group them by generated source site.

This is an independent triage scanner, not authorization evidence. In
particular, a clean result cannot prove that macros, Copy loads, or other Rust
semantics do not duplicate Moss values.
"""

from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import re
from collections import Counter, defaultdict
from pathlib import Path


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


def mask_non_code(source: str) -> str:
    """Blank comments and literals while preserving offsets and line breaks."""
    chars = list(source)
    i = 0
    n = len(source)
    while i < n:
        if source.startswith("//", i):
            end = source.find("\n", i)
            end = n if end < 0 else end
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
            for pos in range(start, i):
                if chars[pos] != "\n":
                    chars[pos] = " "
            continue

        raw_start = RAW_STRING_START.match(source, i)
        if raw_start:
            hashes = raw_start.group(1) or ""
            terminator = '"' + hashes
            # `Match.end()` is an absolute offset when match() receives `pos`.
            end = source.find(terminator, raw_start.end())
            end = n if end < 0 else end + len(terminator)
            for pos in range(i, end):
                if chars[pos] != "\n":
                    chars[pos] = " "
            i = end
            continue

        # Byte and C ordinary strings are literals too.
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


def line_metadata(source: str, masked: str) -> tuple[list[int], list[tuple[str, str] | None], list[str | None], list[str]]:
    line_starts = [0]
    line_starts.extend(i + 1 for i, ch in enumerate(source) if ch == "\n")
    regions: list[tuple[str, str] | None] = []
    moss_sources: list[str | None] = []
    functions: list[str] = []
    active: tuple[str, str] | None = None
    last_moss_source = None
    current_function = "<outside-function>"
    masked_lines = masked.splitlines()
    for line_index, line in enumerate(source.splitlines()):
        begin = re.search(r"// Moss tooling begin\|([^|]+)\|([^|]+)(?:\|([^|]*))?(?:\|([^|]*))?", line)
        if begin:
            kind, identity, symbol, native = begin.groups()
            # The generated source line changes between fixtures; the
            # construct kind/symbol identify the emitter region.
            active = (kind, native or symbol or identity)
            last_moss_source = None
        source_match = re.search(r"// Moss line \d+:\s*(.*)$", line)
        if source_match:
            last_moss_source = source_match.group(1).strip()
        # masked/source lines align. Find a Rust function declaration on this
        # line so following occurrences can be attributed without rescanning
        # the full prefix for every call.
        match = re.search(r"\bfn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", masked_lines[line_index])
        if match:
            current_function = match.group(1)
        functions.append(current_function)
        regions.append(active)
        moss_sources.append(last_moss_source)
        if re.search(r"// Moss tooling end\|", line):
            active = None
            last_moss_source = None
    return line_starts, regions, moss_sources, functions


def inspect_rust(path: Path, root: Path) -> list[dict]:
    source = path.read_text(errors="replace")
    masked = mask_non_code(source)
    line_starts, regions, moss_sources, functions = line_metadata(source, masked)
    source_lines = source.splitlines()
    rows = []
    for operator, pattern in OPERATORS.items():
        for match in pattern.finditer(masked):
            line_index = bisect.bisect_right(line_starts, match.start()) - 1
            line = line_index + 1
            col = match.start() - line_starts[line_index] + 1
            region = regions[line_index]
            moss_source = moss_sources[line_index]
            function = functions[line_index]
            snippet = " ".join(source_lines[line_index].strip().split())
            if region and moss_source:
                site = f"moss:{region[0]}:{region[1]}:{moss_source}"
            elif region:
                site = f"moss:{region[0]}:{region[1]}:rust:{snippet}"
            else:
                site = f"runtime:{function}:rust:{snippet}"
            rows.append({
                "operator": operator,
                "artifact": path.relative_to(root).as_posix(),
                "line": line,
                "column": col,
                "function": function,
                "moss_region": list(region) if region else None,
                "moss_source_line": moss_source,
                "site_key": site,
                "snippet": snippet,
            })
    return rows


def inventory(root: Path) -> dict:
    # Cargo's dependency/build-script Rust is outside compiler-generated Moss
    # artifacts. Keep provider.rs and fixture artifacts, but exclude build.rs.
    artifacts = sorted(p for p in root.rglob("*.rs")
                       if p.is_file() and p.name != "build.rs")
    rows = [row for path in artifacts for row in inspect_rust(path, root)]
    groups: dict[str, list[dict]] = defaultdict(list)
    for row in rows:
        groups[f"{row['operator']}|{row['site_key']}"].append(row)
    group_rows = []
    for key, members in sorted(groups.items()):
        group_rows.append({
            "group_id": hashlib.sha256(key.encode()).hexdigest()[:16],
            "operator": members[0]["operator"],
            "site_key": members[0]["site_key"],
            "occurrences": len(members),
            "artifacts": len({m["artifact"] for m in members}),
            "examples": members[:3],
        })
    return {
        "artifact_root": str(root),
        "artifact_count": len(artifacts),
        "candidate_occurrences": len(rows),
        "operator_counts": dict(sorted(Counter(r["operator"] for r in rows).items())),
        "site_group_count": len(group_rows),
        "groups": group_rows,
        "occurrences": rows,
        "limitations": [
            "Candidates are observations, not proof of Moss-value duplication.",
            "Static scanning does not expand macros or inspect Rust MIR/Copy loads.",
            "A site grouping is triage based on generated Moss tooling markers and Rust context, not compiler authorization.",
        ],
    }


def render_markdown(data: dict) -> str:
    lines = [
        "# Phase 28 R7-010 Generated Rust Duplication Candidate Inventory",
        "",
        f"Artifacts scanned: **{data['artifact_count']}**",
        f"Candidate occurrences: **{data['candidate_occurrences']}**",
        f"Distinct operator/site groups: **{data['site_group_count']}**",
        "",
        "This is an independent static triage. A scanner hit is not proof of an illegal Moss copy; a clean scan is not proof that no duplication exists.",
        "",
        "## Operator counts",
        "",
        "| Operator | Occurrences |",
        "|---|---:|",
    ]
    for operator, count in data["operator_counts"].items():
        lines.append(f"| `{operator}` | {count} |")
    lines += ["", "## Generation-site groups", "", "| Group | Operator | Occurrences | Artifacts | Site evidence |", "|---|---|---:|---:|---|"]
    for group in data["groups"]:
        site = group["site_key"].replace("|", "\\|")
        lines.append(f"| `{group['group_id']}` | `{group['operator']}` | {group['occurrences']} | {group['artifacts']} | `{site}` |")
    lines += ["", "## Limits", ""]
    lines.extend(f"- {item}" for item in data["limitations"])
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("artifact_root", type=Path)
    parser.add_argument("--json-out", type=Path, required=True)
    parser.add_argument("--markdown-out", type=Path, required=True)
    args = parser.parse_args()
    root = args.artifact_root.resolve()
    if not root.is_dir():
        parser.error(f"artifact root is not a directory: {root}")
    result = inventory(root)
    args.json_out.parent.mkdir(parents=True, exist_ok=True)
    args.markdown_out.parent.mkdir(parents=True, exist_ok=True)
    args.json_out.write_text(json.dumps(result, indent=2) + "\n")
    args.markdown_out.write_text(render_markdown(result))
    print(f"Rust artifacts scanned: {result['artifact_count']}")
    print(f"Duplication candidates: {result['candidate_occurrences']}")
    print(f"Distinct operator/site groups: {result['site_group_count']}")
    print(f"Operator counts: {result['operator_counts']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
