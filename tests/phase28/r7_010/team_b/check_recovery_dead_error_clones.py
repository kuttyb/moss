"""Drop dead selected-error clones while preserving bare re-raise behavior."""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / "tmp/team-b/recovery-dead-error-clones"
OUT.mkdir(parents=True, exist_ok=True)
MOSS = Path(sys.argv[1]).resolve()
ORACLE07 = ROOT / "tests/phase28/oracle/fixtures/oracle_07_catchable_collection_errors.moss"
RERAISE = ROOT / "tests/tooling/fixtures/phase21_recovery_native.moss"


def run(args, expect=0):
    proc = subprocess.run(list(map(str, args)), cwd=ROOT, text=True,
                          capture_output=True, check=False, timeout=90)
    if proc.returncode != expect:
        raise RuntimeError(f"{args!r} returned {proc.returncode}\n"
                           f"{proc.stdout}\n{proc.stderr}")
    return proc.stdout.strip(), proc.stderr


for mode, moss_flags, rust_flags in (
        ("debug", [], []), ("optimized", ["-O"], ["-O"])):
    rust = OUT / f"oracle07-{mode}.rs"
    binary = OUT / f"oracle07-{mode}"
    run([MOSS, *moss_flags, ORACLE07, "-o", rust])
    generated = rust.read_text()
    assert "let __moss_selected_error =" not in generated
    clone_count = len(re.findall(r"\.clone\(\)", generated))
    assert clone_count == 6, (mode, clone_count)
    run(["rustc", *rust_flags, rust, "-o", binary])

reraised_fast = run([MOSS, "run", "--interp", RERAISE])[0]
for mode, moss_flags, rust_flags in (
        ("debug", [], []), ("optimized", ["-O"], ["-O"])):
    rust = OUT / f"reraise-{mode}.rs"
    binary = OUT / f"reraise-{mode}"
    run([MOSS, *moss_flags, RERAISE, "-o", rust])
    generated = rust.read_text()
    assert "let __moss_selected_error = __MossRaised::" in generated
    assert "return Err(__moss_selected_error);" in generated
    assert len(re.findall(r"\.clone\(\)", generated)) == 7
    run(["rustc", *rust_flags, rust, "-o", binary])
    assert run([binary])[0] == reraised_fast

print("ORACLE-07 dead recovery clones removed (9 to 0), runtime clone count 6; bare re-raise clone retained, native/Fast Debug parity PASS")
