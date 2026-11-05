#!/usr/bin/env python3
"""Check fact-directed borrowing for an indexed Option[String] match."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
FIXTURE = ROOT / "tests/phase28/oracle/fixtures/oracle_19_take_nested_vector_option.moss"
OUT = ROOT / "tmp/team-b/match-projection-read-borrow"
OUT.mkdir(parents=True, exist_ok=True)
MOSS = Path(sys.argv[1]).resolve()
EXPECTED = "task\n1\ncleared"


def run(args, env=None, expect=0):
    proc = subprocess.run(list(map(str, args)), cwd=ROOT, env=env, text=True,
                          capture_output=True, check=False, timeout=90)
    if proc.returncode != expect:
        raise RuntimeError(f"{args!r} failed ({proc.returncode})\n"
                           f"{proc.stdout}\n{proc.stderr}")
    return proc.stdout.strip(), proc.stderr


assert run([MOSS, "check", "--json", FIXTURE])[0]
assert run([MOSS, "run", "--interp", FIXTURE])[0] == EXPECTED
for mode, moss_flags, rust_flags in (
        ("debug", [], []), ("optimized", ["-O"], ["-O"])):
    rust = OUT / f"oracle19-{mode}.rs"
    binary = OUT / f"oracle19-{mode}"
    run([MOSS, *moss_flags, FIXTURE, "-o", rust])
    generated = rust.read_text()
    match_lines = [line for line in generated.splitlines()
                   if "match &" in line and ".pending" in line]
    assert len(match_lines) == 1, match_lines
    assert "v)[(0_i64) as usize]).pending" in match_lines[0], match_lines[0]
    assert ".clone()" not in match_lines[0], match_lines[0]
    run(["rustc", *rust_flags, rust, "-o", binary])
    assert run([binary])[0] == EXPECTED

for hook in ("MOSS_TEST_CONSUMER_DROP_MATCH_FACT",
             "MOSS_TEST_CONSUMER_DROP_MATCH_REQUIREMENT",
             "MOSS_TEST_CONSUMER_PERTURB_MATCH_ARM_ACCESS",
             "MOSS_TEST_DROP_MATCH_FACT",
             "MOSS_TEST_DUPLICATE_MATCH_FACT",
             "MOSS_TEST_DUPLICATE_MATCH_REQUIREMENT"):
    env = dict(os.environ)
    env[hook] = "1"
    for command in ([MOSS, "run", "--interp", FIXTURE],
                    [MOSS, FIXTURE, "-o", OUT / f"corrupt-{hook}.rs"]):
        stdout, stderr = run(command, env=env, expect=1)
        assert "MOSS_INTERNAL_INVARIANT" in stdout + stderr, (hook, stdout, stderr)

print("ORACLE-19 checked match READ borrow: checker, Fast Debug, native Debug/Optimized PASS")
