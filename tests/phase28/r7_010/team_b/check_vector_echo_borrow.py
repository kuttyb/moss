"""Ensure echo of an indexed Vector element borrows the element for display."""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / "tmp/team-b/vector-echo-borrow"
OUT.mkdir(parents=True, exist_ok=True)
MOSS = Path(sys.argv[1]).resolve()
SOURCE = ROOT / "tests/phase28/oracle/fixtures/oracle_42_custom_wrapper_remove.moss"
EXPECTED = "y\n2\nz"


def run(args):
    proc = subprocess.run(list(map(str, args)), cwd=ROOT, text=True,
                          capture_output=True, check=False, timeout=90)
    if proc.returncode:
        raise RuntimeError(f"{args!r} failed\n{proc.stdout}\n{proc.stderr}")
    return proc.stdout.strip()


assert run([MOSS, "check", "--json", SOURCE])
assert run([MOSS, "run", "--interp", SOURCE]) == EXPECTED
for mode, moss_flags, rust_flags in (
        ("debug", [], []), ("optimized", ["-O"], ["-O"])):
    rust = OUT / f"oracle42-{mode}.rs"
    binary = OUT / f"oracle42-{mode}"
    run([MOSS, *moss_flags, SOURCE, "-o", rust])
    generated = rust.read_text()
    match = re.search(
        r"// Moss line 13: echo b\.elements\[1\]\n([^\n]*)", generated)
    assert match and ".clone()" not in match.group(1), (
        "indexed Vector echo materialized an element clone", match.group(1) if match else None)
    run(["rustc", *rust_flags, rust, "-o", binary])
    assert run([binary]) == EXPECTED

lifetime_source = OUT / "read_then_write.moss"
lifetime_source.write_text(
    "fn main():\n"
    "  var values = [\"before\"]\n"
    "  echo values[0]\n"
    "  values[0] = \"after\"\n"
    "  echo values[0]\n")
assert run([MOSS, "check", "--json", lifetime_source])
assert run([MOSS, "run", "--interp", lifetime_source]) == "before\nafter"
for mode, moss_flags, rust_flags in (
        ("debug", [], []), ("optimized", ["-O"], ["-O"])):
    rust = OUT / f"read-then-write-{mode}.rs"
    binary = OUT / f"read-then-write-{mode}"
    run([MOSS, *moss_flags, lifetime_source, "-o", rust])
    generated = rust.read_text()
    for line in (3, 5):
        match = re.search(rf"// Moss line {line}: echo values\[0\]\n([^\n]*)", generated)
        assert match and ".clone()" not in match.group(1), (
            "indexed Vector echo cloned its source", line)
    run(["rustc", *rust_flags, rust, "-o", binary])
    assert run([binary]) == "before\nafter"

print("ORACLE-42 indexed Vector echo: borrowed element, no generated clone, parity PASS")
