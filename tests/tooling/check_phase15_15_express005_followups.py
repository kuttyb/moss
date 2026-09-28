#!/usr/bin/env python3
"""EXPRESS-005 post-merge review cases.

See docs/PHASE_15_15_EXPRESS_005_REVIEW.md for the findings these cases
reproduce. Every case states the behavior the Moss documentation specifies
and checks the checker, native lowering (``rustc -D warnings``, as
``moss build`` uses), and Fast Debug against it.

Case kinds:

  bug      Unfixed defect (F*). Expected to fail in default mode.
  open     Unresolved design question (D*), pinned to current behavior.
  control  Documented idiom or workaround (C*) that must keep working.
           Fixed F* cases and decided D* cases remain here as permanent probes.

Usage:

  python3 tests/tooling/check_phase15_15_express005_followups.py [compiler]
      Default regression mode. Exit 0 when all decided cases hold.
  ... --expect-fixed      Require bug cases to pass (the target of a fix).
  ... --only F4a,F4b      Run selected cases only.
  ... --list              Print the case registry.
"""

import argparse
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / "tmp" / "express005_followups"
TIMEOUT = 300


@dataclass
class Case:
    case_id: str
    kind: str  # bug | open | control
    title: str
    source: str = ""
    # run         checker accepts; native and Fast Debug print `output`
    # reject      checker rejects; diagnostic contains `needle` if given
    # consistent  checker rejects, or native and Fast Debug agree
    # effects     `moss effects target` has observable_effects[key] == value
    # plan        synchronization plan read/write/mode expectations
    # native      checker and native lowering print `output` (for unsupported Fast Debug for-loops)
    # module_run  multi-file project; margo run and margo debug print `output`
    check: str = "run"
    output: str = ""
    needle: str = ""
    target: str = ""
    effect: tuple = ()
    files: dict = field(default_factory=dict)


PHASE = """enum Phase:
  Idle
  Running(job: Int)
  Done(job: Int)
"""

TEXT_PHASE = """enum Phase:
  Idle
  Done(name: String)
"""

CASES = [
    # ------------------------------------------------------------------ F1
    Case("F1a", "control", "payload constructor is an unresolved observable effect",
         """enum Signal:
  Off
  On(value: Int)

fn make_on() -> Signal:
  return Signal.On(value: 4)

fn main():
  let signal = make_on()
  match signal:
    case Off:
      echo 0
    case On(value):
      echo value
""", check="effects", target="make_on", effect=("unresolved", False)),
    Case("F1b", "control", "payload constructor rejected as a domain state default",
         PHASE + """
domain Machine:
  phase: Phase = Phase.Running(job: 1)

  fn Get() -> Int:
    match phase:
      case Idle:
        reply 0
      case Running(job):
        reply job
      case Done(job):
        reply 0 - job

fn main():
  machine = Machine()
  echo message machine.Get()
""", output="1"),
    Case("F1c", "control", "payload constructor rejected as a main constructor argument",
         PHASE + """
domain Machine:
  phase: Phase

  fn Get() -> Int:
    match phase:
      case Idle:
        reply 0
      case Running(job):
        reply job
      case Done(job):
        reply 0 - job

fn main():
  machine = Machine(phase = Phase.Running(job: 5))
  echo message machine.Get()
""", output="5"),
    # ------------------------------------------------------------------ F2
    Case("F2", "control", "unannotated enum state is not a known match type",
         PHASE + """
domain Machine:
  phase = Phase.Idle

  fn Get() -> Int:
    match phase:
      case Idle:
        reply 0
      case Running(job):
        reply job
      case Done(job):
        reply 0 - job

fn main():
  machine = Machine()
  echo message machine.Get()
""", output="0"),
    Case("F2b", "control", "inferred payload-bearing domain state is matchable",
         PHASE + """
domain Machine:
  phase = Phase.Running(job: 7)

  fn Get() -> Int:
    match phase:
      case Idle:
        reply 0
      case Running(job):
        reply job
      case Done(job):
        reply 0 - job

fn main():
  machine = Machine()
  echo message machine.Get()
""", output="7"),
    # ------------------------------------------------------------------ F3
    Case("F3", "control", "enum popped from an inferred Queue is not a known match type",
         """enum Msg:
  Text(body: String)
  Quit

fn main():
  var q = Queue()
  q.push(Msg.Text(body: "queued"))
  q.push(Msg.Quit)
  let first = q.pop()
  match consume first:
    case Text(body):
      echo body
    case Quit:
      echo "quit"
""", output="queued"),
    # ------------------------------------------------------------------ F4
    Case("F4a", "control", "READ view: returning an Int computed from a view is rejected",
         TEXT_PHASE + """
fn size(p: Phase) -> Int:
  match p:
    case Idle:
      return 0
    case Done(name):
      return name.length()

fn main():
  echo size(Phase.Done(name: "abc"))
""", output="3"),
    Case("F4b", "control", "READ view: returning a new String built from a view is rejected",
         TEXT_PHASE + """
fn describe(p: Phase) -> String:
  match p:
    case Idle:
      return "idle"
    case Done(name):
      return name + "!"

fn main():
  echo describe(Phase.Done(name: "a"))
""", output="a!"),
    Case("F4c", "control", "READ view: replying an Int computed from a view is rejected",
         """enum Result:
  Error(code: Int, message: String)
  Cancelled

domain Source:
  fn Fetch(result: Result) -> Int:
    match result:
      case Error(code, message):
        reply message.length()
      case Cancelled:
        reply 0

fn main():
  source = Source()
  echo message source.Fetch(Result.Error(code: 2, message: "boom"))
""", output="4"),
    Case("F4d", "control", "READ view: replying a pipeline terminal over a view is rejected",
         """enum Batch:
  Items(values: Vector[Int])
  Empty

domain Sum:
  fn Total(batch: Batch) -> Int:
    match batch:
      case Items(values):
        reply values |> sum
      case Empty:
        reply 0

fn main():
  s = Sum()
  echo message s.Total(Batch.Items(values: [1, 2, 3]))
""", output="6"),
    Case("F4e", "control", "READ view: helper result inside an enum constructor is rejected",
         TEXT_PHASE + """
fn copy_text(text: String) -> String:
  return text + ""

fn main():
  let p = Phase.Done(name: "a")
  match p:
    case Idle:
      next = Phase.Idle
    case Done(name):
      next = Phase.Done(name: copy_text(name))
  match next:
    case Idle:
      echo "idle"
    case Done(name):
      echo name
""", output="a"),
    # ------------------------------------------------------------------ F5-F10
    Case("F5", "control", "`_` pattern binding: checker, native, and Fast Debug disagree",
         """enum Span:
  Range(lo: Int, hi: Int)
  Empty

fn main():
  let s = Span.Range(lo: 2, hi: 10)
  match s:
    case Range(_, hi):
      echo hi
    case Empty:
      echo 0
""", check="consistent"),
    Case("F6", "control", "assigning a scalar payload binding: engines disagree",
         """enum Box:
  Num(value: Int)
  Nothing

fn main():
  let b = Box.Num(value: 1)
  match b:
    case Num(value):
      value = value + 1
      echo value
    case Nothing:
      echo 0
""", check="consistent"),
    Case("F7", "control", "enum-typed Map key is accepted (docs: no hashing)",
         """enum Color:
  Red
  Blue

fn main():
  var counts = Map()
  counts[Color.Red] = 1
  echo counts.get(Color.Red, 0)
""", check="reject"),
    Case("F8", "control", "`value.field` projection on an enum is accepted",
         """enum Box:
  Num(value: Int)
  Nothing

fn main():
  let b = Box.Num(value: 1)
  echo b.value
""", check="reject"),
    Case("F9", "control", "trait-typed enum payload field is accepted",
         """trait Shape:
  fn area() -> Int

enum Holder:
  Has(shape: Shape)
  Empty

fn main():
  echo 0
""", check="reject"),
    Case("F10", "control", "`Enum.Case(field = value)`: native accepts, Fast Debug fails",
         """enum Box:
  Num(value: Int)
  Nothing

fn main():
  let b = Box.Num(value = 1)
  match b:
    case Num(value):
      echo value
    case Nothing:
      echo 0
""", check="consistent"),
    # ------------------------------------------------------------------ F11
    Case("F11a", "control", "match consume of an outer binding inside a loop is accepted",
         """enum Msg:
  Text(body: String)
  Quit

fn main():
  let m = Msg.Text(body: "once")
  var i = 0
  while i < 2:
    match consume m:
      case Text(body):
        echo body
      case Quit:
        echo "quit"
    i = i + 1
""", check="reject"),
    Case("F11b", "control", "general: moving an outer binding inside a loop is accepted",
         """fn main():
  let s = "once"
  var i = 0
  while i < 2:
    let t = s
    echo t
    i = i + 1
""", check="reject"),
    # ------------------------------------------------------------------ F12
    Case("F12a", "control", "var initialized, then assigned in every arm, fails the native build",
         PHASE + """
domain Machine:
  phase: Phase = Phase.Idle

  fn Step():
    var next = Phase.Idle
    match phase:
      case Idle:
        next = Phase.Running(job: 1)
      case Running(job):
        next = Phase.Done(job: job)
      case Done(job):
        next = Phase.Idle
    phase = next

  fn Current() -> Int:
    match phase:
      case Idle:
        reply 0
      case Running(job):
        reply job
      case Done(job):
        reply 0 - job

fn main():
  machine = Machine()
  message machine.Step()
  echo message machine.Current()
  message machine.Step()
  echo message machine.Current()
""", output="1\n-1"),
    Case("F12b", "control", "general: var assigned on both if/else paths fails the native build",
         """fn main():
  let ready = true
  var label = "none"
  if ready:
    label = "go"
  else:
    label = "wait"
  echo label
""", output="go"),
    # ------------------------------------------------------------------ F13
    Case("F13", "control", "bare unknown identifier statement is accepted",
         """enum Color:
  Red
  Blue

fn main():
  let c = Color.Red
  match c:
    case Red:
      frobnicate
    case Blue:
      echo 2
""", check="reject", needle="INVALID_BARE_IDENTIFIER_STATEMENT"),
    # ------------------------------------------------------------------ decided design cases
    Case("D1", "control", "payload pattern names must match declaration order",
         """enum Span:
  Range(lo: Int, hi: Int)
  Empty

fn width(s: Span) -> Int:
  match s:
    case Range(hi, lo):
      return hi - lo
    case Empty:
      return 0

fn main():
  echo width(Span.Range(lo: 2, hi: 10))
""", check="reject", needle="MATCH_FIELD_NAME_MISMATCH"),
    Case("D2", "control", "match consume accepts an owned temporary",
         """enum Msg:
  Text(body: String)
  Quit

fn make() -> Msg:
  return Msg.Text(body: "hi")

fn main():
  match consume make():
    case Text(body):
      let owned = body
      echo owned
    case Quit:
      echo "quit"
""", output="hi"),
    Case("D3", "control", "READ borrow spans the whole match",
         PHASE + """
domain Machine:
  phase: Phase = Phase.Idle

  fn Step():
    match phase:
      case Idle:
        phase = Phase.Running(job: 1)
      case Running(job):
        echo job
      case Done(job):
        echo job

fn main():
  machine = Machine()
  message machine.Step()
""", check="reject", needle="MATCH_READ_BORROW_ACTIVE"),
    Case("D4", "control", "replace transfers old domain payload into a new state",
         """enum Phase:
  Idle
  Running(job: String)
  Done(job: String)

domain Machine:
  phase = Phase.Running(job: "work")

  fn Step() -> Int:
    old = replace(phase, Phase.Idle)
    match consume old:
      case Idle:
        reply 0
      case Running(job):
        size = job.length()
        phase = Phase.Done(job: job)
        reply size
      case Done(job):
        phase = Phase.Done(job: job)
        reply 0

fn main():
  machine = Machine()
  echo message machine.Step()
""", output="4"),
    Case("D5", "control", "READ enum view crosses a message by-value boundary",
         """enum Result:
  Error(code: Int, message: String)
  Cancelled

domain Sink:
  fn Store(value: String):
    echo value

fn main():
  sink = Sink()
  let result = Result.Error(code: 1, message: "bad")
  match result:
    case Error(code, message):
      message sink.Store(message)
    case Cancelled:
      echo 0
""", output="bad"),
    Case("D5b", "control", "READ enum view crosses a reply by-value boundary",
         """enum Result:
  Error(code: Int, message: String)
  Cancelled

domain Source:
  fn Fetch(result: Result) -> String:
    match result:
      case Error(code, message):
        reply message
      case Cancelled:
        reply "cancelled"

fn main():
  source = Source()
  echo message source.Fetch(Result.Error(code: 2, message: "boom"))
""", output="boom"),
    Case("D6", "control", "match specializes an untyped parameter at concrete calls",
         """enum Job:
  Waiting
  Done(value: Int)

fn score(item) -> Int:
  match item:
    case Waiting:
      return 0
    case Done(value):
      return value

fn main():
  echo score(Job.Done(value: 5))
""", output="5"),
    Case("D7", "control", "a consumed var can be reinitialized before use",
         """enum Phase:
  Start
  Running(name: String)

fn main():
  var phase = Phase.Start
  match consume phase:
    case Start:
      phase = Phase.Running(name: "job")
    case Running(name):
      echo name
      phase = Phase.Start
  match phase:
    case Start:
      echo "start"
    case Running(name):
      echo name
""", output="job"),
    Case("D8", "control", "enum payload field types are required",
         """enum Box:
  Num(value)
  Nothing

fn main():
  echo 0
""", check="reject", needle="requires a type"),
    Case("D9", "control", "legacy option[T] remains separate from enum match",
         """type Countdown:
  current: Int
  fn next() -> option[Int]:
    if current <= 0:
      return None
    value = current
    current = current - 1
    Some(value)

fn main():
  var counter = Countdown(current = 2)
  let item = counter.next()
  match item:
    case Some(value):
      echo value
    case None:
      echo 0
""", check="reject", needle="MATCH_REQUIRES_ENUM"),
    Case("D10", "control", "iterator contract uses lowercase option[T]",
         """type Countdown:
  current: Int
  fn next() -> Option[Int]:
    if current <= 0:
      return None
    value = current
    current = current - 1
    Some(value)

fn main():
  var counter = Countdown(current = 2)
  var total = 0
  for value in counter:
    total = total + value
  echo total
""", check="reject", needle="without an Option result"),
    Case("D11", "control", "pass is a no-op statement in a match arm",
         """enum Color:
  Red
  Blue

fn main():
  let c = Color.Red
  match c:
    case Red:
      pass
    case Blue:
      echo 2
""", output=""),
    Case("D12", "control", "tag-only enum declarations reject parentheses",
         """enum Box:
  Num(value: Int)
  Nothing()

fn main():
  let b = Box.Nothing
  match b:
    case Num(value):
      echo value
    case Nothing():
      echo 0
""", check="reject", needle="tag-only enum cases are declared without parentheses"),
    Case("D13", "control", "enum methods have a targeted unsupported diagnostic",
         """enum Job:
  Waiting
  Done(value: Int)

  fn score() -> Int:
    return 1

fn main():
  echo 0
""", check="reject", needle="enum methods are not supported"),
    Case("D2b", "control", "consuming match accepts a constructed enum rvalue",
         """enum Box:
  Full(value: String)
  Empty

fn main():
  match consume Box.Full(value: "fresh"):
    case Full(value):
      echo value
    case Empty:
      pass
""", output="fresh"),
    Case("D2c", "control", "consuming match rejects an interior enum place",
         """enum Box:
  Full(value: String)
  Empty

type Envelope:
  box: Box

fn main():
  let envelope = Envelope(box: Box.Empty)
  match consume envelope.box:
    case Full(value):
      echo value
    case Empty:
      pass
""", check="reject", needle="MATCH_CONSUME_INTERIOR_PLACE"),
    Case("D4a", "control", "local replace returns the old owned payload",
         """enum Box:
  Full(value: String)
  Empty

fn main():
  var current = Box.Full(value: "old")
  let previous = replace(current, Box.Empty)
  match consume previous:
    case Full(value):
      echo value
    case Empty:
      echo "missing"
  match current:
    case Full(value):
      echo value
    case Empty:
      echo "empty"
""", output="old\nempty"),
    Case("D4b", "control", "replace consumes a nontrivial replacement binding",
         """enum Box:
  Full(value: String)
  Empty

fn main():
  var current = Box.Empty
  let replacement = Box.Full(value: "new")
  let previous = replace(current, replacement)
  match replacement:
    case Full(value):
      echo value
    case Empty:
      pass
""", check="reject", needle="consumed"),
    Case("D4c", "control", "consuming match accepts replace of domain state",
         """enum Phase:
  Idle
  Running(job: String)

domain Machine:
  phase = Phase.Running(job: "work")

  fn Step() -> String:
    match consume replace(phase, Phase.Idle):
      case Idle:
        reply "idle"
      case Running(job):
        reply job

fn main():
  machine = Machine()
  echo message machine.Step()
""", output="work"),
    Case("D6b", "control", "incompatible non-enum specialization is rejected",
         """enum Status:
  Ready
  Failed(code: Int)

fn describe(value) -> Int:
  match value:
    case Ready:
      return 1
    case Failed(code):
      return code

fn main():
  echo describe(3)
""", check="reject", needle="found int"),
    Case("D7b", "control", "loop back edge accepts a reinitialized outer var",
         """enum Phase:
  Idle
  Running(job: String)

fn main():
  var value = Phase.Running(job: "work")
  var done = false
  while not done:
    let old = value
    value = Phase.Idle
    done = true
  match value:
    case Idle:
      echo "idle"
    case Running(job):
      echo job
""", output="idle"),
    Case("F11c", "control", "for loop rejects consumption without reinitialization",
         """enum Box:
  Full(value: String)
  Empty

fn main():
  let owned = Box.Full(value: "x")
  for index in range(0, 2):
    let old = owned
    echo index
""", check="reject", needle="LOOP_OUTER_BINDING_CONSUMED"),
    Case("F11d", "control", "for loop permits consumed var reinitialized before back edge",
         """enum Box:
  Full(value: String)
  Empty

fn main():
  var owned = Box.Full(value: "x")
  for index in range(0, 2):
    let old = owned
    owned = Box.Empty
    echo index
""", check="native", output="0\n1"),
    Case("F11e", "control", "a loop return path has no ownership back edge",
         """enum State:
  Idle
  Busy(name: String)

fn run(stop: Bool) -> Int:
  var state = State.Busy(name: "x")
  var i = 0
  while i < 2:
    if stop:
      match consume state:
        case Idle:
          return 0
        case Busy(name):
          return name.length()
    i = i + 1
  return 2

fn main():
  echo run(false)
""", output="2"),
    Case("D11b", "control", "pass works in ordinary function and main blocks",
         """fn noop():
  pass

fn main():
  noop()
  pass
  echo 1
""", output="1"),
    Case("D12b", "control", "tag-only enum constructor rejects parentheses",
         """enum Box:
  Empty

fn main():
  let value = Box.Empty()
  echo 1
""", check="reject", needle="ENUM_TAG_CALL_UNSUPPORTED"),
    Case("D12c", "control", "tag-only enum pattern rejects parentheses",
         """enum Box:
  Empty

fn main():
  let value = Box.Empty
  match value:
    case Empty():
      echo 1
""", check="reject", needle="tag-only enum patterns are written without parentheses"),
    # ------------------------------------------------------------------ controls
    Case("C1", "control", "documented transition idiom: bind next in every arm",
         PHASE + """
domain Machine:
  phase: Phase = Phase.Idle

  fn Step():
    match phase:
      case Idle:
        next = Phase.Running(job: 1)
      case Running(job):
        next = Phase.Done(job: job)
      case Done(job):
        next = Phase.Idle
    phase = next

  fn Current() -> Int:
    match phase:
      case Idle:
        reply 0
      case Running(job):
        reply job
      case Done(job):
        reply 0 - job

fn main():
  machine = Machine()
  message machine.Step()
  echo message machine.Current()
  message machine.Step()
  echo message machine.Current()
  message machine.Step()
  echo message machine.Current()
""", output="1\n-1\n0"),
    Case("C2", "control", "F4 workaround: bind the view-derived value first",
         """enum Result:
  Error(code: Int, message: String)
  Cancelled

domain Source:
  fn Fetch(result: Result) -> Int:
    match result:
      case Error(code, message):
        let n = message.length()
        reply n
      case Cancelled:
        reply 0

fn main():
  source = Source()
  echo message source.Fetch(Result.Error(code: 2, message: "boom"))
""", output="4"),
    Case("C3", "control", "qualified payload construction across modules",
         check="module_run", output="9\n1", files={
             "src/tags.moss": """module tags

export enum Signal:
  Off
  On(value: Int)
""",
             "src/main.moss": """module app
import tags

fn main():
  let signal = tags.Signal.On(value: 9)
  match signal:
    case Off:
      echo 0
    case On(value):
      echo value
  let off = tags.Signal.Off
  match consume off:
    case Off:
      echo 1
    case On(value):
      echo value
""",
         }),
    Case("C4", "control", "a READ parameter forwards through message (contrast D5)",
         """domain Sink:
  fn Store(value: String):
    echo value

domain Front:
  domainroutes(sink: Sink)

  fn Relay(text: String):
    message sink.Store(text)

fn main():
  sink = Sink()
  front = Front(sink: sink)
  let s = "hello"
  message front.Relay(s)
  message front.Relay(s)
""", output="hello\nhello"),
    Case("C5", "control", "lock plan: match on state is a SHARED read",
         PHASE + """
domain Machine:
  phase: Phase = Phase.Idle

  fn Current() -> Int:
    match phase:
      case Idle:
        reply 0
      case Running(job):
        reply job
      case Done(job):
        reply 0 - job

  fn Start(job: Int):
    phase = Phase.Running(job: job)

fn main():
  machine = Machine()
  message machine.Start(4)
  echo message machine.Current()
""", check="plan", target="Machine"),
    Case("C6", "control", "user-defined Some/None enum is independent of option[T]",
         """enum MaybeInt:
  Some(value: Int)
  None

fn main():
  let found = MaybeInt.Some(value: 1)
  match found:
    case Some(value):
      echo value
    case None:
      echo "none"
  let missing = MaybeInt.None
  match missing:
    case Some(value):
      echo value
    case None:
      echo "none"
""", output="1\nnone"),
    Case("C7", "control", "nested enums with nested READ and consuming matches",
         """enum Inner:
  Leaf(text: String)
  Blank

enum Outer:
  Wrap(inner: Inner, tag: Int)
  Empty

fn main():
  let o = Outer.Wrap(inner: Inner.Leaf(text: "deep"), tag: 7)
  match o:
    case Wrap(inner, tag):
      match inner:
        case Leaf(text):
          echo text
        case Blank:
          echo "blank"
      echo tag
    case Empty:
      echo 0
  let owned = Outer.Wrap(inner: Inner.Leaf(text: "moved"), tag: 8)
  match consume owned:
    case Wrap(inner, tag):
      match consume inner:
        case Leaf(text):
          let t = text
          echo t
        case Blank:
          echo "blank"
    case Empty:
      echo 0
""", output="deep\n7\nmoved"),
]


class Runner:
    def __init__(self, compiler: Path):
        self.compiler = compiler

    def run(self, command, cwd=ROOT):
        try:
            return subprocess.run(command, cwd=cwd, text=True, capture_output=True,
                                  timeout=TIMEOUT, check=False)
        except subprocess.TimeoutExpired:
            return subprocess.CompletedProcess(command, 124, "", "timeout")

    def check_json(self, source: Path):
        result = self.run([str(self.compiler), "check", str(source), "--json"])
        try:
            envelope = json.loads(result.stdout)
        except json.JSONDecodeError:
            return False, (result.stdout + result.stderr).strip()
        if envelope.get("ok"):
            return True, ""
        error = envelope.get("error") or {}
        return False, f"{error.get('code')}: {error.get('message')}"

    def native(self, source: Path, work: Path):
        rust = work / (source.stem + ".rs")
        generated = self.run([str(self.compiler), str(source), "-o", str(rust)])
        if generated.returncode:
            return None, "generation failed: " + (generated.stdout + generated.stderr).strip()
        binary = work / source.stem
        built = self.run(["rustc", "-D", "warnings", str(rust), "-o", str(binary)])
        if built.returncode:
            first = next((line for line in built.stderr.splitlines()
                          if line.startswith("error")), built.stderr.strip())
            return None, "rustc -D warnings failed: " + first
        executed = self.run([str(binary)])
        if executed.returncode:
            return None, f"native exit {executed.returncode}: {executed.stderr.strip()}"
        return executed.stdout.strip(), ""

    def fast_debug(self, source: Path):
        executed = self.run([str(self.compiler), "run", "--interp", str(source)])
        if executed.returncode:
            return None, "Fast Debug failed: " + executed.stderr.strip()
        return executed.stdout.strip(), ""

    def evaluate(self, case: Case):
        work = SCRATCH / case.case_id
        shutil.rmtree(work, ignore_errors=True)
        work.mkdir(parents=True)
        if case.check == "module_run":
            return self.module_run(case, work)
        source = work / f"{case.case_id.lower()}.moss"
        source.write_text(case.source, encoding="utf-8")
        accepted, diagnostic = self.check_json(source)

        if case.check == "reject":
            if accepted:
                return False, "checker accepted"
            if case.needle and case.needle not in diagnostic:
                return False, f"rejected with a different diagnostic: {diagnostic}"
            return True, diagnostic
        if case.check in ("run", "native", "plan", "effects") and not accepted:
            return False, f"checker rejected: {diagnostic}"
        if case.check == "effects":
            return self.effects(case, source)
        if case.check == "plan":
            return self.plan(case, source)
        if case.check == "consistent" and not accepted:
            return True, f"rejected consistently: {diagnostic}"

        native_output, native_error = self.native(source, work)
        if case.check == "native":
            if native_error:
                return False, native_error
            return native_output == case.output, (
                "native output matches" if native_output == case.output else
                f"native printed {native_output!r}, expected {case.output!r}")
        debug_output, debug_error = self.fast_debug(source)
        if case.check == "consistent":
            if native_error or debug_error:
                return False, "; ".join(filter(None, [native_error, debug_error]))
            if native_output != debug_output:
                return False, f"native {native_output!r} != Fast Debug {debug_output!r}"
            return True, "engines agree"
        problems = []
        if native_error:
            problems.append(native_error)
        elif native_output != case.output:
            problems.append(f"native printed {native_output!r}, expected {case.output!r}")
        if debug_error:
            problems.append(debug_error)
        elif debug_output != case.output:
            problems.append(f"Fast Debug printed {debug_output!r}, expected {case.output!r}")
        return not problems, "; ".join(problems) or "native and Fast Debug match"

    def effects(self, case: Case, source: Path):
        result = self.run([str(self.compiler), "effects", case.target,
                           "--source", str(source), "--json"])
        try:
            observed = json.loads(result.stdout)["result"]["observable_effects"]
        except (json.JSONDecodeError, KeyError, TypeError):
            return False, "effects query failed: " + (result.stdout + result.stderr)[:300]
        key, expected = case.effect
        if observed.get(key) != expected:
            return False, f"{case.target} {key}={observed.get(key)!r}, expected {expected!r}"
        return True, f"{case.target} {key}={expected!r}"

    def plan(self, case: Case, source: Path):
        result = self.run([str(self.compiler), "inspect", case.target,
                           "--source", str(source), "--json"])
        try:
            domains = json.loads(result.stdout)["result"]["synchronization_plan"]["domains"]
        except (json.JSONDecodeError, KeyError, TypeError):
            return False, "inspect query failed: " + (result.stdout + result.stderr)[:300]
        handlers = {handler["name"]: handler for handler in domains[0]["handlers"]}
        current, start = handlers.get("Current"), handlers.get("Start")
        if not current or "phase" not in current["read_set"] or \
                set(current["class_modes"].values()) != {"SHARED"}:
            return False, f"Current plan lacks a SHARED read of phase: {current}"
        if not start or "phase" not in start["write_set"] or \
                set(start["class_modes"].values()) != {"EXCLUSIVE"}:
            return False, f"Start plan lacks an EXCLUSIVE write of phase: {start}"
        return True, "Current SHARED read, Start EXCLUSIVE write"

    def module_run(self, case: Case, work: Path):
        project = work / "project"
        (project / "src").mkdir(parents=True)
        (project / "Moss.toml").write_text(
            '[project]\nname = "express005-followup"\nversion = "0.1.0"\n\n'
            '[build]\nsource = "src"\n', encoding="utf-8")
        for name, text in case.files.items():
            (project / name).write_text(text, encoding="utf-8")
        environment = dict(os.environ, MOSS=str(self.compiler))
        problems = []
        for mode in ("run", "debug"):
            result = subprocess.run([str(ROOT / "margo"), mode], cwd=project, text=True,
                                    capture_output=True, timeout=TIMEOUT,
                                    env=environment, check=False)
            if result.returncode or result.stdout.strip() != case.output:
                problems.append(f"margo {mode}: exit {result.returncode}, "
                                f"printed {result.stdout.strip()[:200]!r}")
        return not problems, "; ".join(problems) or "margo run and debug match"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("compiler", nargs="?", default=str(ROOT / "moss"))
    parser.add_argument("--expect-fixed", action="store_true",
                        help="require bug cases to pass")
    parser.add_argument("--only", default="", help="comma-separated case ids")
    parser.add_argument("--list", action="store_true", help="print the registry")
    arguments = parser.parse_args()

    selected = [case for case in CASES
                if not arguments.only or case.case_id in arguments.only.split(",")]
    if arguments.list:
        for case in selected:
            print(f"{case.case_id:5} {case.kind:8} {case.title}")
        return 0
    compiler = Path(arguments.compiler)
    if not compiler.is_absolute():
        compiler = (ROOT / compiler).resolve()
    if not compiler.exists():
        print(f"Moss compiler not found: {compiler}", file=sys.stderr)
        return 1
    if shutil.which("rustc") is None:
        print("rustc is required for native checks", file=sys.stderr)
        return 1

    runner = Runner(compiler)
    with ThreadPoolExecutor(max_workers=max(1, min(4, os.cpu_count() or 1))) as pool:
        results = list(pool.map(runner.evaluate, selected))

    failures = 0
    counts = {}
    for case, (passed, detail) in zip(selected, results):
        if case.kind == "bug" and not arguments.expect_fixed:
            status = "xfail" if not passed else "XPASS"
            failed = passed
        else:
            status = "ok" if passed else "FAIL"
            failed = not passed
        failures += failed
        counts[status] = counts.get(status, 0) + 1
        print(f"{status:5} {case.case_id:5} {case.title}")
        if failed or status == "xfail":
            print(f"      {detail}")
    summary = ", ".join(f"{count} {status}" for status, count in sorted(counts.items()))
    print(f"EXPRESS-005 follow-up cases: {summary}")
    if counts.get("XPASS"):
        print("XPASS: a known bug no longer reproduces. Promote the case to a "
              "permanent regression, delete it here, and mark the finding fixed "
              "in docs/PHASE_15_15_EXPRESS_005_REVIEW.md.", file=sys.stderr)
    if counts.get("FAIL"):
        print("FAIL: a case does not meet its expectation. For an open or control "
              "case, update the case and the review together; with --expect-fixed, "
              "bug cases must pass as well.", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
