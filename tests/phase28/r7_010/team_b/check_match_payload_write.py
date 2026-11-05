#!/usr/bin/env python3
"""Verify checked match payload WRITE lowering in Fast Debug and native Rust."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
FIXTURE = ROOT / "tests/phase28/oracle/fixtures/oracle_32_match_payload_write_borrow.moss"
SCALAR_FIXTURE = ROOT / "tests/phase28/r7_010/team_b/match_payload_primitive_value.moss"
OUT = ROOT / "tmp/team-b/match-payload-write"
OUT.mkdir(parents=True, exist_ok=True)
MOSS = Path(sys.argv[1]).resolve()


def run(args, env=None, expect=0):
    proc = subprocess.run(list(map(str, args)), cwd=ROOT, env=env, text=True,
                          capture_output=True, check=False, timeout=90)
    if proc.returncode != expect:
        raise RuntimeError(f"{args!r} failed\n{proc.stdout}\n{proc.stderr}")
    return proc.stdout.strip(), proc.stderr


assert run([MOSS, "check", "--json", FIXTURE])[0]
assert run([MOSS, "run", "--interp", FIXTURE])[0] == "20"
assert run([MOSS, "check", "--json", SCALAR_FIXTURE])[0]
assert run([MOSS, "run", "--interp", SCALAR_FIXTURE])[0] == "4"
for mode, moss_flags, rust_flags in (
        ("debug", [], []), ("optimized", ["-O"], ["-O"])):
    rust = OUT / f"oracle32-{mode}.rs"
    binary = OUT / f"oracle32-{mode}"
    run([MOSS, *moss_flags, FIXTURE, "-o", rust])
    generated = rust.read_text()
    assert "match &mut s" in generated, "checked Write match did not borrow mutably"
    assert "buf.size = 20" in generated
    run(["rustc", *rust_flags, rust, "-o", binary])
    assert run([binary])[0] == "20"

for mode, moss_flags, rust_flags in (
        ("debug", [], []), ("optimized", ["-O"], ["-O"])):
    rust = OUT / f"match-primitive-{mode}.rs"
    binary = OUT / f"match-primitive-{mode}"
    run([MOSS, *moss_flags, SCALAR_FIXTURE, "-o", rust])
    generated = rust.read_text()
    assert "match &mut result" in generated, "checked WRITE payload lost its mutable borrow"
    assert "wrapping_sub((*start))" in generated, "primitive payload value read was not dereferenced"
    assert "(*start) = 4_i64;" in generated, "primitive payload assignment did not write through"
    run(["rustc", *rust_flags, rust, "-o", binary])
    assert run([binary])[0] == "4"

for hook in ("MOSS_TEST_CONSUMER_DROP_MATCH_FACT",
             "MOSS_TEST_CONSUMER_PERTURB_MATCH_ARM_ACCESS"):
    env = dict(os.environ)
    env[hook] = "1"
    for command in ([MOSS, "run", "--interp", FIXTURE],
                    [MOSS, FIXTURE, "-o", OUT / f"corrupt-{hook}.rs"],
                    [MOSS, "run", "--interp", SCALAR_FIXTURE],
                    [MOSS, SCALAR_FIXTURE, "-o", OUT / f"corrupt-scalar-{hook}.rs"]):
        stdout, stderr = run(command, env=env, expect=1)
        assert "MOSS_INTERNAL_INVARIANT" in stdout + stderr, (hook, stdout, stderr)

print("ORACLE-32 checked payload WRITE: checker, Fast Debug, native Debug/Optimized PASS")
