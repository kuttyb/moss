#!/usr/bin/env python3
"""Phase 25 Type IR & Generics Conformance Test Suite.

Automates validation across all 11 conformance gates from TYPE_IR_CONTRACT.md Section 10:
- Gate 1: Independent Box[Int] and Box[String] coexistence, plus nominal inequality.
- Gate 2: Nested nominal constructors in collection literals and zero-iteration branches.
- Gate 3: One local binding rejects conflicting concrete instances.
- Gate 4: Option B open-result summaries: per-call freshening and ambiguous result rejection.
- Gate 5 & 6: Known-open nominal field access vs unconstrained receiver rejection.
- Gate 7 & 8: Finite nominal layout cycles rejected before native lowering.
- Gate 9 & 10: Cross-crate provider identity, public backend fields, and Moss field visibility.
- Gate 11: Semantic parity between Fast Debug (interpreter) and native compiled execution.
"""

import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
CONF_DIR = ROOT / "tests" / "phase25" / "g0" / "conformance"

POSITIVE_FIXTURES = [
    "local_pin_positive.moss",
    "open_result_multihop.moss",
    "open_result_zero_iteration.moss",
    "public_field_visibility_positive.moss",
    "nominal_instance_coexistence.moss",
    "nested_nominal_constructor.moss",
]

NEGATIVE_FIXTURES = {
    "open_result_ambiguous.moss": "TYPE_AMBIGUOUS_OPEN_RESULT",
    "open_result_conflict.moss": "TYPE_MISMATCH",
    "private_field_visibility_negative.moss": "FIELD_VISIBILITY_ERROR",
    "nominal_instance_assignment_conflict.moss": "TYPE_MISMATCH",
    "nominal_inequality_same_layout.moss": "TYPE_MISMATCH",
    "unconstrained_receiver_field_negative.moss": "UNTYPED_FIELD_ACCESS",
}


def run_cmd(args, cwd=ROOT, check=True):
    res = subprocess.run(
        [str(a) for a in args],
        cwd=cwd,
        capture_output=True,
        text=True,
    )
    if check and res.returncode != 0:
        raise AssertionError(
            f"Command {args} failed with exit code {res.returncode}\n"
            f"STDOUT:\n{res.stdout}\n"
            f"STDERR:\n{res.stderr}\n"
        )
    return res


def test_positive_fixtures(scratch_dir: Path):
    print("Testing positive conformance fixtures (check, interp, native parity)...")
    for name in POSITIVE_FIXTURES:
        src = CONF_DIR / name
        assert src.exists(), f"Missing positive fixture: {src}"

        # 1. Type check
        chk = run_cmd([COMPILER, "check", src, "--json"])
        payload = json.loads(chk.stdout)
        assert payload.get("ok"), f"{name} check failed: {payload}"

        # 2. Fast Debug (interpreter)
        interp = run_cmd([COMPILER, "run", "--interp", src])
        interp_stdout = interp.stdout

        # 3. Native lowering & Rust compilation
        stem = src.stem
        rs_path = scratch_dir / f"{stem}.rs"
        bin_path = scratch_dir / f"{stem}.bin"

        run_cmd([COMPILER, src, "-o", rs_path])
        run_cmd(["rustc", rs_path, "-o", bin_path])

        # 4. Native execution & Parity Check
        native = run_cmd([bin_path])
        native_stdout = native.stdout

        assert interp_stdout == native_stdout, (
            f"Execution mismatch on {name}:\n"
            f"  Interpreter output:\n{interp_stdout}\n"
            f"  Native output:\n{native_stdout}\n"
        )
        print(f"  [PASS] {name} (interp == native: {repr(interp_stdout.strip())})")


def test_negative_fixtures():
    print("Testing negative conformance diagnostics...")
    for name, expected_code in NEGATIVE_FIXTURES.items():
        src = CONF_DIR / name
        assert src.exists(), f"Missing negative fixture: {src}"

        res = run_cmd([COMPILER, "check", src, "--json"], check=False)
        assert res.returncode != 0, f"Expected {name} to fail check, but it succeeded!"
        payload = json.loads(res.stdout)
        err = payload.get("error", {})
        actual_code = err.get("code")
        assert actual_code == expected_code, (
            f"On {name}: expected diagnostic code '{expected_code}', got '{actual_code}'\n"
            f"Message: {err.get('message')}\n"
        )
        print(f"  [PASS] {name} (rejected with {expected_code})")


def test_cross_crate_feasibility():
    print("Testing cross-crate provider identity & public backend fields...")
    feasibility_script = ROOT / "tests" / "phase25" / "g0" / "check_rust_feasibility.py"
    assert feasibility_script.exists()
    run_cmd([sys.executable, feasibility_script])
    print("  [PASS] Phase 25 G0 Rust feasibility prototype")


def test_recursive_layouts():
    print("Testing finite recursive nominal layout rejection...")
    layout_script = ROOT / "tests" / "tooling" / "check_phase25_recursive_layout.py"
    assert layout_script.exists()
    run_cmd([sys.executable, layout_script, COMPILER])
    print("  [PASS] Phase 25 recursive layout checks")


def main():
    (ROOT / "tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="phase25-conf-", dir=ROOT / "tmp") as scratch:
        scratch_dir = Path(scratch)
        test_positive_fixtures(scratch_dir)
        test_negative_fixtures()
        test_cross_crate_feasibility()
        test_recursive_layouts()
    print("\nPASS: All Phase 25 Type IR & Generics Conformance Gates Passed Cleanly.")


if __name__ == "__main__":
    main()
