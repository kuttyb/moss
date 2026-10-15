#!/usr/bin/env python3
"""Compile the bounded Team G G0 Rust identity/key-adapter prototype."""

from pathlib import Path
import shutil
import subprocess


HERE = Path(__file__).resolve().parent
RUST = HERE / "rust"
ROOT = HERE.parents[2]
OUT = ROOT / "tmp" / "phase25-g0-rust-feasibility"


def run(args, *, expected=0):
    result = subprocess.run(
        [str(arg) for arg in args], cwd=HERE, capture_output=True, text=True
    )
    if result.returncode != expected:
        raise AssertionError(
            f"{args} returned {result.returncode}, expected {expected}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


shutil.rmtree(OUT, ignore_errors=True)
OUT.mkdir(parents=True)

provider = OUT / "libphase25_provider.rlib"
consumer_a = OUT / "libphase25_consumer_a.rlib"
consumer_b = OUT / "libphase25_consumer_b.rlib"
executable = OUT / "phase25_g0"

run(["rustc", "--edition=2021", "--crate-type=lib", RUST / "provider.rs", "-o", provider])
run([
    "rustc", "--edition=2021", "--crate-type=lib", RUST / "consumer_a.rs",
    "--extern", f"phase25_provider={provider}", "-L", f"dependency={OUT}",
    "-o", consumer_a,
])
run([
    "rustc", "--edition=2021", "--crate-type=lib", RUST / "consumer_b.rs",
    "--extern", f"phase25_provider={provider}", "-L", f"dependency={OUT}",
    "-o", consumer_b,
])
run([
    "rustc", "--edition=2021", RUST / "main.rs",
    "--extern", f"phase25_provider={provider}",
    "--extern", f"phase25_consumer_a={consumer_a}",
    "--extern", f"phase25_consumer_b={consumer_b}",
    "-L", f"dependency={OUT}", "-o", executable,
])
output = run([executable]).stdout
assert output == "84 moss-g0 41\n", output

orphan = run([
    "rustc", "--edition=2021", RUST / "orphan_negative.rs",
    "--extern", f"phase25_provider={provider}", "-L", f"dependency={OUT}",
    "-o", OUT / "orphan_negative",
], expected=1)
assert "E0117" in orphan.stderr, orphan.stderr

private = run([
    "rustc", "--edition=2021", RUST / "private_field_negative.rs",
    "--extern", f"phase25_provider={provider}", "-L", f"dependency={OUT}",
    "-o", OUT / "private_field_negative",
], expected=1)
assert "E0616" in private.stderr, private.stderr

print("Phase 25 G0 provider identity, accessor visibility, and Map adapter prototype passed")
