#!/usr/bin/env python3
"""Structural regression checks for the shared Phase 5 Moss debug map."""

from __future__ import annotations

import importlib.util
import json
import pathlib
import sys


def fail(message: str) -> None:
    raise SystemExit(f"test failure: {message}")


if len(sys.argv) != 5:
    raise SystemExit("usage: check_debug_map.py O0.map OPT.map DEBUG.map SOURCE.moss")

o0_path, optimized_path, debug_path, source_path = map(pathlib.Path, sys.argv[1:])
documents = []
for filename in (o0_path, optimized_path, debug_path):
    with filename.open(encoding="utf-8") as stream:
        document = json.load(stream)
    if document.get("format") != "moss-debug-map" or document.get("version") != 1:
        fail(f"{filename} does not implement debug-map version 1")
    documents.append(document)

o0, optimized, debug = documents
if o0.get("optimized") or not optimized.get("optimized"):
    fail("optimized and reference maps do not identify their lowering mode")
if not debug.get("debug_build") or debug.get("optimized"):
    fail("debug map is not marked as an unoptimized debug build")

required_kinds = {
    "function",
    "method",
    "handler",
    "domain",
    "type",
    "main",
    "functional_pipeline",
    "functional_node",
}
for document in documents:
    entries = document.get("entries", [])
    kinds = {entry.get("construct_kind") for entry in entries}
    if not required_kinds <= kinds:
        fail(f"map omitted construct kinds: {sorted(required_kinds - kinds)}")
    identities = [entry.get("semantic_identity", "") for entry in entries]
    if len(identities) != len(set(identities)):
        fail("map contains duplicate stable semantic identities")
    for entry in entries:
        source = entry["source"]
        generated = entry["generated"]
        if source["start_line"] <= 0:
            fail(f"entry lacks a real Moss source line: {entry['semantic_identity']}")
        if generated["start_line"] <= 0:
            fail(f"entry lacks generated Rust provenance: {entry['semantic_identity']}")
        if any(key in entry for key in ("transient_id", "ir_id", "pipeline_id")):
            fail("map exposed a traversal-order compiler ID")

stable_sets = [
    {entry["semantic_identity"] for entry in document["entries"]}
    for document in documents
]
if not (stable_sets[0] == stable_sets[1] == stable_sets[2]):
    fail("stable semantic identities changed across lowering modes")

for identity in stable_sets[0]:
    symbols = {
        next(
            entry["native_symbol"]
            for entry in document["entries"]
            if entry["semantic_identity"] == identity
        )
        for document in documents
    }
    if len(symbols) != 1:
        fail(f"native symbol changed across lowering modes: {identity}")

debug_normalize = next(
    entry
    for entry in debug["entries"]
    if entry["semantic_identity"] == "fn:normalize@7"
)
if not debug_normalize["native_symbol"].startswith("moss__function__normalize__"):
    fail("debug function lacks its readable deterministic native symbol")
for kind in ("method", "handler", "main"):
    concrete = next(entry for entry in debug["entries"] if entry["construct_kind"] == kind)
    if not concrete["native_symbol"]:
        fail(f"debug {kind} lacks a concrete native symbol")

stage_prefix = "fn:summarize<vector[int]>@12:expression:0:stage:"
eager_stages = [
    entry
    for entry in o0["entries"]
    if entry["semantic_identity"].startswith(stage_prefix)
]
fused_stages = [
    entry
    for entry in optimized["entries"]
    if entry["semantic_identity"].startswith(stage_prefix)
]
if len(fused_stages) != 3:
    fail("optimized pipeline did not retain its three stage origins")
if len({entry["generated"]["start_line"] for entry in eager_stages}) != 3:
    fail("-O0 pipeline stages do not retain distinct eager debug boundaries")
generated_ranges = {
    (
        entry["generated"]["start_line"],
        entry["generated"]["end_line"],
        entry["generated_symbol"],
    )
    for entry in fused_stages
}
if len(generated_ranges) != 1:
    fail("fused stages do not share one many-to-one generated region")
expected_origins = {entry["semantic_identity"] for entry in fused_stages}
for entry in fused_stages:
    if not expected_origins <= set(entry["provenance"]):
        fail("fused stage did not retain all contributing Moss origins")

repo = pathlib.Path(__file__).resolve().parents[2]
module_path = repo / "tools" / "moss_lldb.py"
spec = importlib.util.spec_from_file_location("moss_lldb", module_path)
if spec is None or spec.loader is None:
    fail("cannot load the shared LLDB/map resolver")
module = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = module
spec.loader.exec_module(module)
resolver = module.MossDebugMap.load(str(debug_path))
locations = resolver.resolve(str(source_path), 8)
if not locations:
    fail("Moss-to-Rust source resolution returned no location")
reverse = resolver.reverse(locations[0].generated_file, locations[0].generated_line)
if not any(location.moss_line == 8 for location in reverse):
    fail("generated-Rust-to-Moss reverse resolution lost the source line")

print("Phase 5 debug-map checks passed")
