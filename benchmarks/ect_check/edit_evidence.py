"""Query evidence for the B3 edits (Phase 28.5, Team 3).

For one edit scenario: write the fixture's source before and after the frozen
patch to standalone files, ask the compiler (`moss inspect fn:<name>`, which
returns type, ownership, effects, call edges and the typed specialization
records) about EVERY callable in both states, and report which callables'
queried facts changed. This is independent of the generator metadata and of the
runner's counters: the changed set is what the compiler itself says moved.

Per callable the facts are split into
  body          implementation_hash
  summary       semantic_interface_hash, resolved type, ownership, transitive
                effects, and the callable's specialization records (result /
                parameter type keys, observable effects, leaf effects)
  provenance    source lines of calls and of member-requirement origins
Everything else (file paths, @line identities, call-site ids) is dropped.
`ideal` is the callable set a perfect incremental checker would recheck, from
the SOURCE call graph (reverse reachability), again not from generator metadata.
"""
from __future__ import annotations

import concurrent.futures as cf
import json
from pathlib import Path
import re
import shutil
import subprocess

import ect_common as C


def callables(source: str) -> list[str]:
    return [n for n in re.findall(r"^fn (\w+)\(", source, re.M) if n != "main"]


def call_graph(source: str) -> dict[str, set[str]]:
    """callee sets per top-level callable, from the source text only."""
    names = callables(source)
    graph = {}
    chunks = re.split(r"^(?=fn \w+\()", source, flags=re.M)
    for chunk in chunks:
        m = re.match(r"fn (\w+)\(", chunk)
        if not m or m.group(1) == "main":
            continue
        body = chunk[m.end():]
        graph[m.group(1)] = {n for n in names if n != m.group(1) and re.search(rf"\b{n}\(", body)}
    return graph


def cone(graph: dict[str, set[str]], target: str) -> set[str]:
    """target plus every transitive caller."""
    result, frontier = {target}, [target]
    while frontier:
        cur = frontier.pop()
        for fn, callees in graph.items():
            if cur in callees and fn not in result:
                result.add(fn)
                frontier.append(fn)
    return result


def _query(moss: Path, source: Path, name: str) -> dict:
    p = subprocess.run([str(moss), "inspect", f"fn:{name}", "--source", str(source), "--json"],
                       capture_output=True, text=True)
    doc = json.loads(p.stdout)
    if not doc.get("ok"):
        raise C.ToolError(f"inspect fn:{name} failed on {source}: {str(doc.get('error'))[:160]}")
    return doc["result"]


SPEC_KEEP = ("result_type_key", "parameter_type_keys", "receiver_type_key", "observable_effects",
             "parameter_leaf_effects", "dependency_count", "open_result_bindings")


def facts(result: dict, name: str) -> dict:
    t = result["target"]
    specs = []
    prov = [[c.get("line") for c in result.get("direct_calls", [])]]
    for sp in result["typed_semantic_v1"]["specializations"]:
        if sp["callable_id"].endswith(f":fn:{name}"):
            specs.append({k: sp.get(k) for k in SPEC_KEEP})
            prov.append([m.get("origin", {}).get("line") for m in sp.get("member_requirements", [])])
    return {
        "body": t.get("implementation_hash"),
        "summary": {"iface": t.get("semantic_interface_hash"), "type": t.get("type"),
                    "ownership": result.get("ownership"),
                    "effects": (result.get("effect_summary") or {}).get("transitive"),
                    "specializations": sorted(specs, key=lambda s: json.dumps(s, sort_keys=True))},
        "provenance": prov}


def collect(moss: Path, source: Path, names: list[str], workers: int = 8) -> dict[str, dict]:
    with cf.ThreadPoolExecutor(workers) as pool:
        results = list(pool.map(lambda n: facts(_query(moss, source, n), n), names))
    return dict(zip(names, results))


def _compress(names: list[str]) -> str:
    return ", ".join(names) if len(names) <= 6 else f"{names[0]} .. {names[-1]} ({len(names)} callables)"


def evidence_for(sc: dict, fx: dict, moss: Path, scratch: Path) -> dict:
    root = scratch / sc["id"]
    if root.exists():
        shutil.rmtree(root)
    C.stage_fixture(fx, root / "base")
    shutil.copytree(root / "base", root / "edit")
    for patch in sc["patches"].values():
        C.apply_patch(root / "edit", patch, "forward")
    rel = next(iter(sc["patches"].values()))["edits"][0]["file"]
    base_src, edit_src = root / "base" / rel, root / "edit" / rel
    # Standalone copies: the fixtures are single-file programs; querying them
    # outside the package keeps the queries independent of margo.
    base_file, edit_file = root / "base.moss", root / "edit.moss"
    shutil.copy(base_src, base_file)
    shutil.copy(edit_src, edit_file)
    text = edit_file.read_text()
    names = callables(text)
    before, after = collect(moss, base_file, names), collect(moss, edit_file, names)
    body, summary, prov = [], [], []
    for n in names:
        if before[n]["body"] != after[n]["body"]:
            body.append(n)
        if before[n]["summary"] != after[n]["summary"]:
            summary.append(n)
        elif before[n]["provenance"] != after[n]["provenance"]:
            prov.append(n)
    edited = sc["edited_function"]
    graph = call_graph(text)
    cone_set = cone(graph, edited)
    shutil.rmtree(root, ignore_errors=True)
    kind = sc.get("classification")
    ideal = {"local": {edited}, "cone": cone_set, "none": set()}[kind]
    order = {n: i for i, n in enumerate(names)}
    sort = lambda xs: sorted(xs, key=order.get)
    return {
        "scenario": sc["id"], "edited_function": edited, "callables": len(names),
        "classification": kind,
        "body_changed": sort(body), "summary_changed": sort(summary),
        "provenance_only_changed": sort(prov),
        "source_cone": sort(cone_set), "ideal_recheck": sort(ideal),
        # The compiler's own answer matches the source-graph cone for consumed
        # edits, and shows no summary change outside the edited callable otherwise.
        "summary_changed_matches_cone": (set(summary) == cone_set) if kind == "cone" else None,
        "callers_summary_unchanged": (kind != "cone") and not (set(summary) - {edited}),
        "summary": f"body: {_compress(sort(body)) or 'none'}; summary facts: "
                   f"{_compress(sort(summary)) or 'none'}; provenance only: "
                   f"{_compress(sort(prov)) or 'none'}",
    }
