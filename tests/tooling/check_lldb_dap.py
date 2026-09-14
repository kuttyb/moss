#!/usr/bin/env python3
"""Exercise the Moss debug contract through a real lldb-dap session."""

from __future__ import annotations

import importlib.util
import json
import os
import pathlib
import select
import subprocess
import sys
import time
from typing import Any, Callable


def fail(message: str) -> None:
    raise RuntimeError(message)


class DapClient:
    def __init__(self, command: str, cwd: pathlib.Path):
        self.process = subprocess.Popen(
            [command],
            cwd=cwd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.sequence = 0
        self.buffer = bytearray()
        self.pending: list[dict[str, Any]] = []

    def request(self, command: str, arguments: dict[str, Any]) -> int:
        self.sequence += 1
        message = {
            "seq": self.sequence,
            "type": "request",
            "command": command,
            "arguments": arguments,
        }
        payload = json.dumps(message, separators=(",", ":")).encode("utf-8")
        framed = f"Content-Length: {len(payload)}\r\n\r\n".encode("ascii") + payload
        if self.process.stdin is None:
            fail("lldb-dap stdin is unavailable")
        self.process.stdin.write(framed)
        self.process.stdin.flush()
        if os.environ.get("MOSS_DAP_TRACE"):
            print(f"> {message}", file=sys.stderr)
        return self.sequence

    def _message_from_buffer(self) -> dict[str, Any] | None:
        header_end = self.buffer.find(b"\r\n\r\n")
        if header_end < 0:
            return None
        headers = self.buffer[:header_end].decode("ascii").split("\r\n")
        lengths = [
            int(header.split(":", 1)[1].strip())
            for header in headers
            if header.lower().startswith("content-length:")
        ]
        if len(lengths) != 1:
            fail("lldb-dap emitted a malformed Content-Length header")
        body_start = header_end + 4
        body_end = body_start + lengths[0]
        if len(self.buffer) < body_end:
            return None
        body = bytes(self.buffer[body_start:body_end])
        del self.buffer[:body_end]
        return json.loads(body.decode("utf-8"))

    def receive(self, timeout: float = 20.0) -> dict[str, Any]:
        deadline = time.monotonic() + timeout
        while True:
            message = self._message_from_buffer()
            if message is not None:
                if os.environ.get("MOSS_DAP_TRACE"):
                    print(f"< {message}", file=sys.stderr)
                return message
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                fail("timed out waiting for lldb-dap")
            if self.process.stdout is None:
                fail("lldb-dap stdout is unavailable")
            ready, _, _ = select.select([self.process.stdout], [], [], remaining)
            if not ready:
                fail("timed out waiting for lldb-dap")
            chunk = os.read(self.process.stdout.fileno(), 65536)
            if not chunk:
                stderr = b""
                if self.process.stderr is not None:
                    stderr = self.process.stderr.read()
                fail(
                    "lldb-dap closed its output: "
                    + stderr.decode("utf-8", errors="replace").strip()
                )
            self.buffer.extend(chunk)

    def wait_for(
        self, predicate: Callable[[dict[str, Any]], bool], timeout: float = 20.0
    ) -> dict[str, Any]:
        deadline = time.monotonic() + timeout
        while True:
            for index, message in enumerate(self.pending):
                if predicate(message):
                    return self.pending.pop(index)
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                fail("timed out waiting for the expected lldb-dap message")
            message = self.receive(remaining)
            if predicate(message):
                return message
            self.pending.append(message)

    def response(self, request_sequence: int) -> dict[str, Any]:
        response = self.wait_for(
            lambda message: message.get("type") == "response"
            and message.get("request_seq") == request_sequence
        )
        if not response.get("success"):
            label = (
                "DAP launch"
                if response.get("command") == "launch"
                else f"lldb-dap request {response.get('command')}"
            )
            fail(
                f"{label} failed: "
                f"{response.get('message', 'unknown error')}"
            )
        return response

    def event(self, name: str) -> dict[str, Any]:
        return self.wait_for(
            lambda message: message.get("type") == "event"
            and message.get("event") == name
        )

    def event_one_of(self, names: set[str]) -> dict[str, Any]:
        return self.wait_for(
            lambda message: message.get("type") == "event"
            and message.get("event") in names
        )

    def output_text(self) -> str:
        """Return output events observed while awaiting protocol milestones."""
        return "".join(
            str(message.get("body", {}).get("output", ""))
            for message in self.pending
            if message.get("type") == "event" and message.get("event") == "output"
        )

    def close(self) -> None:
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)


def load_map_module(repo: pathlib.Path) -> Any:
    module_path = repo / "tools" / "moss_lldb.py"
    spec = importlib.util.spec_from_file_location("moss_lldb_dap_test", module_path)
    if spec is None or spec.loader is None:
        fail("cannot load the shared Moss debug-map resolver")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def stack_frames(client: DapClient, thread_id: int) -> list[dict[str, Any]]:
    request = client.request(
        "stackTrace", {"threadId": thread_id, "startFrame": 0, "levels": 100}
    )
    frames = client.response(request).get("body", {}).get("stackFrames", [])
    if not frames:
        fail("lldb-dap returned no stack frame at a Moss breakpoint")
    return frames


def validate_stop(
    client: DapClient,
    resolver: Any,
    source: pathlib.Path,
    stopped: dict[str, Any],
    moss_line: int,
    symbol_fragment: str,
    reason: str = "breakpoint",
) -> tuple[int, dict[str, Any], list[dict[str, Any]]]:
    body = stopped.get("body", {})
    if body.get("reason") != reason:
        fail(f"expected {reason} stop, received {body.get('reason')!r}")
    thread_id = int(body.get("threadId", 0))
    if thread_id <= 0:
        fail("lldb-dap stopped without identifying a thread")
    frames = stack_frames(client, thread_id)
    frame = frames[0]
    if symbol_fragment not in str(frame.get("name", "")):
        fail(f"unexpected native frame symbol: {frame.get('name')!r}")
    generated_file = str(frame.get("source", {}).get("path", ""))
    generated_line = int(frame.get("line", 0))
    origins = resolver.reverse_exact(generated_file, generated_line)
    if not any(
        pathlib.Path(origin.source_file).resolve() == source
        and origin.moss_line == moss_line
        for origin in origins
    ):
        fail(
            f"stopped native frame does not map exactly to Moss source line "
            f"{moss_line}"
        )
    return thread_id, frame, frames


def variables_for_reference(
    client: DapClient, variables_reference: int
) -> list[dict[str, Any]]:
    request = client.request(
        "variables", {"variablesReference": variables_reference}
    )
    return client.response(request).get("body", {}).get("variables", [])


def frame_locals(client: DapClient, frame: dict[str, Any]) -> dict[str, dict[str, Any]]:
    request = client.request("scopes", {"frameId": int(frame["id"])})
    scopes = client.response(request).get("body", {}).get("scopes", [])
    locals_scope = next(
        (scope for scope in scopes if scope.get("presentationHint") == "locals"),
        None,
    )
    if locals_scope is None:
        fail("lldb-dap did not expose a Locals scope")
    variables = variables_for_reference(
        client, int(locals_scope["variablesReference"])
    )
    return {str(variable.get("name", "")): variable for variable in variables}


def variable_tree_text(
    client: DapClient, variable: dict[str, Any], depth: int = 5
) -> str:
    """Return LLDB's raw value tree without requiring Rust pretty-printers."""
    pieces = [
        str(variable.get("name", "")),
        str(variable.get("type", "")),
        str(variable.get("value", "")),
    ]
    reference = int(variable.get("variablesReference", 0))
    if reference > 0 and depth > 0:
        for child in variables_for_reference(client, reference):
            pieces.append(variable_tree_text(client, child, depth - 1))
    return " ".join(pieces)


def continue_to_stop(client: DapClient, thread_id: int) -> dict[str, Any]:
    request = client.request("continue", {"threadId": thread_id})
    client.response(request)
    return client.event("stopped")


def main() -> int:
    if len(sys.argv) != 5:
        raise SystemExit(
            "usage: check_lldb_dap.py LLDB-DAP EXECUTABLE MAP SOURCE.moss"
        )
    adapter = os.path.realpath(sys.argv[1])
    executable = pathlib.Path(sys.argv[2]).resolve()
    map_file = pathlib.Path(sys.argv[3]).resolve()
    source = pathlib.Path(sys.argv[4]).resolve()
    repo = pathlib.Path(__file__).resolve().parents[2]
    bridge = (repo / "tools" / "moss_lldb.py").resolve()
    if not os.path.isfile(adapter) or not os.access(adapter, os.X_OK):
        fail(f"lldb-dap not found or not executable: {adapter}")
    if not executable.is_file() or not os.access(executable, os.X_OK):
        fail(f"Moss debug executable not found: {executable}")
    if not map_file.is_file():
        fail(f"Moss debug map not found: {map_file}")
    if not bridge.is_file():
        fail(f"Moss LLDB Python helper not found: {bridge}")
    map_module = load_map_module(repo)
    resolver = map_module.MossDebugMap.load(str(map_file))
    # moss-debug sorts pending source overlays before constructing the same
    # preRunCommands vector.
    breakpoint_lines = [5, 8, 21, 25, 38]
    client = DapClient(adapter, repo)
    try:
        initialize = client.request(
            "initialize",
            {
                "adapterID": "lldb-dap",
                "clientID": "moss-phase5-tests",
                "pathFormat": "path",
                "linesStartAt1": True,
                "columnsStartAt1": True,
            },
        )
        initialize_response = client.response(initialize)
        if not initialize_response.get("body", {}).get(
            "supportsConfigurationDoneRequest"
        ):
            fail("lldb-dap initialize omitted configurationDone support")
        launch = client.request(
            "launch",
            {
                "program": str(executable),
                "cwd": str(repo),
                "stopOnEntry": False,
                "initCommands": [
                    f'command script import "{bridge}"',
                    f'moss-map-load "{map_file}"',
                ],
                "preRunCommands": [
                    f'moss-break "{source}:{line}"' for line in breakpoint_lines
                ],
            },
        )
        client.event("initialized")
        configured = client.request("configurationDone", {})
        client.response(configured)
        client.response(launch)
        launch_event = client.event_one_of({"stopped", "exited", "terminated"})
        output = client.output_text()
        startup_expectations = [
            ("Moss LLDB commands installed", "Moss LLDB Python helper failed to load"),
            ("loaded Moss debug map:", "Moss debug map failed to load in LLDB"),
        ]
        startup_expectations.extend(
            (
                f"Moss breakpoint {source}:{line} ->",
                f"Moss breakpoint failed to resolve exactly: {source}:{line}",
            )
            for line in breakpoint_lines
        )
        for expected, diagnostic in startup_expectations:
            if expected not in output:
                fail(diagnostic)
        if output.count(" native location") < len(breakpoint_lines):
            fail("DAP startup did not resolve every Moss breakpoint natively")
        if launch_event.get("event") != "stopped":
            fail("DAP launch completed without stopping at a Moss breakpoint")
        stopped = launch_event

        thread_id, main_frame, _ = validate_stop(
            client, resolver, source, stopped, 38, "moss__main"
        )
        main_locals = frame_locals(client, main_frame)
        for name in ("meter", "enabled", "label", "values"):
            if name not in main_locals:
                fail(f"lldb-dap could not inspect Moss local {name!r}")
        if str(main_locals["enabled"].get("value", "")).lower() != "true":
            fail(f"unexpected boolean local: {main_locals['enabled']!r}")
        meter_text = variable_tree_text(client, main_locals["meter"])
        if "Meter" not in meter_text or "3" not in meter_text:
            fail(f"user struct local is not inspectable: {meter_text!r}")
        label_text = variable_tree_text(client, main_locals["label"])
        if "String" not in label_text or not (
            "ready" in label_text or ("len" in label_text and "5" in label_text)
        ):
            fail(f"string local is not structurally inspectable: {label_text!r}")
        values_text = variable_tree_text(client, main_locals["values"])
        if "Vec" not in values_text or not (
            "len" in values_text or "size" in values_text
        ):
            fail(f"collection local is not structurally inspectable: {values_text!r}")

        stopped = continue_to_stop(client, thread_id)
        thread_id, _, _ = validate_stop(
            client,
            resolver,
            source,
            stopped,
            5,
            "moss__method__Meter__doubled__",
        )

        # rustc may record several machine locations for the call-bearing
        # println! line.  Each reverse-maps exactly to the same Moss line.
        # Accept those honest repeated locations until execution reaches the
        # separately mapped step probe.
        for _ in range(8):
            stopped = continue_to_stop(client, thread_id)
            candidate_thread = int(stopped.get("body", {}).get("threadId", 0))
            if candidate_thread <= 0:
                fail("lldb-dap repeated stop omitted its thread identity")
            candidate_frames = stack_frames(client, candidate_thread)
            candidate_name = str(candidate_frames[0].get("name", ""))
            if "moss__function__step_probe__" in candidate_name:
                thread_id, _, _ = validate_stop(
                    client,
                    resolver,
                    source,
                    stopped,
                    25,
                    "moss__function__step_probe__",
                )
                break
            if candidate_name == "moss__main":
                thread_id, _, _ = validate_stop(
                    client, resolver, source, stopped, 38, "moss__main"
                )
                continue
            fail(f"unexpected stop before step probe: {candidate_name!r}")
        else:
            fail("repeated main-line locations prevented reaching the step probe")
        next_request = client.request("next", {"threadId": thread_id})
        client.response(next_request)
        stepped = client.event("stopped")
        thread_id, _, _ = validate_stop(
            client,
            resolver,
            source,
            stepped,
            26,
            "moss__function__step_probe__",
            reason="step",
        )

        stopped = continue_to_stop(client, thread_id)
        thread_id, normalize_frame, _ = validate_stop(
            client,
            resolver,
            source,
            stopped,
            8,
            "moss__function__normalize__",
        )
        normalize_locals = frame_locals(client, normalize_frame)
        value = normalize_locals.get("value")
        if value is None or str(value.get("value", "")) != "1":
            fail(f"lldb-dap could not inspect integer local value = 1: {value!r}")

        handler_frames = []
        for expected_value in (2, 3):
            stopped = continue_to_stop(client, thread_id)
            thread_id, repeated_frame, _ = validate_stop(
                client,
                resolver,
                source,
                stopped,
                8,
                "moss__function__normalize__",
            )
            repeated_locals = frame_locals(client, repeated_frame)
            repeated_value = repeated_locals.get("value")
            if repeated_value is None or str(repeated_value.get("value", "")) != str(
                expected_value
            ):
                fail("repeated static function breakpoint exposed the wrong local")

        stopped = continue_to_stop(client, thread_id)
        thread_id, _, handler_frames = validate_stop(
            client,
            resolver,
            source,
            stopped,
            21,
            "moss__handler__Counter__Add__",
        )
        if not any(
            not resolver.reverse_exact(
                str(frame.get("source", {}).get("path", "")),
                int(frame.get("line", 0)),
            )
            for frame in handler_frames[1:]
        ):
            fail("handler stack did not retain any generated/runtime helper frame")

        continue_request = client.request("continue", {"threadId": thread_id})
        client.response(continue_request)
        exited = client.event("exited")
        if int(exited.get("body", {}).get("exitCode", -1)) != 0:
            fail(f"debuggee did not terminate cleanly: {exited!r}")
        client.event("terminated")
        disconnect = client.request("disconnect", {"terminateDebuggee": False})
        client.response(disconnect)
        print(
            "lldb-dap Moss debugging passed: main locals, method, step-over, "
            "function, handler, stack correlation, and clean exit"
        )
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"test failure: {error}", file=sys.stderr)
        raise SystemExit(1)
