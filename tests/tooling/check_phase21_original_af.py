#!/usr/bin/env python3
"""Run the unchanged external review A–F sources against the current compiler."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
FIXTURES = ROOT / "tests/tooling/fixtures/p21_open_at_7775049"
SCRATCH = ROOT / "tmp/phase21-original-af"
SCRATCH.mkdir(parents=True, exist_ok=True)

# The originals are transcribed from p21_open_at_7775049.zip. Any ABI migration
# must use a separate file; changing these bytes invalidates this review gate.
ORIGINAL_SHA256 = {
    "README.md": "7800cce4680124f511f8460e639630c37a716a2ef0927592e2a5781e633c2c8e",
    "A_trailing_method_raise_lost.moss": "1d3bc3590f3234e7ef8e5d3d72ae8eb7a18f3163414cd1c00b5ed150be9c7586",
    "B_negated_float_echo.moss": "5b66d7de216cccb3ab036931a49f9f6ee090f0d9eb715753f39dfc3c68e58e01",
    "C_grouped_bare_reraise.moss": "31343f8a09160ce7ea5f4ad99d267cc60f3cdf2748bcb0b0148723a27ac01165",
    "D_chunk_placeholder_mapper_raise.moss": "e9ca2461592532ea35d578601addfa5325914d82d3367b978f3611fdc0264d0f",
    "E_chunk_reduce_seed_raise.moss": "5aa80caba3732d6b86e00db212c81d64ba8b43ab158fdaa92efba0bdc22c197b",
    "F_sqrt_float_local.moss": "089bec461549b45c0d192ba8a7627edd4159adb020fc75bd090db7798418cd41",
}

FILE_ERRORS = {
    f"enum:std__FileError:{variant}"
    for variant in ("NotFound", "PermissionDenied", "NotRegularFile", "InUse", "Full", "IO")
}


def run(cwd, *command):
    result = subprocess.run(
        [str(part) for part in command], cwd=cwd, capture_output=True,
        text=True, timeout=180,
    )
    return result


def expect_exit(result, expected, label):
    assert result.returncode == expected, (
        label, result.args, result.returncode, result.stdout, result.stderr)


def checked(source, cwd, warnings=()):
    result = run(cwd, COMPILER, "check", source, "--json")
    expect_exit(result, 0, f"check {source.name}")
    body = json.loads(result.stdout)
    assert body["ok"], (source.name, body)
    codes = {diagnostic["code"] for diagnostic in body["result"]["diagnostics"]}
    assert codes == set(warnings), (source.name, codes, warnings)


def effects(source, target, cwd, expected_raises):
    result = run(cwd, COMPILER, "effects", target, "--source", source, "--json")
    expect_exit(result, 0, f"effects {source.name} {target}")
    body = json.loads(result.stdout)
    assert body["ok"] and body["result"]["status"] == "resolved", (
        source.name, target, body)
    facts = body["result"]["observable_effects"]
    assert facts is not None, (source.name, target, body)
    assert set(facts["raise_set"]) == set(expected_raises), (
        source.name, target, facts)
    assert not facts["unresolved"], (source.name, target, facts)


def original_main_effects(source, cwd):
    """main's query is resolvable, though it has no retained local summary."""
    result = run(cwd, COMPILER, "effects", "main", "--source", source, "--json")
    expect_exit(result, 0, f"effects {source.name} main")
    body = json.loads(result.stdout)
    assert body["ok"] and body["result"]["target"]["kind"] == "main", body
    assert body["result"]["effect_summary"]["status"] == "not_available", body


def native(source, case, expected_output, data=None):
    for moss_optimization, rust_optimization in (("-O0", "0"), ("-Oshared-memory", "3")):
        cwd = SCRATCH / case / f"o{rust_optimization}"
        cwd.mkdir(parents=True, exist_ok=True)
        if data:
            name, contents = data
            (cwd / name).write_bytes(contents)
            assert (cwd / name).read_bytes() == contents
        lowered = cwd / "program.rs"
        executable = cwd / "program"
        generated = run(cwd, COMPILER, source, moss_optimization, "-o", lowered)
        expect_exit(generated, 0, f"generate {case} {moss_optimization}")
        built = run(cwd, "rustc", "--edition=2021", "-Dwarnings", "-C",
                    f"opt-level={rust_optimization}", lowered, "-o", executable)
        expect_exit(built, 0, f"rustc {case} o{rust_optimization}")
        executed = run(cwd, executable)
        expect_exit(executed, 0, f"native {case} o{rust_optimization}")
        assert executed.stdout == expected_output and executed.stderr == "", (
            case, moss_optimization, executed.stdout, executed.stderr)


def fast_debug(source, case, expected_output=None, fileio_unsupported=False):
    cwd = SCRATCH / case / "fast-debug"
    cwd.mkdir(parents=True, exist_ok=True)
    debug = run(cwd, COMPILER, "run", "--interp", source)
    if fileio_unsupported:
        expect_exit(debug, 1, f"Fast Debug FileIO {case}")
        assert debug.stdout == "", (case, debug.stdout)
        assert "Fast Debug FileIO execution is intentionally unsupported in Phase 20" in debug.stderr, (
            case, debug.stderr)
        assert "run this program with the native backend" in debug.stderr, (
            case, debug.stderr)
    else:
        expect_exit(debug, 0, f"Fast Debug {case}")
        assert debug.stdout == expected_output and debug.stderr == "", (
            case, debug.stdout, debug.stderr)


assert {path.name for path in FIXTURES.iterdir() if path.is_file()} == set(ORIGINAL_SHA256)
for filename, digest in ORIGINAL_SHA256.items():
    actual = hashlib.sha256((FIXTURES / filename).read_bytes()).hexdigest()
    assert actual == digest, (filename, actual, digest)

# A must fail at checking. Querying its effects requires a separately generated
# recovery-only companion because semantic queries validate the complete source.
a = FIXTURES / "A_trailing_method_raise_lost.moss"
a_cwd = SCRATCH / "A"
a_cwd.mkdir(parents=True, exist_ok=True)
a_check = run(a_cwd, COMPILER, "check", a, "--json")
expect_exit(a_check, 1, "original A must reject unhandled E.A")
a_error = json.loads(a_check.stdout)["error"]
assert a_error["code"] == "MOSS_COMPILE_ERROR" and (
    a_error["message"] == "main may not let typed errors escape; handle every raised variant with try/recover"
), a_error
prefix, separator, _ = a.read_text().partition("\nfn main():\n")
assert separator and "fn main():" not in prefix, "A companion split must be exact"
a_companion = a_cwd / "handled_for_effects.moss"
a_companion.write_text(prefix + "\nfn main():\n  try:\n    echo via_local()\n  recover E.A:\n    echo \"handled\"\n")
checked(a_companion, a_cwd)
effects(a_companion, "via_local", a_cwd, {"enum:E:A"})
effects(a_companion, "Box.poke", a_cwd, {"enum:E:A"})

b = FIXTURES / "B_negated_float_echo.moss"
b_cwd = SCRATCH / "B"
b_cwd.mkdir(parents=True, exist_ok=True)
checked(b, b_cwd)
original_main_effects(b, b_cwd)
b_output = "-2.0\n-1.0\n-2.0\n-2.0\n-1.0\n-0.0\n-1e16\n"
native(b, "B", b_output)
fast_debug(b, "B", b_output)

c = FIXTURES / "C_grouped_bare_reraise.moss"
c_cwd = SCRATCH / "C"
c_cwd.mkdir(parents=True, exist_ok=True)
checked(c, c_cwd, {"RECOVER_UNREACHABLE_VARIANT"})
effects(c, "grouped_reraise", c_cwd, {"enum:E:A"})
effects(c, "only_a", c_cwd, {"enum:E:A"})
native(c, "C", "outer A\n")
fast_debug(c, "C", "outer A\n")

for case, filename, handler, helper, failure, data in (
    ("D", "D_chunk_placeholder_mapper_raise.moss", "Worker.A", "checked",
     "enum:FormatError:Bad", ("p4.bin", b"ABABC")),
    ("E", "E_chunk_reduce_seed_raise.moss", "Worker.Init", "seed",
     "enum:SetupError:Bad", ("p6.bin", b"ABCD")),
):
    source = FIXTURES / filename
    cwd = SCRATCH / case
    cwd.mkdir(parents=True, exist_ok=True)
    checked(source, cwd)
    effects(source, handler, cwd, FILE_ERRORS | {failure})
    effects(source, helper, cwd, {failure})
    native(source, case, "-1\n", data)
    fast_debug(source, case, fileio_unsupported=True)

f = FIXTURES / "F_sqrt_float_local.moss"
f_cwd = SCRATCH / "F"
f_cwd.mkdir(parents=True, exist_ok=True)
checked(f, f_cwd)
effects(f, "root", f_cwd, set())
original_main_effects(f, f_cwd)
f_output = "4.0\n4.0\n1.4142135623730951\n"
native(f, "F", f_output)
fast_debug(f, "F", f_output)

print("Phase 21 unchanged original A–F review reproductions passed.")
