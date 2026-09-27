#!/usr/bin/env python3
"""Focused EXPRESS-005 rejection matrix using the repository compiler."""

import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / "tmp" / "express005_negative"
HEADER = """enum Result:
  Ok(value: Int)
  Error(code: Int, message: String)
  Cancelled

enum Other:
  Different
"""

CASES = {
    "non_exhaustive": (
        """fn main():
  let result = Result.Ok(value: 1)
  match result:
    case Ok(value):
      echo value
""",
        "NON_EXHAUSTIVE_MATCH",
    ),
    "duplicate_case": (
        """fn main():
  let result = Result.Cancelled
  match result:
    case Ok(value):
      echo value
    case Ok(value):
      echo value
    case Error(code, message):
      echo code
    case Cancelled:
      echo 0
""",
        "DUPLICATE_MATCH_CASE",
    ),
    "unknown_case": (
        """fn main():
  let result = Result.Cancelled
  match result:
    case Missing:
      echo 0
""",
        "UNKNOWN_ENUM_CASE",
    ),
    "wrong_enum_case": (
        """fn main():
  let result = Result.Cancelled
  match result:
    case Different:
      echo 0
""",
        "WRONG_ENUM_CASE",
    ),
    "missing_field": (
        """fn main():
  let result = Result.Error(code: 1)
  echo result
""",
        "MISSING_ENUM_FIELD",
    ),
    "extra_field": (
        """fn main():
  let result = Result.Ok(value: 1, extra: 2)
  echo result
""",
        "UNKNOWN_ENUM_FIELD",
    ),
    "duplicate_field": (
        """fn main():
  let result = Result.Ok(value: 1, value: 2)
  echo result
""",
        "DUPLICATE_ENUM_FIELD",
    ),
    "wrong_field_type": (
        """fn main():
  let result = Result.Ok(value: "bad")
  echo result
""",
        "ENUM_FIELD_TYPE_MISMATCH",
    ),
    "enum_equality": (
        """fn main():
  let left = Result.Cancelled
  let right = Result.Cancelled
  echo left == right
""",
        "ENUM_OPERATOR_UNSUPPORTED",
    ),
    "unknown_constructor_case": (
        """fn main():
  let result = Result.Missing
  echo result
""",
        "UNKNOWN_ENUM_CASE",
    ),
    "missing_object_enum_field": (
        """type Holder:
  value: Result

fn main():
  let holder = Holder()
  echo holder
""",
        "ENUM_FIELD_INITIALIZER_REQUIRED",
    ),
    "read_payload_consume": (
        """fn main():
  let result = Result.Error(code: 1, message: "bad")
  match result:
    case Ok(value):
      echo value
    case Error(code, message):
      let escaped = message
      echo escaped
    case Cancelled:
      echo 0
""",
        "BORROWED_ENUM_PAYLOAD_CONSUME",
    ),
    "read_payload_return": (
        """fn leak(result: Result) -> String:
  match result:
    case Ok(value):
      return "ok"
    case Error(code, message):
      return message
    case Cancelled:
      return "cancelled"

fn main():
  echo leak(Result.Cancelled)
""",
        "BORROWED_ENUM_PAYLOAD_ACCESS",
    ),
    "read_payload_message": (
        """domain Sink:
  fn Store(value: String):
    echo value

fn main():
  sink = Sink()
  let result = Result.Error(code: 1, message: "bad")
  match result:
    case Ok(value):
      echo value
    case Error(code, message):
      message sink.Store(message)
    case Cancelled:
      echo 0
""",
        "BORROWED_ENUM_PAYLOAD_ESCAPE",
    ),
    "read_payload_reply": (
        """domain Source:
  fn Fetch(result: Result) -> String:
    match result:
      case Ok(value):
        reply "ok"
      case Error(code, message):
        reply message
      case Cancelled:
        reply "cancelled"

fn main():
  source = Source()
  echo message source.Fetch(Result.Cancelled)
""",
        "BORROWED_ENUM_PAYLOAD_ESCAPE",
    ),
    "read_payload_mutation": (
        """enum Bucket:
  Items(values: Vector[Int])
  Empty

fn main():
  let bucket = Bucket.Items(values: [1])
  match bucket:
    case Items(values):
      values.push(2)
    case Empty:
      echo 0
""",
        "BORROWED_ENUM_PAYLOAD_ACCESS",
    ),
    "read_payload_store": (
        """type Holder:
  value: String

fn main():
  let result = Result.Error(code: 1, message: "bad")
  match result:
    case Ok(value):
      echo value
    case Error(code, message):
      let holder = Holder(value: message)
      echo holder.value
    case Cancelled:
      echo 0
""",
        "BORROWED_ENUM_PAYLOAD_ACCESS",
    ),
    "handler_payload_consume": (
        """domain Worker:
  fn Handle(result: Result):
    match consume result:
      case Ok(value):
        echo value
      case Error(code, message):
        echo message
      case Cancelled:
        echo 0

fn main():
  worker = Worker()
  message worker.Handle(Result.Cancelled)
""",
        "cannot CONSUME incoming message payload",
    ),
    "scrutinee_after_consume": (
        """fn main():
  let result = Result.Error(code: 1, message: "bad")
  match consume result:
    case Ok(value):
      echo value
    case Error(code, message):
      echo message
    case Cancelled:
      echo 0
  let used = result
  echo 0
""",
        "consumed",
    ),
    "move_pattern": (
        """fn main():
  let result = Result.Ok(value: 1)
  match result:
    case Ok(move value):
      echo value
""",
        "ownership modifiers are not allowed",
    ),
    "ref_pattern": (
        """fn main():
  let result = Result.Ok(value: 1)
  match result:
    case Ok(ref value):
      echo value
""",
        "ownership modifiers are not allowed",
    ),
    "wildcard": (
        """fn main():
  let result = Result.Cancelled
  match result:
    case _:
      echo 0
""",
        "wildcard enum patterns are unsupported",
    ),
    "match_expression": (
        """fn main():
  let result = Result.Cancelled
  let answer = match result:
  echo answer
""",
        "MATCH_EXPRESSION_UNSUPPORTED",
    ),
    "guard": (
        """fn main():
  let result = Result.Ok(value: 1)
  match result:
    case Ok(value) if value > 0:
      echo value
""",
        "enum patterns support only",
    ),
    "nested_pattern": (
        """fn main():
  let result = Result.Ok(value: 1)
  match result:
    case Ok(Some(value)):
      echo value
""",
        "enum patterns support only",
    ),
    "recursive_enum": (
        """enum Loop:
  Next(value: Loop)

fn main():
  echo 0
""",
        "recursive enum or aggregate layout",
    ),
    "indirect_recursive_enum": (
        """type Wrapper:
  value: Loop

enum Loop:
  Next(value: Wrapper)

fn main():
  echo 0
""",
        "recursive enum or aggregate layout",
    ),
    "collection_recursive_enum": (
        """enum Loop:
  Next(values: Vector[Loop])

fn main():
  echo 0
""",
        "recursive enum or aggregate layout",
    ),
}


def main():
    bootstrap = subprocess.run(
        [str(ROOT / "moss"), "agent", "bootstrap", "--json"],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=True,
    )
    surface = json.loads(bootstrap.stdout)["result"]["source_surface"]
    if surface["enums"]["consume_match"] != "match consume value:":
        print("bootstrap omitted enum ownership surface", file=sys.stderr)
        return 1
    SCRATCH.mkdir(parents=True, exist_ok=True)
    failures = []
    for name, (body, expected) in CASES.items():
        source = SCRATCH / f"{name}.moss"
        source.write_text(HEADER + body)
        result = subprocess.run(
            [str(ROOT / "moss"), "check", str(source), "--json"],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        try:
            envelope = json.loads(result.stdout)
            details = json.dumps(envelope)
        except json.JSONDecodeError:
            details = result.stdout + result.stderr
        if result.returncode == 0 or expected not in details:
            failures.append(f"{name}: expected {expected}; got {details}")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"{len(CASES)} enum rejection cases passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
