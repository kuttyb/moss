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
CAPTURE_RERAISE = ROOT / "tests/tooling/fixtures/phase21_capture_nested_reraise.moss"


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
    assert "let __moss_selected_error = __MossRaised::" not in generated
    assert re.search(
        r"return Err\(__MossRaised::Raised_[0-9a-f]+\(__moss_selected_value\)\);",
        generated)
    assert len(re.findall(r"\.clone\(\)", generated)) == 6
    assert "__moss_selected_value.clone()" not in generated
    run(["rustc", *rust_flags, rust, "-o", binary])
    assert run([binary])[0] == reraised_fast

capture_source = OUT / "capture_nested_reraise_int.moss"
capture_text = CAPTURE_RERAISE.read_text()
capture_text = capture_text.replace("fn Run() -> String:", "fn Run() -> Int:")
capture_text = capture_text.replace('let item = "kept"', "let item = 7")
capture_text = capture_text.replace('reply "ok"', "reply 1")
capture_source.write_text(capture_text)
assert "reply item" in capture_text, "captured payload reply was lost"
capture_fast = run([MOSS, "run", "--interp", capture_source])[0]
assert capture_fast == "4\n7", capture_fast
for mode, moss_flags, rust_flags in (
        ("debug", [], []), ("optimized", ["-O"], ["-O"])):
    rust = OUT / f"captured-reraise-{mode}.rs"
    binary = OUT / f"captured-reraise-{mode}"
    run([MOSS, *moss_flags, capture_source, "-o", rust])
    generated = rust.read_text()
    assert re.search(r"Err\(__moss_selected_carrier\)", generated), (
        "captured bare raise did not forward the carrier", mode)
    run(["rustc", *rust_flags, rust, "-o", binary])
    assert run([binary])[0] == capture_fast

print("ORACLE-07 dead recovery clones removed (9 to 0), runtime clone count 6; uncaptured and captured bare re-raise Native/Fast Debug parity PASS")
