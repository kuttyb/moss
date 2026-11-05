#!/usr/bin/env python3
"""Verify exact checker-owned whole-local transfer reaches native Rust."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
FIXTURE = ROOT / "tests/phase28/r7_010/team_b/local_transfer_consumer.moss"
OUT = ROOT / "tmp/team-b/local-transfer-consumer"
OUT.mkdir(parents=True, exist_ok=True)
MOSS = Path(sys.argv[1]).resolve()


def run(args, env=None, expect=0):
    proc = subprocess.run(list(map(str, args)), cwd=ROOT, env=env, text=True,
                          capture_output=True, check=False, timeout=90)
    if proc.returncode != expect:
        raise RuntimeError(f"{args!r} returned {proc.returncode}\n"
                           f"{proc.stdout}\n{proc.stderr}")
    return proc.stdout.strip(), proc.stderr


assert run([MOSS, "check", "--json", FIXTURE])[0]

for mode, moss_flags, rust_flags in (
        ("debug", [], []), ("optimized", ["-O"], ["-O"])):
    rust = OUT / f"local-transfer-{mode}.rs"
    binary = OUT / f"local-transfer-{mode}"
    env = dict(os.environ)
    env["MOSS_TEST_CONSUMER_REQUIRE_LOCAL_TRANSFER_FACT"] = "1"
    run([MOSS, *moss_flags, FIXTURE, "-o", rust], env=env)
    generated = rust.read_text()
    assert "target = source;" in generated, generated
    assert "target = &(source);" not in generated, generated
    run(["rustc", *rust_flags, rust, "-o", binary])
    assert run([binary])[0] == "1"

corruptions = (
    {"MOSS_TEST_CONSUMER_DROP_LOCAL_TRANSFER_FACT": "1"},
    {"MOSS_TEST_DROP_LOCAL_TRANSFER_FACT": "1"},
    {"MOSS_TEST_CONSUMER_PERTURB_LOCAL_TRANSFER_FACT": "1"},
    {"MOSS_TEST_PERTURB_LOCAL_TRANSFER_FACT": "liveness"},
    {"MOSS_TEST_CONSUMER_DUPLICATE_LOCAL_TRANSFER_FACT": "1"},
    {"MOSS_TEST_CONSUMER_DROP_LOCAL_TRANSFER_REQUIREMENT": "1"},
    {"MOSS_TEST_CONSUMER_DUPLICATE_LOCAL_TRANSFER_REQUIREMENT": "1"},
)
for corruption in corruptions:
    env = dict(os.environ)
    env["MOSS_TEST_CONSUMER_REQUIRE_LOCAL_TRANSFER_FACT"] = "1"
    env.update(corruption)
    label = next(iter(corruption))
    rust = OUT / f"corrupt-{label}.rs"
    rust.unlink(missing_ok=True)
    Path(str(rust) + ".mossclone.json").unlink(missing_ok=True)
    stdout, stderr = run([MOSS, FIXTURE, "-o", rust], env=env, expect=1)
    assert "MOSS_INTERNAL_INVARIANT" in stdout + stderr, (corruption, stdout, stderr)
    assert not rust.exists(), f"{corruption} wrote a Rust artifact"
    assert not Path(str(rust) + ".mossclone.json").exists(), \
        f"{corruption} wrote a provenance artifact"

print("Exact local transfer: native Debug/Optimized and seven corruption cases PASS")
