#!/usr/bin/env python3
"""Structural regression checks for the shared Phase 5 Moss debug map."""

from __future__ import annotations

import importlib.util
import json
import pathlib
import sys


def fail(message: str) -> None:
    raise SystemExit(f"test failure: {message}")


if len(sys.argv) != 7:
    raise SystemExit(
        "usage: check_debug_map.py O0.map OPT.map DEBUG.map SOURCE.moss "
        "PHASE45.map PHASE45_SOURCE.moss"
    )

(
    o0_path,
    optimized_path,
    debug_path,
    source_path,
    phase45_path,
    phase45_source_path,
) = map(pathlib.Path, sys.argv[1:])
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
        fail("map contains duplicate source/provenance identities")
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
    fail("source/provenance identities changed across lowering modes")

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


class FakeFileSpec:
    def GetDirectory(self) -> str:
        return "/tmp/generated"

    def GetFilename(self) -> str:
        return "program.rs"


if module._lldb_file_spec_path(FakeFileSpec()) != "/tmp/generated/program.rs":
    fail("LLDB file-spec compatibility path assembly failed")

resolver = module.MossDebugMap.load(str(debug_path))
locations = resolver.resolve(str(source_path), 8)
if not locations:
    fail("Moss-to-Rust source resolution returned no location")
reverse = resolver.reverse(locations[0].generated_file, locations[0].generated_line)
if not any(location.moss_line == 8 for location in reverse):
    fail("generated-Rust-to-Moss reverse resolution lost the source line")

expected_exact_kinds = {5: "method", 8: "function", 21: "handler"}
for moss_line, expected_kind in expected_exact_kinds.items():
    exact = resolver.resolve_exact(str(source_path), moss_line)
    if not exact or exact[0].construct_kind != expected_kind:
        fail(f"exact breakpoint mapping lost {expected_kind} line {moss_line}")
    if not exact[0].native_symbol:
        fail(f"exact {expected_kind} breakpoint has no native symbol")

function_lines = [
    resolver.resolve_exact(str(source_path), moss_line)[0]
    for moss_line in (7, 8, 9)
]
if len({location.native_symbol for location in function_lines}) != 1:
    fail("multiple Moss function lines did not retain one generated function")

main_entry = next(
    entry for entry in debug["entries"] if entry["construct_kind"] == "main"
)
mapped_main_lines = {
    mapping["moss_line"] for mapping in main_entry.get("line_mappings", [])
}
range_only_line = next(
    line
    for line in range(
        main_entry["source"]["start_line"], main_entry["source"]["end_line"] + 1
    )
    if line not in mapped_main_lines
)
if not resolver.resolve(str(source_path), range_only_line):
    fail("range provenance is unavailable for ordinary source navigation")
if resolver.resolve_exact(str(source_path), range_only_line):
    fail("range-only Moss line was incorrectly accepted as an exact breakpoint")

optimized_resolver = module.MossDebugMap.load(str(optimized_path))
fused_locations = [
    optimized_resolver.resolve_exact(str(source_path), moss_line)[0]
    for moss_line in (13, 14, 15)
]
if len({location.generated_line for location in fused_locations}) != 1:
    fail("fused pipeline origins do not share their generated loop location")
fused_origins = optimized_resolver.reverse_exact(
    fused_locations[0].generated_file, fused_locations[0].generated_line
)
if not {13, 14, 15} <= {location.moss_line for location in fused_origins}:
    fail("fused generated loop did not reverse-map to every functional stage")

phase45_resolver = module.MossDebugMap.load(str(phase45_path))
phase45_group = next(
    (
        entry
        for entry in phase45_resolver.entries
        if entry["construct_kind"] == "functional_dataflow_group"
    ),
    None,
)
if phase45_group is None:
    fail("Phase 4.5 shared traversal has no dataflow-group provenance")
phase45_locations = [
    phase45_resolver.resolve_exact(str(phase45_source_path), moss_line)[0]
    for moss_line in (3, 4, 5)
]
if len({location.generated_line for location in phase45_locations}) != 1:
    fail("Phase 4.5 DAG consumers do not share their generated traversal location")
if len(phase45_group.get("provenance", [])) < 6:
    fail("Phase 4.5 shared traversal lost contributing pipeline provenance")


class FakeLineEntry:
    def __init__(self, location):
        self.location = location

    def GetFileSpec(self):
        path = pathlib.Path(self.location.generated_file)

        class FileSpec:
            def GetDirectory(self):
                return str(path.parent)

            def GetFilename(self):
                return path.name

        return FileSpec()

    def GetLine(self):
        return self.location.generated_line


class FakeRuntimeLineEntry:
    def GetFileSpec(self):
        class FileSpec:
            def GetDirectory(self):
                return "/rustc/library/std"

            def GetFilename(self):
                return "runtime.rs"

        return FileSpec()

    def GetLine(self):
        return 99


class FakeFrame:
    def __init__(self, line_entry, name):
        self.line_entry = line_entry
        self.name = name

    def GetLineEntry(self):
        return self.line_entry

    def GetDisplayFunctionName(self):
        return self.name


class FakeThread:
    def __init__(self, frames):
        self.frames = frames

    def GetNumFrames(self):
        return len(self.frames)

    def GetFrameAtIndex(self, index):
        return self.frames[index]

    def GetSelectedFrame(self):
        return self.frames[0]


class FakeProcess:
    def __init__(self, thread):
        self.thread = thread

    def GetSelectedThread(self):
        return self.thread


class FakeTarget:
    def __init__(self, process):
        self.process = process

    def GetProcess(self):
        return self.process


class FakeDebugger:
    def __init__(self, target):
        self.target = target

    def GetSelectedTarget(self):
        return self.target


class FakeResult:
    def __init__(self):
        self.messages = []
        self.error = ""

    def AppendMessage(self, message):
        self.messages.append(message)

    def SetError(self, message):
        self.error = message


missing_map = FakeResult()
module.moss_map_load(
    None, '"/tmp/moss-phase5-definitely-missing.mossmap"', missing_map, None
)
if "Moss debug map not found" not in missing_map.error:
    fail("missing map did not produce the focused Moss tooling diagnostic")

frames = [
    FakeFrame(FakeLineEntry(resolver.resolve_exact(str(source_path), line)[0]), name)
    for line, name in (
        (8, "moss__function__normalize"),
        (5, "moss__method__Meter__doubled"),
        (21, "moss__handler__Counter__Add"),
    )
]
frames.append(FakeFrame(FakeRuntimeLineEntry(), "std::rt::lang_start"))
debugger = FakeDebugger(FakeTarget(FakeProcess(FakeThread(frames))))
module._active_map = resolver
filtered = FakeResult()
module.moss_stack(debugger, "", filtered, None)
filtered_text = "\n".join(filtered.messages)
for identity in (
    "fn:normalize@7",
    "method:Meter.doubled@4",
    "handler:Counter.Add@20",
):
    if identity not in filtered_text:
        fail(f"Moss stack filtering lost user frame {identity}")
if "std::rt::lang_start" in filtered_text:
    fail("Moss stack filtering exposed a runtime frame by default")
unfiltered = FakeResult()
module.moss_stack(debugger, "--all", unfiltered, None)
if "std::rt::lang_start" not in "\n".join(unfiltered.messages):
    fail("moss-stack --all did not expose the generated/runtime frame")

invalid_break = FakeResult()
module.moss_break(
    debugger, f'"{source_path}:{range_only_line}"', invalid_break, None
)
if "breakpoint has no exact Moss mapping" not in invalid_break.error:
    fail("range-only breakpoint did not produce the focused Moss diagnostic")

print("Phase 5 debug-map checks passed")
