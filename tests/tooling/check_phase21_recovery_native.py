#!/usr/bin/env python3
"""Native tagged-outcome coverage for Phase 21 local recovery."""

from pathlib import Path
import shutil
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1] if len(sys.argv) > 1 else repo / "moss").resolve()
source = repo / "tests/tooling/fixtures/phase21_recovery_native.moss"
out = repo / "tmp/phase21-recovery-native"
out.mkdir(parents=True, exist_ok=True)
rust = out / "recovery.rs"
executable = out / "recovery"


def run(*args):
    return subprocess.run(
        [str(item) for item in args], cwd=repo, text=True,
        capture_output=True, timeout=120)


lowered = run(compiler, source, "-o", rust)
assert lowered.returncode == 0, (lowered.stdout, lowered.stderr)

rustc = shutil.which("rustc")
assert rustc is not None, "rustc is required for native recovery coverage"
compiled = run(rustc, "--edition=2021", "-Dwarnings", rust, "-o", executable)
assert compiled.returncode == 0, (compiled.stdout, compiled.stderr)

executed = run(executable)
assert executed.returncode == 0, (executed.stdout, executed.stderr)
assert executed.stdout == "10\n20\n30\nreraised\n", executed.stdout

print("Phase 21 native local recovery probes passed.")
