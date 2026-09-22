#!/usr/bin/env python3
"""Phase 15.3: static branch placement has no runtime lock plan."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path


compiler = Path(sys.argv[1]).resolve()
repository = Path(__file__).resolve().parents[2]


def run(args, cwd=None):
    result = subprocess.run([str(compiler), *map(str, args)], cwd=cwd,
                            text=True, capture_output=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def plan(source):
    return json.loads(run(["inspect", "main", "--source", source, "--json"]))["result"]["synchronization_plan"]


scratch = repository / "tmp"
scratch.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix="phase153-", dir=scratch) as temporary:
    root = Path(temporary)
    source = repository / "tests/phase153_path_placement.moss"
    rust = root / "branch.rs"
    run([source, "-o", rust])
    executable = root / "branch"
    subprocess.run(["rustc", "-D", "warnings", rust, "-o", executable], check=True)
    assert subprocess.check_output([executable], text=True) == "11\n"
    handler = next(h for h in plan(source)["domains"][0]["handlers"] if h["name"] == "Update")
    placement = handler["path_placement"]
    assert placement["kind"] == "leading_conditional_continuation_split"
    assert placement["entry"] and placement["then"] and placement["else"]
    text = rust.read_text()
    # Condition is evaluated before the two branch-local typed state views;
    # the wrappers contain no dynamically interpreted acquire-if-not-held plan.
    assert "let __moss_branch" in text
    assert "UpdateThenState" in text and "UpdateElseState" in text
    assert "MossHandlerFrame" not in text and "acquire_if_not_held" not in text

    # A high-ranked condition forces both lower branch classes to entry.  The
    # opposite arm then cancels the untouched guard before it would acquire a
    # later class; this is not a touched-lock shrinking transition.
    forced = root / "forced.moss"
    forced.write_text("""domain Forced:
  a_left: Int
  m_right: Int
  z_marker: Int
  fn Update() -> Int:
    if z_marker > 0:
      a_left = a_left + 1
      reply a_left
    else:
      m_right = m_right + 1
      reply m_right
  fn LeftOnly():
    a_left = a_left + 1
  fn RightOnly():
    m_right = m_right + 1
  fn MarkerOnly():
    z_marker = z_marker + 1
fn main():
  value = Forced(a_left: 1, m_right: 2, z_marker: 1)
  echo message value.Update()
""")
    forced_rust = root / "forced.rs"
    run([forced, "-o", forced_rust])
    assert "lock_cancelled" in forced_rust.read_text()
    forced_handler = next(h for h in plan(forced)["domains"][0]["handlers"] if h["name"] == "Update")
    assert forced_handler["path_placement"]["cancel_then"]
    assert forced_handler["path_placement"]["cancel_else"]

print("Phase 15.3 path-sensitive placement checks passed")
