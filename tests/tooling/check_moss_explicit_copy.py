#!/usr/bin/env python3
"""Explicit-copy boundary diagnostics and lowering across native/Fast Debug."""
import json
from pathlib import Path
import subprocess
import sys

repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)


def run(args, expected=0):
    result = subprocess.run([str(arg) for arg in args], cwd=repo,
                            capture_output=True, text=True, timeout=120)
    assert result.returncode == expected, (
        args, result.returncode, result.stdout, result.stderr)
    return result


record = """type Payload:
  value: Int
"""
invalid = [
    ("vector_read", record + """fn main():
  values = [Payload(value: 9)]
  result = values[0]
  echo result.value
""", "values[0]", "Payload"),
    ("map_read", """fn main():
  values = Map()
  values[\"k\"] = [3, 4]
  result = values[\"k\"]
  echo result |> count
""", 'values["k"]', "vector[int]"),
    ("map_get_read", """fn main():
  values = Map()
  values[\"k\"] = [3, 4]
  fallback = [0]
  result = values.get(\"k\", fallback)
  echo result |> count
""", 'values.get("k", fallback)', "vector[int]"),
    ("payload_reply", record + """domain Worker:
  fn Echo(payload: Payload) -> Payload:
    reply payload

fn main():
  worker = Worker()
  result = message worker.Echo(Payload(value: 6))
  echo result.value
""", "payload", "Payload"),
    ("payload_field_reply", record + """type Envelope:
  payload: Payload

domain Worker:
  fn Echo(envelope: Envelope) -> Payload:
    reply envelope.payload

fn main():
  worker = Worker()
  result = message worker.Echo(Envelope(payload: Payload(value: 6)))
  echo result.value
""", "envelope.payload", "Payload"),
    ("state_reply", """domain Store:
  stash: Vector[Int]
  fn Snapshot() -> Vector[Int]:
    reply stash

fn main():
  store = Store(stash: [2, 5])
  value = message store.Snapshot()
  echo value |> count
""", "stash", "vector[int]"),
    ("reply_index_read", record + """domain Worker:
  fn Echo(values: Vector[Payload]) -> Payload:
    reply values[0]

fn main():
  worker = Worker()
  result = message worker.Echo([Payload(value: 6)])
  echo result.value
""", "values[0]", "Payload"),
    ("reply_map_get_read", record + """domain Worker:
  fn Echo(values: Map, fallback: Payload) -> Payload:
    reply values.get("k", fallback)

fn main():
  worker = Worker()
  values = Map()
  values["k"] = Payload(value: 6)
  result = message worker.Echo(values, Payload(value: 0))
  echo result.value
""", 'values.get("k", fallback)', "Payload"),
    ("consume_argument", record + """fn identity(value: Payload) -> Payload:
  return value

fn main():
  values = [Payload(value: 4)]
  result = identity(values[0])
  echo result.value
""", "values[0]", "Payload"),
    ("constructor_field", record + """type Box:
  value: Payload

fn main():
  values = [Payload(value: 4)]
  result = Box(value: values[0])
  echo result.value.value
""", "values[0]", "Payload"),
    ("collection_push", record + """fn main():
  values = [Payload(value: 4)]
  destination = Vector[Payload]()
  destination.push(values[0])
  echo destination |> count
""", "values[0]", "Payload"),
]

for name, source, expression, moss_type in invalid:
    path = out / f"{name}.moss"
    path.write_text(source)
    checked = run([compiler, "check", path, "--json"], expected=1)
    payload = json.loads(checked.stdout)
    error = payload["error"]
    assert error.get("code") == "IMPLICIT_COPY", (name, payload)
    message = error.get("message", "")
    assert expression in message and moss_type in message, (name, payload)
    assert "copy(" + expression + ")" in message or "cannot be copied" in message, (
        name, payload)
    for engine_args in ([compiler, path, "-o", out / f"{name}.rs"],
                        [compiler, "run", "--interp", path]):
        failed = run(engine_args, expected=1)
        combined = failed.stdout + failed.stderr
        assert "IMPLICIT_COPY" in combined or "implicit copy" in combined, (
            name, engine_args, combined)
    print(f"PASS reject {name}: IMPLICIT_COPY")

valid = [
    ("vector_copy", record + """fn main():
  values = [Payload(value: 9)]
  result = copy(values[0])
  echo result.value
""", "9"),
    ("view_backed_index_copy", record + """type Holder:
  items: Vector[Payload]

fn first(holder: Holder) -> Payload:
  return copy(holder.items[0])

fn main():
  holder = Holder(items: [Payload(value: 17)])
  result = first(holder)
  echo result.value
""", "17"),
    ("payload_index_reply_copy", record + """domain Worker:
  fn Echo(values: Vector[Payload]) -> Payload:
    reply copy(values[0])

fn main():
  worker = Worker()
  result = message worker.Echo([Payload(value: 18)])
  echo result.value
""", "18"),
    ("map_copy", """fn main():
  values = Map()
  values[\"k\"] = [3, 4]
  result = copy(values[\"k\"])
  echo result |> count
""", "2"),
    ("map_get_copy", """fn main():
  values = Map()
  values[\"k\"] = [3, 4]
  fallback = [0]
  result = copy(values.get(\"k\", fallback))
  echo result |> count
""", "2"),
    ("map_get_fallback_read", """fn main():
  values = Map()
  values[\"present\"] = [7]
  fallback = [5]
  result = copy(values.get(\"missing\", fallback))
  echo result[0], fallback[0]
""", "5 5"),
    ("payload_reply_copy", record + """domain Worker:
  fn Echo(payload: Payload) -> Payload:
    reply copy(payload)

fn main():
  worker = Worker()
  result = message worker.Echo(Payload(value: 6))
  echo result.value
""", "6"),
    ("payload_field_reply_copy", record + """type Envelope:
  payload: Payload

domain Worker:
  fn Echo(envelope: Envelope) -> Payload:
    reply copy(envelope.payload)

fn main():
  worker = Worker()
  result = message worker.Echo(Envelope(payload: Payload(value: 6)))
  echo result.value
""", "6"),
    ("state_reply_copy", """domain Store:
  stash: Vector[Int]
  fn Snapshot() -> Vector[Int]:
    reply copy(stash)

fn main():
  store = Store(stash: [2, 5])
  value = message store.Snapshot()
  echo value |> count
""", "2"),
    ("primitive_index", """fn main():
  values = [11]
  result = values[0]
  echo result
""", "11"),
    ("projection_copy", record + """fn inspect(value: Payload) -> Int:
  return value.value

fn main():
  values = [Payload(value: 13)]
  echo inspect(copy(values[0])), copy(values[0]).value
""", "13 13"),
    ("match_index_borrow", """enum Choice:
  Text(value: String)
  Empty

fn main():
  values = [Choice.Text(value: \"ok\")]
  match values[0]:
    case Text(value):
      echo value
    case Empty:
      echo \"empty\"
""", "ok"),
    ("primitive_map_get", """fn main():
  values = Map()
  values[\"k\"] = 12
  result = values.get(\"k\", 0)
  echo result
""", "12"),
]

for name, source, expected in valid:
    path = out / f"{name}.moss"
    rust = out / f"{name}.rs"
    binary = out / name
    path.write_text(source)
    run([compiler, "check", path, "--json"])
    run([compiler, path, "-o", rust])
    run(["rustc", "-D", "warnings", rust, "-o", binary])
    native = run([binary]).stdout.strip()
    debug = run([compiler, "run", "--interp", path]).stdout.strip()
    assert native == debug == expected, (name, expected, native, debug)
    if name == "view_backed_index_copy":
        generated = rust.read_text()
        start = generated.index("fn __moss_view_first(")
        end = generated.index("\n}", start)
        body = generated[start:end]
        assert body.count(".clone()") == 1, (name, body)
        assert "copy(holder.items[0])" in body, (name, body)
    if name == "match_index_borrow":
        generated = rust.read_text()
        user_main = generated[generated.index("fn main() {"):]
        assert "match &(values)[(0_i64) as usize]" in user_main and ".clone()" not in user_main, (
            name, generated[generated.index("Moss line 7:"):generated.index("Moss line 7:") + 160])
    if name == "primitive_map_get":
        generated = rust.read_text()
        lowered = next(line for line in generated.splitlines()
                       if "let mut result = (values).get(" in line)
        assert ".copied()" in lowered and ".cloned()" not in lowered, (
            name, lowered)
    print(f"PASS {name}: native = Fast Debug = {expected}")

print("PASS all explicit-copy directed checks")
