#!/usr/bin/env python3
"""Compiler-path conformance for Phase 25 Team A's type and identity work."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
MARGO = ROOT / "margo"
SCRATCH = ROOT / "tmp"
os.environ["TMPDIR"] = str(SCRATCH)


def run(*args, cwd=ROOT, env=None):
    result = subprocess.run([str(arg) for arg in args], cwd=cwd, env=env,
                            capture_output=True, text=True, timeout=180)
    return result


def checked(source, code=None, env=None):
    result = run(MOSS, "check", source, "--json", env=env)
    try:
        document = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise AssertionError((source, result.stdout, result.stderr)) from exc
    if code is None:
        assert result.returncode == 0 and document["ok"], (source, document)
    else:
        assert result.returncode != 0 and not document["ok"], (source, document)
        assert document["error"]["code"] == code, (source, document["error"])
    return document


def parity(source, scratch, expected):
    checked(source)
    interpreted = run(MOSS, "run", "--interp", source)
    assert interpreted.returncode == 0 and interpreted.stdout == expected, (
        source, interpreted.stdout, interpreted.stderr)
    rust = scratch / (source.stem + ".rs")
    lower = run(MOSS, source, "-o", rust)
    assert lower.returncode == 0, (source, lower.stdout, lower.stderr)
    for optimized in (False, True):
        binary = scratch / (source.stem + ("-release" if optimized else "-debug"))
        options = ["rustc", "-D", "warnings"]
        if optimized:
            options.append("-O")
        compiled = run(*options, rust, "-o", binary)
        assert compiled.returncode == 0, (source, compiled.stdout, compiled.stderr)
        executed = run(binary)
        assert executed.returncode == 0 and executed.stdout == expected, (
            source, binary, executed.stdout, executed.stderr)


def source(scratch, name, body):
    path = scratch / (name + ".moss")
    path.write_text(body, encoding="utf-8")
    return path


def package(path, name, version, modules):
    src = path / "src"
    src.mkdir(parents=True)
    (path / "Moss.toml").write_text(
        f'[project]\nname = "{name}"\nversion = "{version}"\n'
        '\n[build]\nsource = "src"\n', encoding="utf-8")
    for module, body in modules.items():
        (src / (module + ".moss")).write_text(body, encoding="utf-8")


def project_check(path, entry="main.moss", code=None, env=None):
    return checked(path / "src" / entry, code, env)


def main():
    SCRATCH.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="phase25-team-a-", dir=SCRATCH) as temp:
        scratch = Path(temp)

        # Each call to fresh() owns a new result variable. Assignment and later
        # use of one local binding must keep its single monotype.
        independent = source(scratch, "independent_calls", '''fn fresh():
  return []

fn main():
  a = fresh()
  b = fresh()
  a.push(1)
  b.push("hello")
  echo a[0]
  echo b[0]
''')
        parity(independent, scratch, "1\nhello\n")
        print("PASS independent open-result calls and three-engine parity", flush=True)

        callable_keys = source(scratch, "resolved_callable_keys", '''fn add_one(x: Int) -> Int:
  return x + 1

fn add_two(x: Int) -> Int:
  return x + 2

fn apply(f, x: Int) -> Int:
  return f(x)

fn main():
  echo apply(add_one, 2)
  echo apply(add_two, 2)
''')
        parity(callable_keys, scratch, "3\n4\n")
        inspected = run(MOSS, "inspect", "main", "--source", callable_keys,
                        "--json")
        assert inspected.returncode == 0, (inspected.stdout, inspected.stderr)
        result = json.loads(inspected.stdout)["result"]
        specializations = [entry for entry in
                           result["typed_semantic_v1"]["specializations"]
                           if entry["callable_id"].endswith(":fn:apply")]
        module_id = "module-v1:__standalone__@0.0.0::main"
        assert {entry["callable_arguments"][0] for entry in specializations} == {
            f"callable-v1:{module_id}:fn:add_one",
            f"callable-v1:{module_id}:fn:add_two"}, specializations
        assert len({entry["key"] for entry in specializations}) == 2, (
            specializations)
        edges = [edge for edge in result["direct_calls"]
                 if edge["target"] == "fn:apply"]
        assert {edge["specialization_key"] for edge in edges} == {
            entry["key"] for entry in specializations}, (edges, specializations)
        print("PASS callable arguments use resolved IDs in checked keys and edges",
              flush=True)

        same_line = source(scratch, "same_line_callable_sites", '''fn add_one(x: Int) -> Int:
  return x + 1

fn add_two(x: Int) -> Int:
  return x + 2

fn apply(f, x: Int) -> Int:
  return f(x)

fn main():
  echo apply(add_one, 2) + apply(add_two, 2)
  echo apply(add_one, 2) + apply(add_one, 3)
''')
        parity(same_line, scratch, "7\n7\n")
        calls = run(MOSS, "calls", "main", "--source", same_line, "--json")
        assert calls.returncode == 0, (calls.stdout, calls.stderr)
        edges = [edge for edge in json.loads(calls.stdout)["result"]["direct_calls"]
                 if edge["target"] == "fn:apply"]
        distinct = [edge for edge in edges if edge["line"] == 11]
        repeated = [edge for edge in edges if edge["line"] == 12]
        assert len(distinct) == len(repeated) == 2, edges
        assert len({edge["specialization_key"] for edge in distinct}) == 2, distinct
        assert len({edge["specialization_key"] for edge in repeated}) == 1, repeated
        for pair in (distinct, repeated):
            assert len({edge["call_site_semantic_identity"] for edge in pair}) == 2, pair
            assert len({edge["call_site_id"] for edge in pair}) == 2, pair
        selected = run(MOSS, "calls", distinct[1]["call_site_semantic_identity"],
                       "--source", same_line, "--json")
        assert selected.returncode == 0, (selected.stdout, selected.stderr)
        selected_edges = json.loads(selected.stdout)["result"]["direct_calls"]
        assert len(selected_edges) == 1 and selected_edges[0]["specialization_key"] == \
            distinct[1]["specialization_key"], selected_edges
        print("PASS same-line calls retain one exact identity per expression",
              flush=True)

        # Equal callable and argument identities must still diverge when the
        # checked result variables close to different concrete types.
        result_keys = source(scratch, "open_result_keys", '''fn make(seed):
  return []

fn main():
  a = make(0)
  b = make(0)
  a.push(1)
  b.push("hello")
  echo a[0]
  echo b[0]
''')
        parity(result_keys, scratch, "1\nhello\n")
        calls = run(MOSS, "calls", "main", "--source", result_keys, "--json")
        assert calls.returncode == 0, (calls.stdout, calls.stderr)
        result_edges = [edge for edge in json.loads(calls.stdout)["result"]["direct_calls"]
                        if edge["target"] == "fn:make"]
        result_keys_seen = [edge.get("specialization_key") for edge in result_edges]
        assert len(result_keys_seen) == 2 and all(result_keys_seen), result_edges
        assert len(set(result_keys_seen)) == 2, result_edges
        assert all(key.startswith("specialization-v2:sha256:")
                   for key in result_keys_seen), result_edges
        print("PASS checked open-result bindings select distinct query and native keys",
              flush=True)

        nested_arguments = source(scratch, "nested_argument_open_results", '''fn make(seed):
  return []

fn use_ints(a: Vector[Int]):
  echo a |> count

fn use_words(a: Vector[String]):
  echo a |> count

fn main():
  use_ints(make(0))
  use_words(make(0))
''')
        parity(nested_arguments, scratch, "0\n0\n")
        calls = run(MOSS, "calls", "main", "--source", nested_arguments, "--json")
        assert calls.returncode == 0, (calls.stdout, calls.stderr)
        nested_edges = [edge for edge in json.loads(calls.stdout)["result"]["direct_calls"]
                        if edge["target"] == "fn:make"]
        nested_keys = [edge.get("specialization_key") for edge in nested_edges]
        assert len(nested_keys) == 2 and all(nested_keys), nested_edges
        assert len(set(nested_keys)) == 2, nested_edges
        print("PASS nested argument results close to distinct exact keys across three engines",
              flush=True)

        repeated_nested = source(scratch, "same_line_nested_open_results", '''fn make(seed):
  return []

fn both(a: Vector[Int], b: Vector[String]):
  echo a |> count
  echo b |> count

fn main():
  both(make(0), make(0))
''')
        parity(repeated_nested, scratch, "0\n0\n")
        calls = run(MOSS, "calls", "main", "--source", repeated_nested, "--json")
        assert calls.returncode == 0, (calls.stdout, calls.stderr)
        repeated_edges = [edge for edge in json.loads(calls.stdout)["result"]["direct_calls"]
                          if edge["target"] == "fn:make"]
        assert len(repeated_edges) == 2, repeated_edges
        assert len({edge["specialization_key"] for edge in repeated_edges}) == 2, (
            repeated_edges)
        assert len({edge["call_site_id"] for edge in repeated_edges}) == 2, (
            repeated_edges)
        print("PASS repeated same-line open results retain distinct checked and native keys",
              flush=True)

        generic_caller = source(scratch, "generic_caller_result_keys", '''fn make(seed):
  return []

fn outer(item):
  values = make(0)
  values.push(item)
  return values

fn main():
  ints = outer(1)
  words = outer("hello")
  echo ints[0]
  echo words[0]
''')
        parity(generic_caller, scratch, "1\nhello\n")
        calls = run(MOSS, "calls", "outer", "--source", generic_caller, "--json")
        assert calls.returncode == 0, (calls.stdout, calls.stderr)
        bound_edges = [edge for edge in json.loads(calls.stdout)["result"]["direct_calls"]
                       if edge["target"] == "fn:make" and
                       edge.get("caller_specialization_key")]
        assert len(bound_edges) == 2, bound_edges
        assert len({edge["specialization_key"] for edge in bound_edges}) == 2, bound_edges
        assert len({edge["caller_specialization_key"] for edge in bound_edges}) == 2, bound_edges
        print("PASS generic caller contexts retain distinct checked call edges",
              flush=True)

        call_constraint = source(scratch, "open_call_constraint", '''fn use_ints(values: Vector[Int]):
  echo values |> count

fn main():
  items = []
  use_ints(items)
  echo items
''')
        checked(call_constraint)
        print("PASS concrete call parameter closes one open local", flush=True)

        call_conflict = source(scratch, "open_call_conflict", '''fn use_ints(values: Vector[Int]):
  echo values |> count

fn main():
  items = []
  use_ints(items)
  items.push("wrong")
''')
        checked(call_conflict, "TYPE_MISMATCH")
        print("PASS call and later push constrain the same local", flush=True)

        declared_call = source(scratch, "typed_declared_call", '''fn first(values: Vector[Int]) -> Int:
  return values[0]

fn main():
  var values = []
  values.push(9)
  echo first(values)
''')
        parity(declared_call, scratch, "9\n")
        declared_call_conflict = source(scratch, "typed_declared_call_conflict", '''fn first(values: Vector[Int]) -> Int:
  return values[0]

fn main():
  var values = []
  values.push("wrong")
  echo first(values)
''')
        checked(declared_call_conflict, "TYPE_MISMATCH")
        print("PASS concrete call arguments use bound collection terms", flush=True)

        member_constraint = source(scratch, "open_member_requirement", '''fn append_one(values):
  values.push(1)

fn main():
  var values = []
  append_one(values)
  echo values[0]
''')
        parity(member_constraint, scratch, "1\n")
        print("PASS generic member requirement closes open caller element", flush=True)

        member_conflict = source(scratch, "open_member_conflict", '''fn append_one(values):
  values.push(1)

fn main():
  var values = []
  append_one(values)
  values.push("wrong")
''')
        checked(member_conflict, "TYPE_MISMATCH")
        print("PASS generic member constraint rejects conflicting element", flush=True)

        nested_member = source(scratch, "nested_and_pipeline_member", '''fn length(value):
  return value.length()

fn consume(n: Int):
  echo n

fn main():
  consume(length("abc"))
  echo ["ab", "cde"] |> map(length) |> sum
''')
        parity(nested_member, scratch, "3\n5\n")
        inspected = run(MOSS, "inspect", "main", "--source", nested_member,
                        "--json")
        assert inspected.returncode == 0, (inspected.stdout, inspected.stderr)
        typed = json.loads(inspected.stdout)["result"]["typed_semantic_v1"]
        length_specs = [entry for entry in typed["specializations"]
                        if entry["callable_id"].endswith(":fn:length")]
        assert len(length_specs) == 2, length_specs
        assert {entry["member_requirements"][0]["origin"]["line"]
                for entry in length_specs} == {8, 9}, length_specs
        assert all(entry["member_requirements"][0]["receiver_access"] == "READ"
                   and entry["member_requirements"][0]["selected_callable_id"] ==
                   "builtin:string.length" for entry in length_specs), length_specs
        print("PASS nested and pipeline member requirements close in the solver", flush=True)

        trait_member = source(scratch, "distinct_trait_member_bindings", '''trait Costed:
  fn cost() -> Int

type Alpha:
  units: Int
  fn cost():
    return units

type Beta:
  units: Int
  fn cost():
    return units * 2

fn total(left: Costed, right: Costed):
  return left.cost() + right.cost()

fn main():
  echo total(Alpha(units: 3), Beta(units: 4))
''')
        parity(trait_member, scratch, "11\n")
        inspected = run(MOSS, "inspect", "main", "--source", trait_member,
                        "--json")
        assert inspected.returncode == 0, (inspected.stdout, inspected.stderr)
        typed = json.loads(inspected.stdout)["result"]["typed_semantic_v1"]
        total_specs = [entry for entry in typed["specializations"]
                       if entry["callable_id"].endswith(":fn:total")]
        assert len(total_specs) == 1, total_specs
        members = total_specs[0]["member_requirements"]
        assert [entry["selected_callable_id"] for entry in members] == [
            "method:Alpha.cost", "method:Beta.cost"], members
        assert members[0]["receiver"]["id"] != members[1]["receiver"]["id"], members
        bad_trait = source(scratch, "invalid_trait_member_binding",
            trait_member.read_text().replace("Beta(units: 4)", "4"))
        checked(bad_trait, "TYPE_MISMATCH")
        print("PASS each trait formal binds its own conforming nominal type", flush=True)

        relay = source(scratch, "transitive_open_result", '''fn fresh():
  return []

fn relay():
  return fresh()

fn main():
  a = relay()
  b = relay()
  a.push(1)
  b.push("hello")
  echo a[0]
  echo b[0]
''')
        parity(relay, scratch, "1\nhello\n")
        relay_calls = run(MOSS, "calls", "relay", "--source", relay, "--json")
        assert relay_calls.returncode == 0, (relay_calls.stdout, relay_calls.stderr)
        relay_edges = [edge for edge in json.loads(relay_calls.stdout)["result"]["direct_calls"]
                       if edge["target"] == "fn:fresh" and
                       edge.get("caller_specialization_key")]
        assert len(relay_edges) == 2, relay_edges
        assert len({edge["specialization_key"] for edge in relay_edges}) == 2, relay_edges
        print("PASS transitive open-result calls freshen independently", flush=True)

        nested_open = source(scratch, "nested_open_result", '''fn nested():
  return [[]]

fn main():
  values = nested()
  values.push([1])
  echo values[1][0]
''')
        parity(nested_open, scratch, "1\n")
        print("PASS nested open result closes across three engines", flush=True)

        nominal_open = source(scratch, "nominal_open_result", '''type Box:
  value

fn boxed():
  return Box(value: [])

fn main():
  var item = boxed()
  item = Box(value: [1])
  echo item.value[0]
''')
        parity(nominal_open, scratch, "1\n")
        print("PASS nominal open field closes across three engines", flush=True)

        conflict = source(scratch, "one_binding_conflict", '''fn main():
  a = []
  a.push(1)
  a.push("hello")
''')
        checked(conflict, "TYPE_MISMATCH")
        print("PASS one binding retains one monotype", flush=True)

        assigned_collection = source(scratch, "assigned_open_collection", '''fn main():
  var values = []
  values = [1]
  echo values[0]
''')
        parity(assigned_collection, scratch, "1\n")
        assigned_conflict = source(scratch, "assigned_collection_conflict", '''fn main():
  var values = []
  values = [1]
  values = ["wrong"]
''')
        checked(assigned_conflict, "TYPE_MISMATCH")
        print("PASS assignment projects one solved TypeRef", flush=True)

        moved_collection = source(scratch, "moved_collection_term", '''fn main():
  a = []
  b = a
  b.push(1)
  echo b[0]
''')
        parity(moved_collection, scratch, "1\n")
        moved_conflict = source(scratch, "moved_collection_conflict", '''fn main():
  a = []
  b = a
  b.push(1)
  b.push("wrong")
''')
        checked(moved_conflict, "TYPE_MISMATCH")
        print("PASS moved local retains its TypeRef across assignment", flush=True)

        # Checking a callee body from a caller that also names `column` and
        # `result` must install the callee's own retained terms. Otherwise a
        # caller Int contaminates the Column parameter and a caller Table
        # contaminates the Vector used by copy_ints during effect capture.
        nested_body_scopes = source(scratch, "nested_body_type_scopes", '''export type Column:
  value: Int

export type Table:
  columns: Vector[Column]

fn require_column(column: Column):
  echo column.value

fn copy_ints(values: Vector[Int]) -> Vector[Int]:
  var result = Vector[Int]()
  for value in values:
    result.push(value)
  return result

export fn add_column(column: Column) -> Table:
  require_column(column)
  var result = Table(columns: [column])
  ignored = copy_ints([1, 2])
  return result

fn csv_adapter(column: Int):
  ignored = add_column(Column(value: column))

fn main():
  csv_adapter(2)
''')
        parity(nested_body_scopes, scratch, "2\n")
        print("PASS nested call and effect walks use callee TypeRefs", flush=True)

        map_key_conflict = source(scratch, "one_map_key_binding", '''fn main():
  var values = Map()
  values[1] = "one"
  values["two"] = "two"
''')
        checked(map_key_conflict, "TYPE_MISMATCH")
        print("PASS indexed writes retain one Map key type", flush=True)

        inferred_map = source(scratch, "inferred_map_index", '''fn main():
  var values = Map()
  values[1] = "one"
  echo values[1]
''')
        parity(inferred_map, scratch, "one\n")
        print("PASS indexed Map write projects solved key/value types", flush=True)

        map_read_conflict = source(scratch, "one_map_key_read", '''fn main():
  var values = Map()
  values[1] = "one"
  echo values["wrong"]
''')
        checked(map_read_conflict, "TYPE_MISMATCH")
        print("PASS indexed reads use the retained Map key term", flush=True)

        generic_index = source(scratch, "generic_index_result", '''fn first(values):
  return values[0]

fn main():
  echo first([2, 3])
''')
        parity(generic_index, scratch, "2\n")
        indexed_inspect = run(MOSS, "inspect", "main", "--source",
                              generic_index, "--json")
        assert indexed_inspect.returncode == 0, (
            indexed_inspect.stdout, indexed_inspect.stderr)
        indexed_typed = json.loads(indexed_inspect.stdout)["result"][
            "typed_semantic_v1"]
        first_id = "callable-v1:module-v1:__standalone__@0.0.0::main:fn:first"
        first_summary = [item for item in indexed_typed["open_summaries"]
                         if item["callable_id"] == first_id]
        assert len(first_summary) == 1 and first_summary[0]["requirement_count"], (
            indexed_typed)
        print("PASS generic index result closes from typed requirement", flush=True)

        operator_result = source(scratch, "generic_operator_result", '''fn add_one(value):
  return value + 1

fn main():
  echo add_one(2)
  echo add_one(2.5)
''')
        parity(operator_result, scratch, "3\n3.5\n")
        operator_inspect = run(MOSS, "inspect", "main", "--source",
                               operator_result, "--json")
        assert operator_inspect.returncode == 0, (
            operator_inspect.stdout, operator_inspect.stderr)
        operator_typed = json.loads(operator_inspect.stdout)["result"][
            "typed_semantic_v1"]
        operator_id = (
            "callable-v1:module-v1:__standalone__@0.0.0::main:fn:add_one")
        operator_summaries = [item for item in operator_typed["open_summaries"]
                              if item["callable_id"] == operator_id]
        assert len(operator_summaries) == 1 and (
            operator_summaries[0]["requirement_count"] == 1), operator_typed
        print("PASS generic operator result closes from typed requirement", flush=True)

        bad_operator = source(scratch, "bad_generic_operator", '''fn add_one(value):
  return value + 1

fn main():
  echo add_one("oops")
''')
        checked(bad_operator, "TYPE_MISMATCH")
        print("PASS closed generic operator rejects invalid operand", flush=True)

        known = source(scratch, "known_nominal_field", '''type Box:
  value

fn unwrap(item: Box):
  return item.value

fn main():
  echo unwrap(Box(value: 42))
  echo unwrap(Box(value: "hello"))
''')
        parity(known, scratch, "42\nhello\n")
        calls = run(MOSS, "calls", "main", "--source", known, "--json")
        assert calls.returncode == 0, (calls.stdout, calls.stderr)
        edges = [edge for edge in json.loads(calls.stdout)["result"]["direct_calls"]
                 if edge["target"] == "fn:unwrap"]
        identities = [edge.get("specialization_identity") for edge in edges]
        assert len(identities) == 2 and all(identities) and len(set(identities)) == 2, edges
        print("PASS known nominal template field binds per call", flush=True)

        reordered = source(scratch, "constructor_order", '''type Pair:
  first
  second

fn main():
  var item = Pair(first: 1, second: "two")
  item = Pair(second: "three", first: 4)
  echo item.first
  echo item.second
''')
        parity(reordered, scratch, "4\nthree\n")
        print("PASS constructor argument order preserves instance identity", flush=True)

        nested = source(scratch, "nested_nominal_collection", '''type Box:
  value

fn main():
  items = [Box(value: [1]), Box(value: [2])]
  echo items[0].value[0]
  echo items[1].value[0]
''')
        parity(nested, scratch, "1\n2\n")
        print("PASS nested collection and nominal field closure", flush=True)

        typed_reads = source(scratch, "typed_field_and_index_reads", '''type Box:
  value

fn main():
  container_box = Box(value: [1])
  var field = [0]
  field = container_box.value
  var item = 0
  item = field[0]
  echo item
''')
        parity(typed_reads, scratch, "1\n")
        print("PASS local assignments use typed nominal field and index reads", flush=True)

        checked(ROOT / "examples" / "collections_and_methods.moss")
        print("PASS closed Vector argument satisfies bare Vector parameter", flush=True)

        field_write = source(scratch, "typed_field_write", '''type Box:
  value: Int

fn main():
  var item = Box(value: 1)
  item.value = 2
  echo item.value
''')
        parity(field_write, scratch, "2\n")
        field_conflict = source(scratch, "typed_field_write_conflict", '''type Box:
  value: Int

fn main():
  var item = Box(value: 1)
  item.value = "wrong"
''')
        checked(field_conflict, "TYPE_MISMATCH")
        print("PASS field writes use retained nominal field type", flush=True)

        open_field_write = source(scratch, "open_nominal_field_write", '''type Box:
  value

fn main():
  var item = Box(value: [])
  item.value = [1]
  echo item.value[0]
''')
        parity(open_field_write, scratch, "1\n")
        print("PASS nominal field write closes nested open collection", flush=True)

        replace_terms = source(scratch, "replace_index_terms", '''fn main():
  var values = [1]
  old = replace(values[0], 2)
  echo old
  echo values[0]
''')
        parity(replace_terms, scratch, "1\n2\n")
        replace_conflict = source(scratch, "replace_index_conflict", '''fn main():
  var values = [1]
  old = replace(values[0], "wrong")
''')
        checked(replace_conflict, "REPLACE_TYPE_MISMATCH")
        print("PASS replace checks retained indexed place type", flush=True)

        handler_reply = source(scratch, "typed_handler_reply", '''type Box:
  value: Int

domain Reader:
  fn Get() -> Int:
    var values = [Box(value: 7)]
    reply values[0].value

fn main():
  reader = Reader()
  echo message reader.Get()
''')
        parity(handler_reply, scratch, "7\n")
        print("PASS handler reply uses checked field/index result", flush=True)

        typed_message_assignment = source(scratch, "typed_message_assignment", '''domain Reader:
  fn Get() -> Int:
    reply 7

fn main():
  reader = Reader()
  var value = 0
  value = message reader.Get()
  echo value
''')
        parity(typed_message_assignment, scratch, "7\n")
        message_assignment_conflict = source(scratch, "typed_message_assignment_conflict", '''domain Reader:
  fn Get() -> Int:
    reply 7

fn main():
  reader = Reader()
  var value = "bad"
  value = message reader.Get()
''')
        checked(message_assignment_conflict, "TYPE_MISMATCH")
        print("PASS message results preserve the destination monotype", flush=True)

        typed_return_conflict = source(scratch, "typed_return_conflict", '''fn wrong(values: Vector[Int]) -> Vector[String]:
  return values

fn main():
  echo 1
''')
        result = run(MOSS, "check", typed_return_conflict, "--json")
        assert result.returncode != 0, result.stdout
        diagnostic = json.loads(result.stdout)["error"]
        assert diagnostic["code"] == "MOSS_COMPILE_ERROR" and (
            "returns 'vector[int]' but is annotated 'vector[string]'"
            in diagnostic["message"]), diagnostic
        reply_conflict = source(scratch, "typed_reply_conflict", '''domain Reader:
  fn Get() -> Vector[String]:
    var values = [1]
    reply values

fn main():
  reader = Reader()
  echo message reader.Get()
''')
        result = run(MOSS, "check", reply_conflict, "--json")
        assert result.returncode != 0, result.stdout
        diagnostic = json.loads(result.stdout)["error"]
        assert diagnostic["code"] == "TYPE_MISMATCH" and (
            "conflicting reply types" in diagnostic["message"]), diagnostic
        print("PASS return and reply reject incompatible bound terms", flush=True)

        zero_loop = source(scratch, "zero_iteration", '''fn fresh():
  return []

fn main():
  items = fresh()
  for item in items:
    echo item
  items.push(1)
  echo items[0]
''')
        parity(zero_loop, scratch, "1\n")
        print("PASS zero-iteration loop type constraints are static", flush=True)

        branch_join = source(scratch, "open_collection_branch_join", '''fn main():
  var values = []
  if true:
    values = [1]
  else:
    values = []
  echo values[0]
''')
        parity(branch_join, scratch, "1\n")
        nested_join = source(scratch, "nested_collection_branch_join", '''fn main():
  var values = []
  if true:
    values.push([1])
  else:
    values.push([2])
  echo values[0][0]
''')
        parity(nested_join, scratch, "1\n")
        incompatible_join = source(scratch, "incompatible_branch_join", '''fn main():
  var values = []
  if true:
    values = [1]
  else:
    values = ["wrong"]
''')
        checked(incompatible_join, "TYPE_MISMATCH")
        print("PASS open and nested collection branch joins use checked types", flush=True)

        range_loop = source(scratch, "range_loop", '''fn main():
  for index in range(0, 2):
    echo index
''')
        parity(range_loop, scratch, "0\n1\n")
        print("PASS range traversal agrees across engines", flush=True)

        different_instances = source(scratch, "different_instances", '''type Box:
  value

fn main():
  var item = Box(value: 1)
  item = Box(value: "hello")
''')
        checked(different_instances, "TYPE_MISMATCH")
        print("PASS distinct concrete instances reject reassignment", flush=True)

        same_layout = source(scratch, "same_layout", '''type Box:
  value

type Cell:
  value

fn main():
  var item = Box(value: 1)
  item = Cell(value: 1)
''')
        checked(same_layout, "TYPE_MISMATCH")
        print("PASS same-layout declarations retain distinct identity", flush=True)

        # A malformed built-in cannot close by dropping a missing type argument.
        for spelling in ("Map[Int]", "Vector[]"):
            malformed = source(scratch, "malformed_" + spelling.split("[")[0],
                               f'''fn main():
  values = {spelling}()
  echo values
''')
            result = run(MOSS, "check", malformed, "--json")
            document = json.loads(result.stdout)
            assert result.returncode != 0 and not document["ok"], (spelling, document)
            print(f"PASS malformed {spelling} fails in Moss check", flush=True)

        nested_open_argument = source(scratch, "nested_open_argument", '''fn id(value):
  return value

fn main():
  items = [[]]
  echo id(items)
''')
        checked(nested_open_argument, "TYPE_AMBIGUOUS_OPEN_RESULT")
        print("PASS nested open collection is not a concrete specialization", flush=True)

        package(scratch / "modules", "team_a_identity", "1.2.3", {
            "alpha": '''module Alpha
export type Token:
  value: Int
''',
            "beta": '''module Beta
export type Token:
  value: Int
''',
            "main": '''module Main
import Alpha
import Beta
fn main():
  var token = Alpha.Token(value: 1)
  token = Beta.Token(value: 1)
''',
        })
        project_check(scratch / "modules", code="TYPE_MISMATCH")
        print("PASS qualified same-layout declarations retain distinct identity", flush=True)

        # The existing G2 suite owns source-free ABI records. Exercise its
        # compiler-only generic call path here without parsing diagnostics.
        package(scratch / "provider", "team_a_provider", "0.1.0", {
            "provider": '''module Provider
export fn fresh():
  return []

export fn relay():
  return fresh()

export fn size(value):
  return value.length()
''',
            "alpha": '''module Alpha
export fn add_one(value: Int) -> Int:
  return value + 1
''',
            "beta": '''module Beta
export fn add_one(value: Int) -> Int:
  return value + 2
''',
        })
        env = os.environ.copy()
        env["MOSS_STAGED_ABI"] = "v10"
        built = run(MARGO, "build", "--json", cwd=scratch / "provider", env=env)
        assert built.returncode == 0 and json.loads(built.stdout)["ok"], (
            built.stdout, built.stderr)
        artifacts = scratch / "provider" / "build" / "debug"
        for provider_source in (scratch / "provider" / "src").glob("*.moss"):
            provider_source.unlink()
        package(scratch / "consumer", "team_a_consumer", "0.1.0", {
            "main": '''module Main
import Provider
fn use_ints(values: Vector[Int]):
  echo values |> count

fn main():
  ints = Provider.relay()
  words = Provider.relay()
  ints.push(1)
  words.push("one")
  echo ints[0]
  echo words[0]
  echo Provider.size("abc")
  use_ints(Provider.fresh())
''',
        })
        env["MOSS_MODULE_PATH"] = str(artifacts)
        project_check(scratch / "consumer", env=env)
        consumer = run(MARGO, "build", "--json", cwd=scratch / "consumer", env=env)
        assert consumer.returncode == 0, (consumer.stdout, consumer.stderr)
        data = json.loads(consumer.stdout)
        assert data["ok"], data
        executable = data["artifacts"]["artifacts"]["executable"]
        executed = run(executable, cwd=scratch / "consumer", env=env)
        assert executed.returncode == 0 and executed.stdout == "1\none\n3\n0\n", (
            executed.stdout, executed.stderr)
        print("PASS source-free generic calls freshen independently", flush=True)

        package(scratch / "callable_consumer", "team_a_callable_consumer",
                "0.1.0", {"main": '''module Main
import Alpha
import Beta

fn apply(f, value: Int) -> Int:
  return f(value)

fn main():
  echo apply(Alpha.add_one, 2)
  echo apply(Beta.add_one, 2)
'''})
        callable_path = scratch / "callable_consumer" / "src" / "main.moss"
        project_check(scratch / "callable_consumer", env=env)
        imported = run(MOSS, "inspect", "main", "--source", callable_path,
                       "--json", env=env)
        assert imported.returncode == 0, (imported.stdout, imported.stderr)
        result = json.loads(imported.stdout)["result"]
        specializations = [entry for entry in
                           result["typed_semantic_v1"]["specializations"]
                           if entry["callable_id"].endswith(":fn:apply")]
        provider_module = "module-v1:team_a_provider@0.1.0::"
        assert {entry["callable_arguments"][0] for entry in specializations} == {
            f"callable-v1:{provider_module}Alpha:fn:add_one",
            f"callable-v1:{provider_module}Beta:fn:add_one"}, specializations
        assert len({entry["key"] for entry in specializations}) == 2, (
            specializations)
        built_consumer = run(MARGO, "build", "--json",
                             cwd=scratch / "callable_consumer", env=env)
        assert built_consumer.returncode == 0, (
            built_consumer.stdout, built_consumer.stderr)
        built_data = json.loads(built_consumer.stdout)
        assert built_data["ok"], built_data
        executable = built_data["artifacts"]["artifacts"]["executable"]
        executed = run(executable, cwd=scratch / "callable_consumer", env=env)
        assert executed.returncode == 0 and executed.stdout == "3\n4\n", (
            executed.stdout, executed.stderr)
        print("PASS source-free provider callable IDs distinguish identical names",
              flush=True)

        package(scratch / "bad_consumer", "team_a_bad_consumer", "0.1.0", {
            "main": '''module Main
import Provider
fn main():
  echo Provider.size(42)
''',
        })
        rejected = project_check(scratch / "bad_consumer",
                                 code="TYPE_MISMATCH", env=env)
        related = rejected["error"].get("related", [])
        assert any(note["source"]["file"].endswith("Provider.mossi") and
                   "requires method 'length'" in note["message"]
                   for note in related), related
        print("PASS source-free member failure retains provider provenance",
              flush=True)

        # The imported generic body returns an owned Vector[Int] after
        # iterating a borrowed Range. Its old display `_generic:data` result
        # must not cause the consumer to bind the result back to Range.
        source_free_range = run(
            sys.executable,
            ROOT / "sdk/foundations/acceptance/validate_source_free.py",
            "--foundations-root", ROOT / "sdk/foundations",
            "--workdir", scratch / "source_free_range",
            "--moss", MOSS)
        assert source_free_range.returncode == 0, (
            source_free_range.stdout, source_free_range.stderr)
        range_data = json.loads(source_free_range.stdout)
        assert range_data["source_removed"] and (
            range_data["range_output"] == "329"), range_data
        print("PASS source-free generic Range return keeps owned Vector type",
              flush=True)

    print("Phase 25 Team A compiler-path semantics: PASS")


if __name__ == "__main__":
    main()
