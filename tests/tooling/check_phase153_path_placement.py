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
    assert subprocess.check_output([executable], text=True) == "1\n"
    handler = next(h for h in plan(source)["domains"][0]["handlers"] if h["name"] == "Update")
    placement = handler["path_placement"]
    assert placement["kind"] == "leading_conditional_continuation_split"
    assert placement["entry"] and (placement["then"] or placement["else"])
    text = rust.read_text()
    # Condition is evaluated before the two branch-local typed state views;
    # the wrappers contain no dynamically interpreted acquire-if-not-held plan.
    assert "let __moss_branch" in text
    assert "UpdateThenState" in text and "UpdateElseState" in text
    assert "MossHandlerFrame" not in text and "acquire_if_not_held" not in text
    # The source-level tail is duplicated into both typed continuations, so a
    # branch-touched guard cannot die at the source CFG join.
    assert text.count("tail = tail + 1") >= 2

    # A high-ranked condition forces both lower branch classes to entry.  The
    # opposite arm then cancels the untouched guard before it would acquire a
    # later class; this is not a touched-lock shrinking transition.
    forced = root / "forced.moss"
    forced.write_text("""domain Forced:
  a_left: Int
  m_marker: Int
  z_right: Int
  fn Update() -> Int:
    if m_marker > 0:
      a_left = a_left + 1
      reply a_left
    else:
      z_right = z_right + 1
      reply z_right
  fn LeftOnly():
    a_left = a_left + 1
  fn RightOnly():
    z_right = z_right + 1
  fn MarkerOnly():
    m_marker = m_marker + 1
fn main():
  value = Forced(a_left: 1, m_marker: 1, z_right: 2)
  echo message value.Update()
""")
    forced_rust = root / "forced.rs"
    run([forced, "-o", forced_rust])
    forced_text = forced_rust.read_text()
    assert "lock_cancelled" in forced_text
    forced_handler = next(h for h in plan(forced)["domains"][0]["handlers"] if h["name"] == "Update")
    assert forced_handler["path_placement"]["cancel_else"]
    runtime_impl = forced_text.split("impl ForcedRef {", 1)[1]
    wrapper = runtime_impl.split("fn Update_shared", 1)[1].split("fn LeftOnly_shared", 1)[0]
    assert wrapper.index("lock_cancelled") < wrapper.index("moss_write_or_abort", wrapper.index("lock_cancelled"))

    # A message-valued condition is an observable-action barrier. It uses the
    # conservative full entry plan instead of emitting a post-condition arm.
    observable = root / "observable_condition.moss"
    observable.write_text("""domain Decider:
  fn Pick() -> Bool:
    reply true

domain Observable:
  a: Int
  b: Int
  domainroutes(decider: Decider)
  fn Update() -> Int:
    if message decider.Pick():
      a = a + 1
      reply a
    else:
      b = b + 1
      reply b
  fn TouchA():
    a = a + 1
  fn TouchB():
    b = b + 1

fn main():
  decider = Decider()
  value = Observable(a: 0, b: 0, decider: decider)
  echo message value.Update()
""")
    observable_rust = root / "observable.rs"
    run([observable, "-o", observable_rust])
    observable_exe = root / "observable"
    subprocess.run(["rustc", "-D", "warnings", observable_rust, "-o", observable_exe], check=True)
    assert subprocess.check_output([observable_exe], text=True) == "1\n"
    update = next(h for h in plan(observable)["domains"] if h["domain"] == "Observable")["handlers"]
    update = next(h for h in update if h["name"] == "Update")
    assert update["path_placement"]["kind"] == "entry"
    assert "observable-action barrier" in update["path_placement"]["reason"]
    assert "ObservableUpdateThenState" not in observable_rust.read_text()

    # A loop in a selected continuation receives its class before the typed
    # body starts; no acquisition is emitted inside the loop body.
    looped = root / "looped.moss"
    looped.write_text("""domain Looped:
  a_flag: Int
  z_value: Int
  fn Update() -> Int:
    if a_flag > 0:
      index = 0
      while index < 2:
        z_value = z_value + 1
        index = index + 1
    reply z_value
  fn TouchValue():
    z_value = z_value + 1
  fn TouchFlag():
    a_flag = a_flag + 1
fn main():
  value = Looped(a_flag: 1, z_value: 0)
  echo message value.Update()
""")
    looped_rust = root / "looped.rs"
    run([looped, "-o", looped_rust])
    loop_text = looped_rust.read_text()
    loop_body = loop_text.split("while", 1)[1].split("}", 1)[0]
    assert "moss_read_or_abort" not in loop_body and "moss_write_or_abort" not in loop_body

    # The whole-handler mode remains conservative: one write path makes the
    # shared class EXCLUSIVE rather than attempting a shared-to-exclusive upgrade.
    modes = root / "modes.moss"
    modes.write_text("""domain Modes:
  flag: Int
  value: Int
  fn Update() -> Int:
    if flag > 0:
      value = value + 1
    reply value
  fn Change():
    value = value + 1
  fn ChangeFlag():
    flag = flag + 1
fn main():
  value = Modes(flag: 1, value: 0)
  echo message value.Update()
""")
    modes_rust = root / "modes.rs"
    run([modes, "-o", modes_rust])
    mode_handler = next(h for h in plan(modes)["domains"][0]["handlers"] if h["name"] == "Update")
    assert "EXCLUSIVE" in mode_handler["class_modes"].values()
    assert "try_read" not in modes_rust.read_text().split("fn Update_shared", 1)[1].split("fn Change_shared", 1)[0]

    # Existing Phase 15.1 borrowed lowering also composes with a selected
    # path: a protected vector is lent to the synchronous callee and the
    # sender continuation retains its backing class through that call.
    borrowed = root / "borrowed_path.moss"
    borrowed.write_text("""domain Sink:
  fn Sum(values: Vector[Int]) -> Int:
    reply values |> sum

domain Producer:
  a_flag: Int
  z_values: Vector[Int]
  domainroutes(sink: Sink)
  fn Update() -> Int:
    if a_flag > 0:
      total = message sink.Sum(z_values)
      reply total
    else:
      reply 0
  fn Replace():
    z_values = [9]
  fn ChangeFlag():
    a_flag = a_flag + 1

fn main():
  sink = Sink()
  producer = Producer(a_flag: 1, z_values: [1, 2, 3], sink: sink)
  echo message producer.Update()
""")
    borrowed_rust = root / "borrowed_path.rs"
    run([borrowed, "-o", borrowed_rust])
    borrowed_exe = root / "borrowed_path"
    subprocess.run(["rustc", "-D", "warnings", borrowed_rust, "-o", borrowed_exe], check=True)
    assert subprocess.check_output([borrowed_exe], text=True) == "6\n"
    borrowed_text = borrowed_rust.read_text()
    producer = borrowed_text.split("impl ProducerRef {", 1)[1]
    wrapper = producer.split("fn Update_shared", 1)[1].split("fn Replace_shared", 1)[0]
    assert "Sum_shared" in borrowed_text
    assert "__moss_body_Producer_Update_Then" in wrapper
    assert wrapper.index("__moss_body_Producer_Update_Then") < wrapper.index("drop(__class")

print("Phase 15.3 path-sensitive placement checks passed")
