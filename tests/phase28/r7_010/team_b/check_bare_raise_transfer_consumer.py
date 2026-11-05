"""Exercise exact bare-raise transfer authority in all three consumers."""
from pathlib import Path
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
SOURCE = ROOT / "tests/tooling/fixtures/phase21_recovery_native.moss"
BOUND_SOURCE = ROOT / "tests/phase28/r7_010/team_b/fixtures/bare_raise_recovery_binding.moss"
OUT = ROOT / "tmp/team-b/bare-raise-transfer-consumer"
OUT.mkdir(parents=True, exist_ok=True)
MOSS = Path(sys.argv[1]).resolve()


def run(args, env=None, expect=0):
    proc = subprocess.run(list(map(str, args)), cwd=ROOT, text=True,
                          capture_output=True, check=False, timeout=90,
                          env=env)
    if proc.returncode != expect:
        raise RuntimeError(f"{args!r} returned {proc.returncode}\n"
                           f"stdout:\n{proc.stdout}\nstderr:\n{proc.stderr}")
    return proc.stdout.strip(), proc.stderr


baseline_fast = run([MOSS, "run", "--interp", SOURCE])[0]
assert baseline_fast.endswith("reraised"), baseline_fast
for mode, moss_flags, rust_flags in (
        ("debug", [], []), ("optimized", ["-O"], ["-O"])):
    rust = OUT / f"bare-reraise-{mode}.rs"
    binary = OUT / f"bare-reraise-{mode}"
    run([MOSS, *moss_flags, SOURCE, "-o", rust])
    generated = rust.read_text()
    assert "__moss_selected_value.clone()" not in generated
    assert re.search(
        r"return Err\(__MossRaised::Raised_[0-9a-f]+\(__moss_selected_value\)\);",
        generated)
    run(["rustc", *rust_flags, rust, "-o", binary])
    assert run([binary])[0] == baseline_fast

bound_fast = run([MOSS, "run", "--interp", BOUND_SOURCE])[0]
assert bound_fast == "handled", bound_fast
for mode, moss_flags, rust_flags in (
        ("debug", [], []), ("optimized", ["-O"], ["-O"])):
    rust = OUT / f"bound-reraise-{mode}.rs"
    binary = OUT / f"bound-reraise-{mode}"
    run([MOSS, *moss_flags, BOUND_SOURCE, "-o", rust])
    generated = rust.read_text()
    assert "__moss_selected_value.clone()" not in generated
    assert re.search(
        r"__MossRaised::Raised_[0-9a-f]+\(selected\)", generated)
    run(["rustc", *rust_flags, rust, "-o", binary])
    assert run([binary])[0] == bound_fast

corruptions = (
    "MOSS_TEST_CONSUMER_DROP_BARE_RAISE_TRANSFER_FACT",
    "MOSS_TEST_CONSUMER_PERTURB_BARE_RAISE_TRANSFER_FACT",
    "MOSS_TEST_CONSUMER_DUPLICATE_BARE_RAISE_TRANSFER_FACT",
    "MOSS_TEST_CONSUMER_WRONG_BARE_RAISE_TRANSFER_EDGE",
    "MOSS_TEST_CONSUMER_DROP_BARE_RAISE_TRANSFER_REQUIREMENT",
    "MOSS_TEST_CONSUMER_DUPLICATE_BARE_RAISE_TRANSFER_REQUIREMENT",
)
for hook in corruptions:
    env = os.environ.copy()
    env[hook] = "1"
    for mode, moss_flags in (("Native Debug", []),
                             ("Native Optimized", ["-O"])):
        proc = subprocess.run(
            [str(MOSS), *moss_flags, str(SOURCE), "-o", str(OUT / "corrupt.rs")],
            cwd=ROOT, text=True, capture_output=True, check=False, timeout=90,
            env=env)
        assert proc.returncode != 0 and "MOSS_INTERNAL_INVARIANT" in proc.stderr, (
            hook, mode, proc.returncode, proc.stdout, proc.stderr)
    proc = subprocess.run(
        [str(MOSS), "run", "--interp", str(SOURCE)], cwd=ROOT,
        text=True, capture_output=True, check=False, timeout=90, env=env)
    assert proc.returncode != 0 and "MOSS_INTERNAL_INVARIANT" in proc.stderr, (
        hook, "Fast Debug", proc.returncode, proc.stdout, proc.stderr)
    print(f"PASS {hook}: Native Debug/Optimized and Fast Debug fail closed")

print("PASS bare re-raise carrier transfer: Native Debug/Optimized/Fast Debug parity; selected payload moves without clone")
