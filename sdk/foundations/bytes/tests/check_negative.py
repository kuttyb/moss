"""Check that invalid byte input fails in both native and Fast Debug paths."""

from pathlib import Path
import subprocess


package = Path(__file__).resolve().parents[1]
repo = package.parents[2]
project = package / "tests" / "negative_input"
margo = repo / "margo"

for mode in ("run", "debug"):
    result = subprocess.run(
        [str(margo), mode],
        cwd=project,
        text=True,
        capture_output=True,
        check=False,
    )
    output = result.stdout + result.stderr
    if result.returncode == 0 or "assertion failed" not in output.lower():
        raise SystemExit(f"{mode}: expected invalid-byte assertion, got {result.returncode}: {output}")
    print(f"{mode}: invalid byte rejected")
