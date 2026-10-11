#!/usr/bin/env python3
"""Reproduce the unresolved ABI-v8 provider/interface authenticity defect.

This is a known-issue reproducer, not a green acceptance gate. It exits zero
only when an edited, self-consistent .mossi builds against the old rlib and
the resulting program aborts. Source-free acceptance requires this behavior
to become a checker rejection under an approved provider-bound contract.
"""

import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
OUT = ROOT / "tmp/phase21-mossi-pattern-forgery"
shutil.rmtree(OUT, ignore_errors=True)
(OUT / "src").mkdir(parents=True)
(OUT / "moss.toml").write_text(
    '[project]\nname="phase21_pattern_forgery"\nversion="0.1.0"\n')
provider = OUT / "src/service.moss"
provider.write_text('''module service
export enum E:
  A
export enum F:
  B
export domain Worker:
  count: Int
  fn Run(which: Int) -> Int:
    if which == 1:
      raise E.A
    raise F.B
  on_fail E.A:
    count = count + 1
    reply count
  on_fail F.B:
    count = count + 2
    reply count
''')
main = OUT / "src/main.moss"
main.write_text('''module app
import service
fn main():
  worker = service.Worker(count: 0)
  echo message worker.Run(1)
  echo message worker.Run(2)
''')


def run(*command, expected=0):
    result = subprocess.run(
        [str(part) for part in command], cwd=OUT, capture_output=True,
        text=True, timeout=180)
    assert result.returncode == expected, (command, result.returncode,
                                            result.stdout, result.stderr)
    return result


built = json.loads(run(COMPILER, "build", "--json").stdout)
assert built["ok"], built
executable = Path(built["result"]["artifacts"]["executable"])
assert run(executable).stdout == "1\n3\n"
provider.rename(provider.with_suffix(".removed"))
interface = OUT / "build/debug/service.mossi"
text = interface.read_text()
patterns = {
    'handler_failure_arm "Run" 0 "enum:service__E:A"':
        'handler_failure_arm "Run" 0 "enum:service__F:B"',
    'handler_failure_arm "Run" 1 "enum:service__F:B"':
        'handler_failure_arm "Run" 1 "enum:service__E:A"',
}
for before, after in patterns.items():
    assert text.count(before) == 2, before
    text = text.replace(before, after)

contract, semantic = text.split("\ncontract\n", 1)[1].split(
    "\nsemantic_exports\n", 1)


def hash_section(section):
    value = 14695981039346656037
    for byte in section.encode():
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return f"{value:016x}"


for key, section in (("concrete_interface_hash", contract + "\n"),
                     ("interface_hash", contract + "\n"),
                     ("generic_semantic_hash", semantic)):
    text, count = re.subn(rf"(?m)^{key} [0-9a-f]{{16}}$",
                          f"{key} {hash_section(section)}", text)
    assert count == 1, (key, count)
interface.write_text(text)

rebuilt = json.loads(run(COMPILER, "build", "--json").stdout)
assert rebuilt["ok"], rebuilt
outcome = subprocess.run(
    [str(Path(rebuilt["result"]["artifacts"]["executable"]))],
    cwd=OUT, capture_output=True, text=True, timeout=30)
assert outcome.returncode != 0 and not outcome.stdout, outcome
print(f"REPRODUCED NO-GO: forged ABI-v8 pattern builds, then exits {outcome.returncode}")
