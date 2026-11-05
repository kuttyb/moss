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
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rust_duplication_scanner import OPERATORS, code_tokens, mask_non_code, operator_spans


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
    for operator in OPERATORS:
        for operator_start, operator_end in operator_spans(masked, operator):
            line_index = bisect.bisect_right(line_starts, operator_start) - 1
            line = line_index + 1
            col = operator_start - line_starts[line_index] + 1
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


def inspect_moss_clone_derives(path: Path, root: Path) -> list[dict]:
    """Report Clone derive capabilities declared on generated Moss value types.

    A derive declaration creates a callable duplication implementation; it is
    not itself evidence that any value was cloned. Keep these capabilities
    separate from operator occurrence counts.
    """
    source = path.read_text(errors="replace")
    masked = mask_non_code(source)
    _line_starts, regions, _moss_sources, _functions = line_metadata(source, masked)
    source_lines = source.splitlines()
    line_starts = [0]
    line_starts.extend(i + 1 for i, ch in enumerate(source) if ch == "\n")
    capabilities = []
    derive_pattern = re.compile(r"#\s*\[\s*derive\s*\((.*?)\)\s*\]", re.S)
    for match in derive_pattern.finditer(masked):
        derives = [part.strip() for part in match.group(1).split(",")]
        if "Clone" not in derives:
            continue
        line_index = bisect.bisect_right(line_starts, match.start()) - 1
        region = regions[line_index]
        if not region or region[0] not in {"type", "enum"}:
            continue
        declaration = re.search(
            r"\b(struct|enum)\s+([A-Za-z_][A-Za-z0-9_]*)\b",
            masked[match.end():])
        if not declaration:
            continue
        capabilities.append({
            "artifact": path.relative_to(root).as_posix(),
            "moss_region_kind": region[0],
            "moss_region_symbol": region[1],
            "rust_type": declaration.group(2),
            "rust_kind": declaration.group(1),
            "derive_line": line_index + 1,
            "derive_snippet": " ".join(source_lines[line_index].strip().split()),
            "capability": "derived Clone implementation",
            "is_invocation": False,
        })
    return capabilities


def inspect_moss_clone_derive_uses(path: Path, root: Path,
                                   capabilities: list[dict]) -> list[dict]:
    """Bind clone calls in typed per-fact helper bodies to derived Moss types.

    The fact marker and helper name are locators only. This lexical scan does
    not query checker authority or validate the full helper call graph.
    """
    if not capabilities:
        return []
    source = path.read_text(errors="replace")
    masked = mask_non_code(source)
    tokens = code_tokens(masked)
    clone_spans = operator_spans(masked, "dot_clone")
    line_starts = [0]
    line_starts.extend(i + 1 for i, ch in enumerate(source) if ch == "\n")
    marker_pattern = re.compile(
        r"/\*\s*Moss copy fact call:\s*(typed-copy-fact-v1:[0-9a-f]{64})\s*"
        r"\*/\s*(__moss_copy_value_[0-9a-f]{16})\s*\(")
    markers: dict[str, set[str]] = defaultdict(set)
    for marker in marker_pattern.finditer(source):
        markers[marker.group(2)].add(marker.group(1))

    rows = []
    for index, (token, fn_start, _fn_end) in enumerate(tokens[:-2]):
        if token != "fn" or not tokens[index + 1][0].startswith("__moss_copy_value_"):
            continue
        symbol = tokens[index + 1][0]
        open_paren = index + 2
        if tokens[open_paren][0] != "(":
            continue
        depth = 0
        close_paren = None
        for pos in range(open_paren, len(tokens)):
            if tokens[pos][0] == "(":
                depth += 1
            elif tokens[pos][0] == ")":
                depth -= 1
                if depth == 0:
                    close_paren = pos
                    break
        if close_paren is None:
            continue
        body_open = next((pos for pos in range(close_paren + 1, len(tokens))
                          if tokens[pos][0] == "{"), None)
        if body_open is None:
            continue
        brace_depth = 0
        body_close = None
        for pos in range(body_open, len(tokens)):
            if tokens[pos][0] == "{":
                brace_depth += 1
            elif tokens[pos][0] == "}":
                brace_depth -= 1
                if brace_depth == 0:
                    body_close = pos
                    break
        if body_close is None:
            continue
        signature = masked[fn_start:tokens[body_open][1]]
        matching_types = [cap for cap in capabilities
                          if re.search(r"\b" + re.escape(cap["rust_type"]) + r"\b", signature)]
        if len(matching_types) != 1:
            continue
        capability = matching_types[0]
        body_start = tokens[body_open][1]
        body_end = tokens[body_close][2]
        fact_ids = markers.get(symbol, set())
        fact_id = next(iter(fact_ids)) if len(fact_ids) == 1 else None
        prefix_ok = bool(fact_id and symbol == "__moss_copy_value_" +
                         fact_id.removeprefix("typed-copy-fact-v1:")[:16])
        for clone_start, clone_end in clone_spans:
            if body_start <= clone_start and clone_end <= body_end:
                line_index = bisect.bisect_right(line_starts, clone_start) - 1
                rows.append({
                    "artifact": path.relative_to(root).as_posix(),
                    "moss_region_kind": capability["moss_region_kind"],
                    "moss_region_symbol": capability["moss_region_symbol"],
                    "rust_type": capability["rust_type"],
                    "helper_symbol": symbol,
                    "clone_line": line_index + 1,
                    "clone_column": clone_start - line_starts[line_index] + 1,
                    "fact_marker_locator": fact_id,
                    "helper_prefix_matches_marker": prefix_ok,
                    "authority_status": "not independently verified",
                })
    return rows


def inventory(root: Path) -> dict:
    # Cargo's dependency/build-script Rust is outside compiler-generated Moss
    # artifacts. Keep provider.rs and fixture artifacts, but exclude build.rs.
    artifacts = sorted(p for p in root.rglob("*.rs")
                       if p.is_file() and p.name != "build.rs")
    rows = [row for path in artifacts for row in inspect_rust(path, root)]
    clone_derives = []
    clone_derive_uses = []
    for path in artifacts:
        capabilities = inspect_moss_clone_derives(path, root)
        clone_derives.extend(capabilities)
        clone_derive_uses.extend(inspect_moss_clone_derive_uses(path, root, capabilities))
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
        "moss_clone_derive_capability_count": len(clone_derives),
        "moss_clone_derives": clone_derives,
        "moss_clone_derive_helper_clone_use_count": len(clone_derive_uses),
        "moss_clone_derive_helper_clone_uses": clone_derive_uses,
        "limitations": [
            "Candidates are observations, not proof of Moss-value duplication.",
            "Clone derive declarations are reported as capabilities, not invocations; use sites require separate call-graph/type analysis.",
            "Typed per-fact helper clone uses are associated lexically; fact markers are locators and are not checker authority without the provenance validator.",
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
        f"Generated Moss-value `Clone` derive capabilities: **{data['moss_clone_derive_capability_count']}** (declarations, not invocations)",
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
    lines += ["", "## Generated Moss-value Clone capabilities", "",
              "These `#[derive(Clone)]` declarations make a Rust duplication method available. They are not counted as value-copy occurrences or authorized by themselves.",
              "", "| Artifact | Moss region | Rust type | Derive line |", "|---|---|---|---:|"]
    for row in data["moss_clone_derives"]:
        lines.append(f"| `{row['artifact']}` | `{row['moss_region_kind']}:{row['moss_region_symbol']}` | `{row['rust_type']}` | {row['derive_line']} |")
    lines += ["", "## Clone calls in typed copy helpers", "",
              f"Lexically associated helper-body clone calls: **{data['moss_clone_derive_helper_clone_use_count']}**. Markers are locators; they do not establish authority without independent checker validation.",
              "", "| Artifact | Type | Helper | Rust clone | Fact marker locator | Prefix matches |", "|---|---|---|---|---|---|"]
    for row in data["moss_clone_derive_helper_clone_uses"]:
        location = f"{row['clone_line']}:{row['clone_column']}"
        lines.append(f"| `{row['artifact']}` | `{row['rust_type']}` | `{row['helper_symbol']}` | `{location}` | `{row['fact_marker_locator'] or 'none'}` | {row['helper_prefix_matches_marker']} |")
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
