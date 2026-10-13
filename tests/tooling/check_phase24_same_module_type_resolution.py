"""Validate same-module type resolution in types and expressions."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve()
MARGO = ROOT / "margo"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)

with tempfile.TemporaryDirectory(prefix="phase24-type-resolution-", dir=SCRATCH) as name:
    work = Path(name)
    pkg = work / "probe_pkg"
    (pkg / "src").mkdir(parents=True)
    (pkg / "Moss.toml").write_text(
        '[package]\nname = "moss.type_resolution_probe"\nversion = "0.1.0"\n'
        '[build]\nsource = "src"\n', encoding="utf-8")
    (pkg / "src" / "main.moss").write_text(
        'module probe_pkg\n'
        '\n'
        'export type State:\n'
        '  value: Int\n'
        '\n'
        'type PrivateState:\n'
        '  tag: String\n'
        '\n'
        'fn add_state(states: Vector[State]):\n'
        '  pass\n'
        '\n'
        'fn add_state_qualified(states: Vector[probe_pkg.State]):\n'
        '  pass\n'
        '\n'
        'fn add_private(states: Vector[PrivateState]):\n'
        '  pass\n'
        '\n'
        'fn add_private_qualified(states: Vector[probe_pkg.PrivateState]):\n'
        '  pass\n'
        '\n'
        'fn make_state() -> Vector[State]:\n'
        '  return Vector[State]()\n'
        '\n'
        'fn make_state_qualified() -> Vector[probe_pkg.State]:\n'
        '  return Vector[probe_pkg.State]()\n'
        '\n'
        'fn main():\n'
        '  var states = Vector[State]()\n'
        '  add_state(states)\n'
        '  add_state_qualified(states)\n'
        '\n'
        '  var q_states = Vector[probe_pkg.State]()\n'
        '  add_state(q_states)\n'
        '  add_state_qualified(q_states)\n'
        '\n'
        '  var priv_states = Vector[PrivateState]()\n'
        '  add_private(priv_states)\n'
        '  add_private_qualified(priv_states)\n'
        '\n'
        '  var q_priv_states = Vector[probe_pkg.PrivateState]()\n'
        '  add_private(q_priv_states)\n'
        '  add_private_qualified(q_priv_states)\n'
        '\n'
        '  var s1 = make_state()\n'
        '  var s2 = make_state_qualified()\n'
        '  add_state(s1)\n'
        '  add_state(s2)\n'
        '  add_state_qualified(s1)\n'
        '  add_state_qualified(s2)\n',
        encoding="utf-8")

    # 1. moss check
    check = subprocess.run([str(MOSS), "check", "src/main.moss"],
                           cwd=pkg, capture_output=True, text=True)
    assert check.returncode == 0, f"moss check failed:\n{check.stderr}\n{check.stdout}"

    # 2. moss run --interp (Fast Debug)
    interp = subprocess.run([str(MOSS), "run", "--interp", "src/main.moss"],
                            cwd=pkg, capture_output=True, text=True)
    assert interp.returncode == 0, f"moss run --interp failed:\n{interp.stderr}\n{interp.stdout}"

    # 3. moss build (native debug)
    build = subprocess.run([str(MOSS), "build"],
                           cwd=pkg, capture_output=True, text=True)
    assert build.returncode == 0, f"moss build failed:\n{build.stderr}\n{build.stdout}"

    # 4. run native executable
    debug_binaries = list((pkg / "build" / "debug").iterdir())
    assert len(debug_binaries) > 0, "expected binary in build/debug"
    exe = [b for b in debug_binaries if not b.suffix][0]
    run = subprocess.run([str(exe)], capture_output=True, text=True)
    assert run.returncode == 0, f"execution failed:\n{run.stderr}\n{run.stdout}"

    # 5. Verify cross-module private access is still rejected
    neg_pkg = work / "neg_pkg"
    (neg_pkg / "src").mkdir(parents=True)
    (neg_pkg / "Moss.toml").write_text(
        '[package]\nname = "neg_pkg"\nversion = "0.1.0"\n'
        '[build]\nsource = "src"\n',
        encoding="utf-8")
    (neg_pkg / "src" / "probe.moss").write_text(
        'module probe\n'
        'export type PublicState:\n'
        '  value: Int\n'
        'type PrivateState:\n'
        '  value: Int\n',
        encoding="utf-8")
    (neg_pkg / "src" / "main.moss").write_text(
        'module app\nimport probe\n'
        'fn main():\n'
        '  var priv = Vector[probe.PrivateState]()\n',
        encoding="utf-8")
    priv_check = subprocess.run([str(MOSS), "check", "src/main.moss"],
                                cwd=neg_pkg, capture_output=True, text=True)
    assert priv_check.returncode != 0, "private type from another module should be rejected"
    assert "PrivateState" in priv_check.stderr or "PrivateState" in priv_check.stdout, \
        f"expected PrivateState in diagnostic, got:\n{priv_check.stderr}\n{priv_check.stdout}"

print("check_phase24_same_module_type_resolution: ok")
