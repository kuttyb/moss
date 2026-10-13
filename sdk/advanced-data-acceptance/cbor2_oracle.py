#!/usr/bin/env python3
"""Run the optional two-way live oracle against pinned cbor2 5.7.1."""

import importlib.metadata
import os
from pathlib import Path
import subprocess
import tempfile

import cbor2

from validate_cbor_fixtures import load_and_validate


PROJECT = Path(__file__).resolve().parent
REPO = PROJECT.parents[1]
MARGO = REPO / "margo"
CBOR_PACKAGE = REPO / "sdk" / "cbor"
SCRATCH = REPO / "tmp"


def fixture_value(entry: dict):
    if entry["kind"] == "bytes":
        return bytes.fromhex(entry["value_hex"])
    return entry["value"]


def moss_bytes(values: bytes) -> str:
    return "[" + ", ".join(str(value) for value in values) + "]"


def emit_node(value, doc: str, prefix: str, lines: list[str], counter: list[int]) -> str:
    node = f"{prefix}_{counter[0]}"
    counter[0] += 1
    if value is None:
        lines.append(f"    {node} = cbor.add_null({doc})")
    elif isinstance(value, bool):
        literal = "true" if value else "false"
        lines.append(f"    {node} = cbor.add_bool({doc}, {literal})")
    elif isinstance(value, int):
        lines.append(f"    {node} = cbor.add_int({doc}, {value})")
    elif isinstance(value, bytes):
        lines.append(f"    {node} = cbor.add_byte_string({doc}, {moss_bytes(value)})")
    elif isinstance(value, str):
        lines.append(
            f"    {node} = cbor.add_text_bytes({doc}, {moss_bytes(value.encode('utf-8'))})"
        )
    elif isinstance(value, list):
        lines.append(f"    {node} = cbor.add_array({doc})")
        for child_value in value:
            child = emit_node(child_value, doc, prefix, lines, counter)
            lines.append(f"    cbor.array_push({doc}, {node}, {child})")
    elif isinstance(value, dict):
        lines.append(f"    {node} = cbor.add_map({doc})")
        for key_value, mapped_value in value.items():
            key = emit_node(key_value, doc, prefix, lines, counter)
            mapped = emit_node(mapped_value, doc, prefix, lines, counter)
            lines.append(f"    cbor.map_set({doc}, {node}, {key}, {mapped})")
    else:
        raise AssertionError(f"unsupported fixture value {value!r}")
    return node


def generated_source(values: list, encoded: list[bytes]) -> str:
    lines = [
        "module cbor2_live_oracle",
        "",
        "import binary",
        "import cbor",
        "",
        "fn emit_bytes(values: Vector[Int]):",
        "  var i = 0",
        "  while i < (values |> count):",
        "    echo values[i]",
        "    i = i + 1",
        "  echo -1",
        "",
        "fn main():",
        "  try:",
    ]
    for index, wire in enumerate(encoded):
        lines.append(f"    decoded_{index} = cbor.decode({moss_bytes(wire)})")
        lines.append(f"    decoded_wire_{index} = cbor.encode(decoded_{index})")
        lines.append(f"    emit_bytes(decoded_wire_{index})")
    for index, value in enumerate(values):
        doc = f"produced_{index}"
        lines.append(f"    var {doc} = cbor.new_doc()")
        root = emit_node(value, doc, f"node_{index}", lines, [0])
        lines.append(f"    cbor.set_root({doc}, {root})")
        lines.append(f"    produced_wire_{index} = cbor.encode({doc})")
        lines.append(f"    emit_bytes(produced_wire_{index})")
    lines.extend(["    echo \"cbor2 live oracle complete\"", "  recover:", "    assert(false)", ""])
    return "\n".join(lines)


def main() -> None:
    version = importlib.metadata.version("cbor2")
    assert version == "5.7.1", f"cbor2 5.7.1 required, found {version}"
    contract = load_and_validate()
    entries = contract["vectors"]
    values = [fixture_value(entry) for entry in entries]
    encoded = [cbor2.dumps(value) for value in values]
    for entry, wire, value in zip(entries, encoded, values):
        assert wire.hex() == entry["hex"], entry["id"]
        assert cbor2.loads(bytes.fromhex(entry["hex"])) == value, entry["id"]

    SCRATCH.mkdir(exist_ok=True)
    env = os.environ.copy()
    env.update(
        MOSS=str(REPO / "moss"),
        MARGO_HOME=str(SCRATCH / "margo-advanced-data-cbor2"),
        TMPDIR=str(SCRATCH),
    )
    with tempfile.TemporaryDirectory(prefix="advanced-data-cbor2-", dir=SCRATCH) as temporary:
        oracle = Path(temporary)
        (oracle / "src").mkdir()
        (oracle / "Moss.toml").write_text(
            '[package]\nname = "advanced-data-cbor2-oracle"\nversion = "0.1.0"\n\n'
            '[build]\nsource = "src"\n\n'
            f'[dependencies]\n"moss.binary" = {{ path = "{REPO / "sdk" / "binary"}" }}\n'
            f'"moss.cbor" = {{ path = "{CBOR_PACKAGE}" }}\n',
            encoding="utf-8",
        )
        (oracle / "src" / "main.moss").write_text(
            generated_source(values, encoded), encoding="utf-8"
        )
        result = subprocess.run(
            [str(MARGO), "run"],
            cwd=oracle,
            env=env,
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0, (
            f"generated Moss oracle failed\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )

    lines = result.stdout.splitlines()
    assert lines[-1] == "cbor2 live oracle complete", result.stdout
    moss_vectors = []
    current = []
    for line in lines[:-1]:
        value = int(line)
        if value == -1:
            moss_vectors.append(bytes(current))
            current = []
        else:
            current.append(value)
    assert not current
    count = len(entries)
    assert len(moss_vectors) == count * 2
    decoded_reencoded = moss_vectors[:count]
    moss_produced = moss_vectors[count:]
    for entry, value, cbor2_wire, moss_wire in zip(entries, values, encoded, decoded_reencoded):
        assert moss_wire == cbor2_wire, f"Moss decode/re-encode mismatch: {entry['id']}"
    for entry, value, moss_wire in zip(entries, values, moss_produced):
        assert moss_wire.hex() == entry["hex"], f"Moss encode mismatch: {entry['id']}"
        assert cbor2.loads(moss_wire) == value, f"cbor2 decode mismatch: {entry['id']}"
    print(
        f"cbor2 {version} live oracle passed: {count} cbor2->Moss and "
        f"{count} Moss->cbor2 comparisons"
    )


if __name__ == "__main__":
    main()
