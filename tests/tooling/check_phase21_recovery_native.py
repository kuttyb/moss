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

# `return` inside a try exits the Moss function. A Rust closure around the
# attempt would instead return from that closure and could not preserve the
# function's plain return type.
return_source = repo / "tests/tooling/fixtures/phase21_try_return_native.moss"
return_rust = out / "try_return.rs"
return_executable = out / "try_return"
assert run(compiler, return_source, "-o", return_rust).returncode == 0
return_compiled = run(rustc, "--edition=2021", "-Dwarnings", return_rust,
                      "-o", return_executable)
assert return_compiled.returncode == 0, return_compiled.stderr
return_executed = run(return_executable)
assert return_executed.returncode == 0, return_executed.stderr
assert return_executed.stdout == "7\n9\n", return_executed.stdout

print("Phase 21 native local recovery probes passed.")
