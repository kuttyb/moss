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

        conflict = source(scratch, "one_binding_conflict", '''fn main():
  a = []
  a.push(1)
  a.push("hello")
''')
        checked(conflict, "TYPE_MISMATCH")
        print("PASS one binding retains one monotype", flush=True)

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
''',
        })
        env = os.environ.copy()
        env["MOSS_STAGED_ABI"] = "v10"
        built = run(MARGO, "build", "--json", cwd=scratch / "provider", env=env)
        assert built.returncode == 0 and json.loads(built.stdout)["ok"], (
            built.stdout, built.stderr)
        artifacts = scratch / "provider" / "build" / "debug"
        (scratch / "provider" / "src" / "provider.moss").unlink()
        package(scratch / "consumer", "team_a_consumer", "0.1.0", {
            "main": '''module Main
import Provider
fn main():
  ints = Provider.fresh()
  words = Provider.fresh()
  ints.push(1)
  words.push("one")
  echo ints[0]
  echo words[0]
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
        assert executed.returncode == 0 and executed.stdout == "1\none\n", (
            executed.stdout, executed.stderr)
        print("PASS source-free generic calls freshen independently", flush=True)

    print("Phase 25 Team A compiler-path semantics: PASS")


if __name__ == "__main__":
    main()
