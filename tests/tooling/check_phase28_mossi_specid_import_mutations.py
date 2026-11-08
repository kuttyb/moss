#!/usr/bin/env python3
"""Exercise malformed SpecId call rows through the real .mossi importer.

Each mutation starts from a sealed provider artifact, reseals only the test copy,
then compiles a source-free consumer. The consumer therefore reaches the same
Section 2 parser/remapper used by ordinary imports.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import hashlib
import re

from phase26_1_1_mossi_test_helpers import (
    refresh_v9_execution_section, section1_digest_material,
    section1_semantic_exports)


ROOT = Path(__file__).resolve().parents[2]
_interface_baselines: dict[Path, tuple[Path, bytes, str]] = {}


def run(args: list[str], *, cwd: Path, env: dict[str, str], expected: int):
    result = subprocess.run(args, cwd=cwd, env=env, text=True,
                            capture_output=True, timeout=120)
    if result.returncode != expected:
        raise AssertionError(
            f"{args} returned {result.returncode}, expected {expected}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}")
    return result


def reseal_mutation(interface: Path, mutate) -> None:
    text = interface.read_text(encoding="utf-8")
    text = mutate(text)
    rlib = interface.with_suffix(".rlib")
    if not rlib.exists():
        rlib = interface.with_name("lib" + interface.stem + ".rlib")
    if interface not in _interface_baselines:
        original_text = interface.read_text(encoding="utf-8")
        old_digest = re.search(
            r"(?m)^provider_contract_digest sha256:([0-9a-f]{64})$",
            original_text).group(1)
        _interface_baselines[interface] = (rlib, rlib.read_bytes(), old_digest)
    rlib, original_bytes, old_digest = _interface_baselines[interface]
    contract, _ = text.split("\ncontract\n", 1)[1].split(
        "\nsemantic_exports\n", 1)
    def fnv(section: str) -> str:
        value = 14695981039346656037
        for byte in section.encode("utf-8"):
            value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
        return f"{value:016x}"
    for key, section in (("concrete_interface_hash", contract + "\n"),
                         ("interface_hash", contract + "\n"),
                         ("generic_semantic_hash",
                          section1_semantic_exports(text))):
        text, count = re.subn(rf"(?m)^{key} [0-9a-f]{{16}}$",
                              f"{key} {fnv(section)}", text)
        if count != 1:
            raise AssertionError(f"expected one {key}, found {count}")
    digest = hashlib.sha256(section1_digest_material(text).encode()).hexdigest()
    text, count = re.subn(
        r"(?m)^provider_contract_digest sha256:[0-9a-f]{64}$",
        "provider_contract_digest sha256:" + digest, text)
    if count != 1:
        raise AssertionError("expected one provider contract digest")
    text = refresh_v9_execution_section(text)
    interface.write_text(text, encoding="utf-8")
    replacement = original_bytes.replace(old_digest.encode(), digest.encode())
    if replacement == original_bytes and digest != old_digest:
        raise AssertionError("paired test rlib has no old contract digest")
    rlib.write_bytes(replacement)


def edit_one_call(text: str, edit) -> str:
    lines = text.splitlines()
    changed = False
    for index, line in enumerate(lines):
        if not line.startswith("call_edge "):
            continue
        fields = shlex.split(line)
        argc = int(fields[9])
        tail = 10 + argc
        edit(fields, tail)
        lines[index] = "call_edge " + " ".join(
            json.dumps(value) if offset in (5, 6, len(fields) - 2)
            else value for offset, value in enumerate(fields[1:]))
        changed = True
        break
    if not changed:
        raise AssertionError("provider fixture emitted no call-edge row")
    return "\n".join(lines) + "\n"


def edit_call_row_count(text: str, *, duplicate: bool) -> str:
    lines = text.splitlines()
    count_index = next(index for index, line in enumerate(lines)
                       if line.startswith("call_edge_count "))
    count = int(lines[count_index].split()[1])
    row_index = next(index for index, line in enumerate(lines)
                     if line.startswith("call_edge "))
    if duplicate:
        lines[count_index] = f"call_edge_count {count + 1}"
        lines.insert(row_index + 1, lines[row_index])
    else:
        lines[count_index] = f"call_edge_count {count - 1}"
        del lines[row_index]
    return "\n".join(lines) + "\n"


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_phase28_mossi_specid_import_mutations.py COMPILER",
              file=sys.stderr)
        return 2
    compiler = str(Path(sys.argv[1]).resolve())
    margo = str(ROOT / "margo")
    scratch = ROOT / "tmp" / f"phase28-mossi-specid-mutations-{os.getpid()}"
    base = scratch / "base"
    provider = scratch / "provider"
    consumer = scratch / "consumer"
    for package in (base, provider, consumer):
        (package / "src").mkdir(parents=True, exist_ok=True)
        (package / "moss.toml").write_text(
            '[project]\nname = "phase28_mossi_mutation"\nversion = "0.1.0"\n',
            encoding="utf-8")
    (base / "src" / "base.moss").write_text(
        "module Base\nexport fn normalize(x):\n  return x\n", encoding="utf-8")
    (provider / "src" / "provider.moss").write_text(
        "module Provider\nimport Base\n"
        "export fn forward(x):\n  return Base.normalize(x)\n",
        encoding="utf-8")
    (consumer / "src" / "main.moss").write_text(
        "module App\nimport Provider\nfn main():\n  echo Provider.forward(10)\n",
        encoding="utf-8")
    base_env = {**os.environ, "MOSS": compiler, "MOSS_STAGED_ABI": "v10"}
    try:
        run([margo, "build", "--json"], cwd=base, env=base_env, expected=0)
        base_artifacts = base / "build" / "debug"
        provider_env = {**base_env, "MOSS_MODULE_PATH": str(base_artifacts)}
        run([margo, "build", "--json"], cwd=provider, env=provider_env,
            expected=0)
        provider_artifacts = provider / "build" / "debug"
        interface = provider_artifacts / "Provider.mossi"
        if not interface.exists():
            raise AssertionError(f"provider artifact missing: {interface}")
        # Retain only the serialized interface and paired artifact while the
        # consumer imports; this forces source-free Section 2 loading.
        shutil.rmtree(provider / "src")
        imported_env = {**base_env, "MOSS_MODULE_PATH": os.pathsep.join(
            (str(provider_artifacts), str(base_artifacts)))}
        originals = interface.read_text(encoding="utf-8")
        mutation_cases = (
            ("duplicate call row", lambda text: edit_call_row_count(
                text, duplicate=True),
             "invalid, duplicate, or unknown-ID call edge"),
            ("stale locator", lambda text: edit_one_call(
                text, lambda fields, tail: fields.__setitem__(tail, "999999")),
             "stale caller or statement locator"),
            ("wrong EST kind", lambda text: edit_one_call(
                text, lambda fields, tail: fields.__setitem__(
                    tail + 4 + int(fields[tail + 3]) + 0, "999")),
             "imported EST call kind or callee"),
            ("wrong callee", lambda text: edit_one_call(
                text, lambda fields, tail: fields.__setitem__(
                    tail + 5 + int(fields[tail + 3]) + 0, "forged_callee")),
             "imported EST call kind or callee"),
            ("unknown callee SpecId", lambda text: edit_one_call(
                text, lambda fields, tail: fields.__setitem__(4, "999999")),
             "invalid, duplicate, or unknown-ID call edge"),
        )
        for name, mutation, diagnostic in mutation_cases:
            interface.write_text(originals, encoding="utf-8")
            reseal_mutation(interface, mutation)
            result = run([margo, "build", "--json"], cwd=consumer,
                         env=imported_env, expected=1)
            combined = result.stdout + result.stderr
            if diagnostic not in combined:
                raise AssertionError(
                    f"{name} failed for an unintended reason:\n{combined}")
            print(f"PASS importer mutation: {name}")

        interface.write_text(originals, encoding="utf-8")
        reseal_mutation(interface, lambda text: edit_call_row_count(
            text, duplicate=False))
        result = run([margo, "build", "--json"], cwd=consumer,
                     env=imported_env, expected=1)
        if "MOSS_INTERNAL_INVARIANT" not in result.stdout + result.stderr:
            raise AssertionError(
                "missing imported call edge did not fail closed:\n" +
                result.stdout + result.stderr)
        print("PASS importer mutation: missing call row fails closed")

        # A qualified foreign reference must resolve by the original provider
        # pair. Keep the table slot/module fixed, perturb only provider ID.
        interface.write_text(originals, encoding="utf-8")
        rows = [line.split() for line in originals.splitlines()
                if line.startswith("spec_identity ")]
        foreign = next((row for row in rows
                        if json.loads(row[2]) == "Base"), None)
        if foreign is None:
            raise AssertionError("fixture has no serialized foreign Base row")
        foreign_slot = int(foreign[1])
        foreign_provider_id = int(foreign[4])
        def unknown_provider_pair(text: str) -> str:
            old = f"spec_identity {foreign_slot} "
            lines = text.splitlines()
            for index, line in enumerate(lines):
                if line.startswith(old):
                    fields = shlex.split(line)
                    fields[4] = str(foreign_provider_id + 1000000)
                    lines[index] = "spec_identity " + " ".join(
                        json.dumps(value) if offset in (1, 2, 4, 5)
                        else value for offset, value in enumerate(fields[1:]))
                    return "\n".join(lines) + "\n"
            raise AssertionError("foreign Base identity row disappeared")
        reseal_mutation(interface, unknown_provider_pair)
        result = run([margo, "build", "--json"], cwd=consumer,
                     env=imported_env, expected=1)
        if "cannot remap imported SpecId table" not in result.stdout + result.stderr:
            raise AssertionError("unknown qualified provider pair did not fail in remapper")
        print("PASS importer mutation: unknown qualified provider SpecId")
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
