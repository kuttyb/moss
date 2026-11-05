"""Ensure copy(v[index]) borrows its source and clones only in its fact helper."""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / "tmp/team-b/explicit-copy-source-borrow"
OUT.mkdir(parents=True, exist_ok=True)
MOSS = Path(sys.argv[1]).resolve()
SOURCE = ROOT / "tests/phase28/oracle/fixtures/oracle_10a_explicit_copy_only.moss"


def run(args):
    proc = subprocess.run(list(map(str, args)), cwd=ROOT, text=True,
                          capture_output=True, check=False, timeout=90)
    if proc.returncode:
        raise RuntimeError(f"{args!r} failed\n{proc.stdout}\n{proc.stderr}")
    return proc.stdout.strip()


assert run([MOSS, "check", "--json", SOURCE])
assert run([MOSS, "run", "--interp", SOURCE]) == "copy\noriginal"
for mode, moss_flags, rust_flags in (
        ("debug", [], []), ("optimized", ["-O"], ["-O"])):
    rust = OUT / f"oracle10a-{mode}.rs"
    binary = OUT / f"oracle10a-{mode}"
    run([MOSS, *moss_flags, SOURCE, "-o", rust])
    generated = rust.read_text()
    matches = list(re.finditer(
        r"fn (__moss_copy_value_([0-9a-f]{16}))\(value: &Record\) -> Record \{([^}]*)\} "
        r"/\* Moss copy fact call: typed-copy-fact-v1:([0-9a-f]{64}) \*/\s*"
        r"__moss_copy_value_[0-9a-f]{16}\(&\(\(v\)\[\(0_i64\) as usize\]\)\)",
        generated))
    assert len(matches) == 1, "copy(v[0]) lacks one direct fact-specific source-borrow helper"
    helper_body = matches[0].group(3)
    assert helper_body.count(".clone()") == 1, "explicit copy helper must materialize exactly once"
    assert matches[0].group(4).startswith(matches[0].group(2))
    assert "clone()" not in matches[0].group(0).split("Moss copy fact call:")[0].split("} ", 1)[1]
    run(["rustc", *rust_flags, rust, "-o", binary])
    assert run([binary]) == "copy\noriginal"

print("ORACLE-10A source is borrowed into one fact helper; double clone eliminated, Debug/Optimized/Fast Debug PASS")
