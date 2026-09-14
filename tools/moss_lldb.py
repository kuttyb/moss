"""Shared Moss source-map helpers and a small LLDB command integration.

The module intentionally depends only on Python's standard library.  It can be
used as a command-line map inspector without LLDB, or imported by LLDB with:

    command script import /path/to/moss_lldb.py
    moss-map-load /path/to/program.mossmap
    moss-break /path/to/program.moss:12
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import sys
from dataclasses import dataclass
from typing import Any, Iterable, Optional


def _path(value: str) -> str:
    return os.path.normcase(os.path.realpath(os.path.expanduser(value)))


@dataclass(frozen=True)
class MossLocation:
    source_file: str
    moss_line: int
    generated_file: str
    generated_line: int
    semantic_identity: str
    construct_kind: str
    generated_symbol: str
    native_symbol: str
    provenance: tuple[str, ...]


class MossDebugMap:
    """Validated, deterministic queries over one compiler-emitted .mossmap."""

    def __init__(self, filename: str, document: dict[str, Any]):
        if document.get("format") != "moss-debug-map":
            raise ValueError(f"{filename}: not a Moss debug map")
        if document.get("version") != 1:
            raise ValueError(
                f"{filename}: unsupported Moss debug-map version "
                f"{document.get('version')!r}"
            )
        self.filename = _path(filename)
        self.document = document
        self.entries: list[dict[str, Any]] = list(document.get("entries", []))

    @classmethod
    def load(cls, filename: str) -> "MossDebugMap":
        with open(filename, "r", encoding="utf-8") as stream:
            return cls(filename, json.load(stream))

    @property
    def executable(self) -> str:
        value = self.document.get("native_executable", "")
        return _path(value) if value else ""

    def _entry_locations(
        self,
        entry: dict[str, Any],
        source_file: str,
        line: int,
        exact_only: bool,
    ) -> Iterable[MossLocation]:
        source = entry.get("source", {})
        if _path(str(source.get("file", ""))) != source_file:
            return
        mappings = entry.get("line_mappings", [])
        exact = [mapping for mapping in mappings if mapping.get("moss_line") == line]
        if exact:
            generated_lines = [int(mapping["generated_rust_line"]) for mapping in exact]
        elif exact_only:
            return
        elif int(source.get("start_line", 0)) <= line <= int(
            source.get("end_line", 0)
        ):
            generated_lines = [int(entry.get("generated", {}).get("start_line", 0))]
        else:
            return
        generated_file = str(entry.get("generated", {}).get("file", ""))
        for generated_line in generated_lines:
            if generated_file and generated_line > 0:
                yield MossLocation(
                    source_file=str(source.get("file", "")),
                    moss_line=line,
                    generated_file=generated_file,
                    generated_line=generated_line,
                    semantic_identity=str(entry.get("semantic_identity", "")),
                    construct_kind=str(entry.get("construct_kind", "")),
                    generated_symbol=str(entry.get("generated_symbol", "")),
                    native_symbol=str(entry.get("native_symbol", "")),
                    provenance=tuple(str(item) for item in entry.get("provenance", [])),
                )

    def resolve(
        self, source_file: str, line: int, exact_only: bool = False
    ) -> list[MossLocation]:
        """Resolve a Moss line, optionally requiring an exact line mapping.

        Range fallback is useful for source navigation and symbol lookup.  It
        must never be used for a debugger breakpoint because doing so would
        claim source precision that the compiler did not emit.
        """
        wanted = _path(source_file)
        locations = [
            location
            for entry in self.entries
            for location in self._entry_locations(
                entry, wanted, line, exact_only
            )
        ]
        kind_order = {
            "functional_node": 0,
            "functional_pipeline": 1,
            "function": 2,
            "method": 2,
            "handler": 2,
            "main": 2,
            "domain": 3,
            "type": 3,
        }
        locations.sort(
            key=lambda item: (
                item.generated_file,
                item.generated_line,
                kind_order.get(item.construct_kind, 9),
                item.semantic_identity,
            )
        )
        unique: list[MossLocation] = []
        seen: set[tuple[str, int]] = set()
        for location in locations:
            coordinate = (_path(location.generated_file), location.generated_line)
            if coordinate not in seen:
                seen.add(coordinate)
                unique.append(location)
        return unique

    def resolve_exact(self, source_file: str, line: int) -> list[MossLocation]:
        """Resolve only compiler-emitted exact Moss-to-generated mappings."""
        return self.resolve(source_file, line, exact_only=True)

    def entry_at(self, source_file: str, line: int) -> Optional[dict[str, Any]]:
        wanted = _path(source_file)
        candidates = []
        for entry in self.entries:
            source = entry.get("source", {})
            if _path(str(source.get("file", ""))) != wanted:
                continue
            start = int(source.get("start_line", 0))
            end = int(source.get("end_line", 0))
            if start <= line <= end:
                candidates.append((end - start, start, str(entry.get("semantic_identity", "")), entry))
        candidates.sort(key=lambda item: item[:3])
        return candidates[0][3] if candidates else None

    def reverse(
        self, generated_file: str, line: int, exact_only: bool = False
    ) -> list[MossLocation]:
        """Reverse a generated line, optionally rejecting range-only origins."""
        wanted = _path(generated_file)
        locations: list[MossLocation] = []
        for entry in self.entries:
            generated = entry.get("generated", {})
            if _path(str(generated.get("file", ""))) != wanted:
                continue
            mappings = [
                mapping
                for mapping in entry.get("line_mappings", [])
                if int(mapping.get("generated_rust_line", 0)) == line
            ]
            if not mappings and (exact_only or not (
                int(generated.get("start_line", 0))
                <= line
                <= int(generated.get("end_line", 0))
            )):
                continue
            moss_lines = [int(item["moss_line"]) for item in mappings]
            if not moss_lines:
                moss_lines = [int(entry.get("source", {}).get("start_line", 0))]
            for moss_line in moss_lines:
                if moss_line <= 0:
                    continue
                source = entry.get("source", {})
                locations.append(
                    MossLocation(
                        source_file=str(source.get("file", "")),
                        moss_line=moss_line,
                        generated_file=str(generated.get("file", "")),
                        generated_line=line,
                        semantic_identity=str(entry.get("semantic_identity", "")),
                        construct_kind=str(entry.get("construct_kind", "")),
                        generated_symbol=str(entry.get("generated_symbol", "")),
                        native_symbol=str(entry.get("native_symbol", "")),
                        provenance=tuple(
                            str(item) for item in entry.get("provenance", [])
                        ),
                    )
                )
        locations.sort(key=lambda item: (item.moss_line, item.semantic_identity))
        return locations

    def reverse_exact(self, generated_file: str, line: int) -> list[MossLocation]:
        """Return only compiler-emitted exact generated-to-Moss mappings."""
        return self.reverse(generated_file, line, exact_only=True)


_active_map: Optional[MossDebugMap] = None


def _lldb_error(result: Any, message: str) -> None:
    if hasattr(result, "SetError"):
        result.SetError(message)
    else:
        print(message, file=sys.stderr)


def moss_map_load(debugger: Any, command: str, result: Any, _internal: Any) -> None:
    """LLDB command: load the one shared map used by all Moss commands."""
    del debugger
    global _active_map
    try:
        arguments = shlex.split(command)
        if len(arguments) != 1:
            raise ValueError("usage: moss-map-load PROGRAM.mossmap")
        _active_map = MossDebugMap.load(arguments[0])
        result.AppendMessage(f"loaded Moss debug map: {_active_map.filename}")
    except FileNotFoundError as error:
        _lldb_error(result, f"Moss debug map not found: {error.filename}")
    except (OSError, ValueError, json.JSONDecodeError) as error:
        _lldb_error(result, str(error))


def moss_break(debugger: Any, command: str, result: Any, _internal: Any) -> None:
    """LLDB command: translate one Moss file:line breakpoint deterministically."""
    if _active_map is None:
        _lldb_error(result, "load a Moss map first with moss-map-load")
        return
    try:
        arguments = shlex.split(command)
        if len(arguments) != 1:
            raise ValueError("usage: moss-break FILE.moss:LINE")
        location, separator, line_text = arguments[0].rpartition(":")
        if not separator or not location:
            raise ValueError("usage: moss-break FILE.moss:LINE")
        line = int(line_text)
        matches = _active_map.resolve_exact(location, line)
        if not matches:
            raise ValueError(
                f"breakpoint has no exact Moss mapping: {location}:{line}"
            )
        selected = matches[0]
        target = debugger.GetSelectedTarget()
        breakpoint = target.BreakpointCreateByLocation(
            selected.generated_file, selected.generated_line
        )
        location_count = breakpoint.GetNumLocations()
        if location_count == 0:
            raise ValueError(
                "exact Moss mapping has no native breakpoint location: "
                f"{location}:{line}"
            )
        result.AppendMessage(
            f"Moss breakpoint {location}:{line} -> "
            f"{selected.generated_file}:{selected.generated_line} "
            f"(breakpoint {breakpoint.GetID()}, {location_count} native "
            f"location{'s' if location_count != 1 else ''}, "
            f"{selected.semantic_identity})"
        )
        if len(matches) > 1:
            result.AppendMessage(
                f"selected the first of {len(matches)} generated locations "
                "in deterministic file/line order"
            )
    except (TypeError, ValueError) as error:
        _lldb_error(result, str(error))


def _selected_frame(debugger: Any) -> Any:
    target = debugger.GetSelectedTarget()
    process = target.GetProcess()
    thread = process.GetSelectedThread()
    return thread.GetSelectedFrame()


def _lldb_file_spec_path(file_spec: Any) -> str:
    """Return an SBFileSpec path across LLDB Python binding versions.

    Some bindings expose GetPath() as a convenient zero-argument method, while
    Debian's LLDB 19 binding retains the underlying destination-buffer
    signature.  GetDirectory()/GetFilename() are stable in both forms.
    """
    directory = file_spec.GetDirectory() or ""
    filename = file_spec.GetFilename() or ""
    if directory and filename:
        return os.path.join(str(directory), str(filename))
    if filename:
        return str(filename)
    return str(file_spec)


def moss_where(debugger: Any, _command: str, result: Any, _internal: Any) -> None:
    """LLDB command: display the Moss origins for the selected native frame."""
    if _active_map is None:
        _lldb_error(result, "load a Moss map first with moss-map-load")
        return
    frame = _selected_frame(debugger)
    line_entry = frame.GetLineEntry()
    file_spec = line_entry.GetFileSpec()
    filename = _lldb_file_spec_path(file_spec)
    matches = _active_map.reverse_exact(filename, line_entry.GetLine())
    if not matches:
        result.AppendMessage("selected frame has no Moss provenance")
        return
    for match in matches:
        origins = ", ".join(match.provenance) if match.provenance else match.semantic_identity
        result.AppendMessage(
            f"{match.source_file}:{match.moss_line} "
            f"[{match.construct_kind}] {origins}"
        )


def moss_stack(debugger: Any, command: str, result: Any, _internal: Any) -> None:
    """LLDB command: show Moss-mapped frames; pass --all for runtime frames."""
    if _active_map is None:
        _lldb_error(result, "load a Moss map first with moss-map-load")
        return
    show_all = command.strip() == "--all"
    target = debugger.GetSelectedTarget()
    thread = target.GetProcess().GetSelectedThread()
    shown = 0
    for index in range(thread.GetNumFrames()):
        frame = thread.GetFrameAtIndex(index)
        line_entry = frame.GetLineEntry()
        filename = _lldb_file_spec_path(line_entry.GetFileSpec())
        matches = _active_map.reverse_exact(filename, line_entry.GetLine())
        if matches:
            match = matches[0]
            result.AppendMessage(
                f"#{index} {match.semantic_identity} at "
                f"{match.source_file}:{match.moss_line}"
            )
            shown += 1
        elif show_all:
            result.AppendMessage(f"#{index} {frame.GetDisplayFunctionName() or '<runtime>'}")
            shown += 1
    if shown == 0:
        result.AppendMessage("no Moss-mapped frames")


def __lldb_init_module(debugger: Any, _internal: Any) -> None:
    module = __name__
    debugger.HandleCommand(f"command script add -f {module}.moss_map_load moss-map-load")
    debugger.HandleCommand(f"command script add -f {module}.moss_break moss-break")
    debugger.HandleCommand(f"command script add -f {module}.moss_where moss-where")
    debugger.HandleCommand(f"command script add -f {module}.moss_stack moss-stack")
    print("Moss LLDB commands installed: moss-map-load, moss-break, moss-where, moss-stack")


def _location_json(location: MossLocation) -> dict[str, Any]:
    return {
        "source_file": location.source_file,
        "moss_line": location.moss_line,
        "generated_file": location.generated_file,
        "generated_line": location.generated_line,
        "semantic_identity": location.semantic_identity,
        "construct_kind": location.construct_kind,
        "generated_symbol": location.generated_symbol,
        "native_symbol": location.native_symbol,
        "provenance": list(location.provenance),
    }


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="inspect a compiler-emitted Moss map")
    subparsers = parser.add_subparsers(dest="command", required=True)
    resolve = subparsers.add_parser("resolve", help="translate Moss source to Rust")
    resolve.add_argument("map")
    resolve.add_argument("source")
    resolve.add_argument("line", type=int)
    resolve.add_argument(
        "--exact", action="store_true", help="reject range-only source mappings"
    )
    reverse = subparsers.add_parser("reverse", help="translate generated Rust to Moss")
    reverse.add_argument("map")
    reverse.add_argument("generated")
    reverse.add_argument("line", type=int)
    reverse.add_argument(
        "--exact", action="store_true", help="reject range-only source mappings"
    )
    symbol = subparsers.add_parser("symbol", help="find the native symbol at Moss source")
    symbol.add_argument("map")
    symbol.add_argument("source")
    symbol.add_argument("line", type=int)
    arguments = parser.parse_args(argv)
    debug_map = MossDebugMap.load(arguments.map)
    if arguments.command == "resolve":
        values = debug_map.resolve(
            arguments.source, arguments.line, exact_only=arguments.exact
        )
        print(json.dumps([_location_json(item) for item in values], indent=2))
        return 0 if values else 1
    if arguments.command == "reverse":
        values = debug_map.reverse(
            arguments.generated, arguments.line, exact_only=arguments.exact
        )
        print(json.dumps([_location_json(item) for item in values], indent=2))
        return 0 if values else 1
    entry = debug_map.entry_at(arguments.source, arguments.line)
    if not entry or not entry.get("native_symbol"):
        return 1
    print(entry["native_symbol"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
