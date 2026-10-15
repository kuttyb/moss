#!/usr/bin/env python3
"""Compile the bounded Team G G0 Rust identity, public backend-field, and key-adapter prototype."""

from pathlib import Path
import shutil
import subprocess


HERE = Path(__file__).resolve().parent
RUST = HERE / "rust"
ACCESSOR = RUST / "accessor_alternative"
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

runtime = OUT / "libphase25_moss_map_runtime.rlib"
provider = OUT / "libphase25_provider.rlib"
consumer_a = OUT / "libphase25_consumer_a.rlib"
consumer_b = OUT / "libphase25_consumer_b.rlib"
executable = OUT / "phase25_g0"

# 1. Primary selected A1 strategy: provider-owned generic record with mechanical
# public backend fields, consumer-generated free functions, and policy-free MossMap.
run(["rustc", "--edition=2021", "--crate-type=lib", RUST / "moss_map_runtime.rs", "-o", runtime])
run([
    "rustc", "--edition=2021", "--crate-type=lib", RUST / "provider.rs",
    "--extern", f"phase25_moss_map_runtime={runtime}",
    "-L", f"dependency={OUT}", "-o", provider,
])
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
    "--extern", f"phase25_moss_map_runtime={runtime}",
    "--extern", f"phase25_consumer_a={consumer_a}",
    "--extern", f"phase25_consumer_b={consumer_b}",
    "-L", f"dependency={OUT}", "-o", executable,
])
output = run([executable]).stdout
assert output == "84 moss-g0 map=10,10,11,15,80,1,true\n", output

# Structural guards for primary strategy:
# - mechanical public backend fields
# - consumer-generated free functions accessing backend fields directly
# - no foreign trait impl or dynamic dispatch
positive_sources = "\n".join(
    (RUST / name).read_text()
    for name in ("moss_map_runtime.rs", "provider.rs", "consumer_a.rs", "consumer_b.rs", "main.rs")
)
assert "dyn " not in positive_sources
assert "impl Hash for provider::UserKey" not in positive_sources
assert "MossKeyOps" not in positive_sources
assert "MossMap<K, V, Ops>" not in positive_sources
assert "pub struct MossMap<K, V>" in positive_sources
assert "pub field_0: T" in (RUST / "provider.rs").read_text()
assert "record.field_0 += 1" in (RUST / "consumer_a.rs").read_text()

# Negative control: orphan rule rejects foreign trait impl in consumer
orphan = run([
    "rustc", "--edition=2021", RUST / "orphan_negative.rs",
    "--extern", f"phase25_provider={provider}", "-L", f"dependency={OUT}",
    "-o", OUT / "orphan_negative",
], expected=1)
assert "E0117" in orphan.stderr, orphan.stderr

# 2. Preserved evaluated alternative: accessor / private-field experiment
prov_acc = OUT / "libphase25_provider_accessor.rlib"
cons_acc = OUT / "libphase25_consumer_accessor.rlib"
run([
    "rustc", "--edition=2021", "--crate-type=lib",
    ACCESSOR / "provider_accessor.rs", "-o", prov_acc,
])
run([
    "rustc", "--edition=2021", "--crate-type=lib",
    ACCESSOR / "consumer_accessor.rs",
    "--extern", f"phase25_provider_accessor={prov_acc}",
    "-L", f"dependency={OUT}", "-o", cons_acc,
])
private = run([
    "rustc", "--edition=2021", ACCESSOR / "private_field_negative.rs",
    "--extern", f"phase25_provider_accessor={prov_acc}",
    "-L", f"dependency={OUT}", "-o", OUT / "private_field_negative",
], expected=1)
assert "E0616" in private.stderr, private.stderr

print("Phase 25 G0 provider identity, public backend fields, canonical cross-crate MossMap, and preserved accessor alternative passed")
