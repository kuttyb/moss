#!/usr/bin/env python3
"""Exercise the ABI-v7 .mossi loader with provider source removed."""

import json
from pathlib import Path
import shutil
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()


def run(args, *, cwd=None, expected=0):
    completed = subprocess.run(
        [str(item) for item in args], cwd=cwd, text=True,
        capture_output=True, timeout=180)
    assert completed.returncode == expected, (
        args, completed.returncode, completed.stdout, completed.stderr)
    return completed


if out.exists():
    shutil.rmtree(out)
(out / "src").mkdir(parents=True)
(out / "moss.toml").write_text(
    '[project]\nname="phase21_provider"\nversion="0.1.0"\n')
provider = out / "src/provider.moss"
provider.write_text('''module provider
export enum FileError:
  Full(code: Int)
export domain Worker:
  value: Int
  fn Bump():
    value = value + 1
  fn Read() -> Int:
    reply value
''')
main = out / "src/main.moss"
main.write_text('''module app
import provider
fn main():
  worker = provider.Worker(value: 3)
  message worker.Bump()
  echo message worker.Read()
''')

first = json.loads(run([compiler, "build", "--json"], cwd=out).stdout)
assert run([first["result"]["artifacts"]["executable"]]).stdout.strip() == "4"
interface_path = out / "build/debug/provider.mossi"
original = interface_path.read_text()
assert "native_abi 7\n" in original
for required in (
    'handler_abi "Bump"',
    'handler_outcome_contract "Bump" "__MossBodyOutcome_provider__Worker_Bump" "unit"',
    'handler_wrapper_contract "Bump" nested=propagate '
    'root=join,close_fileio,release_guards,acquire_failure_locks,execute_arm,fulfill_reply',
    'handler_reply_contract "Bump" "unit" 1 1',
    'handler_raise_tags "Bump" 0',
    'handler_failure_arms "Bump" 0',
    'handler_raising_nodes "Bump" 0',
    'handler_exceptional_edges "Bump" 0',
    'handler_exceptional_consumers "Bump" 63',
):
    assert required in original, required

# Freeze a nonempty provider contract as if Agent A had produced it. The
# checked provider source is then removed. The consumer must load the arm,
# owned capture, reply and exceptional-CFG records from .mossi alone.
synthetic = original.replace(
    'raise_set=-;may_diverge=0;unresolved=0\n'
    '  handler_state_effects "Bump"',
    'raise_set=enum:provider::FileError:Full;may_diverge=0;unresolved=0\n'
    '  handler_state_effects "Bump"',
    1,
)
synthetic = synthetic.replace(
    '  handler_raise_tags "Bump" 0\n',
    '  handler_raise_tags "Bump" 1\n'
    '  handler_raise_tag "Bump" 0 "enum:provider::FileError:Full" 1\n'
    '  handler_raise_payload "Bump" 0 "code" "Int" 0 1 1\n',
)
synthetic = synthetic.replace(
    '  handler_failure_arms "Bump" 0\n',
    '  handler_failure_arms "Bump" 1\n'
    '  handler_failure_arm "Bump" 0 "enum:provider::FileError:Full" '
    '"__moss_on_fail_provider__Worker_Bump_0" 1 1 0 1 "value" 0\n'
    '  handler_failure_capture "Bump" 0 "attempt" "Int" "attempt" 1 1\n',
)
synthetic = synthetic.replace(
    '  handler_raising_nodes "Bump" 0\n',
    '  handler_raising_nodes "Bump" 1 77\n',
)
synthetic = synthetic.replace(
    '  handler_exceptional_edges "Bump" 0\n',
    '  handler_exceptional_edges "Bump" 1\n'
    '  handler_exceptional_edge "Bump" 77 0 1 63 1 '
    '"enum:provider::FileError:Full"\n',
)
assert synthetic != original
interface_path.write_text(synthetic)
provider.rename(provider.with_suffix(".removed"))
run([compiler, "--check", main], cwd=out)
rebuilt = json.loads(run([compiler, "build", "--json"], cwd=out).stdout)
assert run([rebuilt["result"]["artifacts"]["executable"]]).stdout.strip() == "4"

# v6 is intentionally incompatible because fallible signatures and provider
# wrapper metadata changed. It must request a full provider rebuild.
interface_path.write_text(synthetic.replace("native_abi 7\n", "native_abi 6\n"))
v6 = run([compiler, "--check", main], cwd=out, expected=1)
assert "incompatible native calling convention" in v6.stderr
assert "rebuild its .mossi provider" in v6.stderr

# ABI-v7 with missing exceptional-consumer metadata also fails closed.
interface_path.write_text(synthetic.replace(
    '  handler_exceptional_consumers "Bump" 63\n', ""))
missing = run([compiler, "--check", main], cwd=out, expected=1)
assert "lacks ABI-v7 handler metadata" in missing.stderr

for required_record in (
    '  handler_reply_contract "Bump" "unit" 1 1\n',
    '  handler_raising_nodes "Bump" 1 77\n',
):
    interface_path.write_text(synthetic.replace(required_record, ""))
    missing = run([compiler, "--check", main], cwd=out, expected=1)
    assert "lacks ABI-v7 handler metadata" in missing.stderr

# Every effect fact is mandatory in v7, even when its value is false.
interface_path.write_text(synthetic.replace(";fileio=0", "", 1))
missing_effect = run([compiler, "--check", main], cwd=out, expected=1)
assert "lacks Phase 21 observable effects" in missing_effect.stderr

interface_path.write_text(synthetic)
run([compiler, "--check", main], cwd=out)
print("Phase 21.0 source-free ABI-v7 metadata, v6 rejection, and fail-closed loader checks passed.")
