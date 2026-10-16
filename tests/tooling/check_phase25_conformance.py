#!/usr/bin/env python3
"""Executable Phase 25 G2 gates. Each gate must prove its Moss contract.

The G0 Rust feasibility prototype is deliberately not an acceptance gate: it
cannot establish compiled-only Moss provider/consumer behavior.
"""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "moss"
MARGO = ROOT / "margo"
FIX = ROOT / "tests/phase25/g0/conformance"
G2 = ROOT / "tests/phase25/g2/conformance"


def run(*args, cwd=ROOT, env=None):
    return subprocess.run([str(x) for x in args], cwd=cwd, env=env,
                          text=True, capture_output=True, timeout=180)


def payload(result):
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise AssertionError(f"no structured Moss result: {result.stdout}\n{result.stderr}") from exc


def check_positive(source):
    result = run(MOSS, "check", source, "--json")
    data = payload(result)
    assert result.returncode == 0 and data.get("ok"), (source, data)


def check_negative(source, codes, required=(), env=None):
    result = run(MOSS, "check", source, "--json", env=env)
    data = payload(result)
    assert result.returncode != 0 and data.get("ok") is False, (source, data)
    error = data["error"]
    assert error["code"] in codes, (source, error)
    for fragment in required:
        assert fragment in json.dumps(error), (source, fragment, error)
    return error


def native(source, scratch):
    rust = scratch / (source.stem + ".rs")
    binary = scratch / (source.stem + ".bin")
    lowered = run(MOSS, source, "-o", rust)
    assert lowered.returncode == 0, (source, lowered.stdout, lowered.stderr)
    compiled = run("rustc", rust, "-o", binary)
    assert compiled.returncode == 0, (source, compiled.stdout, compiled.stderr)
    executed = run(binary)
    assert executed.returncode == 0, (source, executed.stdout, executed.stderr)
    return executed.stdout


def parity(source, scratch, expected):
    check_positive(source)
    interpreted = run(MOSS, "run", "--interp", source)
    assert interpreted.returncode == 0, (source, interpreted.stdout, interpreted.stderr)
    actual = native(source, scratch)
    assert actual == interpreted.stdout == expected, (source, actual, interpreted.stdout, expected)


def cases(**named):
    failures = []
    for name, test in named.items():
        try:
            test()
        except Exception as exc:
            failures.append(f"{name}: {str(exc)[:350]}")
    assert not failures, "; ".join(failures)


def write_package(root, name, source):
    (root / "src").mkdir(parents=True)
    (root / "Moss.toml").write_text(
        f'[project]\nname = "{name}"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n')
    (root / "src" / ("provider.moss" if name == "g2provider" else "main.moss")).write_text(source)


PROVIDER_OPEN = '''module Provider
export fn fresh():
  return []

export fn relay():
  return fresh()
'''

PROVIDER_METHOD = '''module Provider
export fn outer(x):
  return middle(x)

fn middle(x):
  return x.required()
'''

PROVIDER_BOX = '''module Provider
export type Box:
  value

export fn unbox(box: Box):
  return box.value
'''


def source_free(scratch, provider_source):
    provider = scratch / "provider"
    write_package(provider, "g2provider", provider_source)
    built = run(MARGO, "build", "--json", cwd=provider)
    assert built.returncode == 0 and payload(built).get("ok"), (built.stdout, built.stderr)
    artifacts = scratch / "artifacts"
    artifacts.mkdir()
    files = list((provider / "build/debug").glob("*.mossi")) + list((provider / "build/debug").glob("*.rlib"))
    assert files, "provider build emitted no semantic interface/native artifacts"
    interfaces = [file for file in files if file.suffix == ".mossi"]
    assert interfaces and all(file.read_bytes().startswith(b"moss-module-interface-v3\n")
                              for file in interfaces), "provider did not emit ABI-v10 .mossi"
    for file in files:
        shutil.copy2(file, artifacts / file.name)
    shutil.rmtree(provider / "src")
    assert not (provider / "src").exists(), "provider source directory still reachable"
    env = os.environ.copy()
    env["MOSS_MODULE_PATH"] = str(artifacts)
    return env, artifacts


def consumer(scratch, name, source):
    root = scratch / name
    write_package(root, name, source)
    return root, root / "src/main.moss"


def source_free_run(root, env, expected):
    built = run(MARGO, "build", "--json", cwd=root, env=env)
    data = payload(built)
    assert built.returncode == 0 and data.get("ok"), (root, data, built.stderr)
    executable = data["result"]["artifacts"]["executable"]
    executed = run(executable, cwd=root, env=env)
    assert executed.returncode == 0 and executed.stdout == expected, (root, executed.stdout, expected, executed.stderr)
    return data


def g1(s):
    parity(FIX / "nominal_instance_coexistence.moss", s, "1\nhello\n")
    check_negative(FIX / "nominal_inequality_same_layout.moss", {"TYPE_MISMATCH"})


def g2(s):
    cases(nested_constructor=lambda: parity(FIX / "nested_nominal_constructor.moss", s, "1\n2\n"),
          zero_iteration=lambda: parity(FIX / "open_result_zero_iteration.moss", s, "0\n"),
          recursive_parser=lambda: check_positive(G2 / "nested_type_expression.moss"),
          malformed_nested=lambda: check_negative(G2 / "malformed_nested_type_expression.moss",
                                                  {"MALFORMED_TYPE_EXPRESSION"}))


def g3(s):
    check_negative(FIX / "nominal_instance_assignment_conflict.moss", {"TYPE_MISMATCH"})
    check_negative(FIX / "open_result_conflict.moss", {"TYPE_MISMATCH"})
    parity(FIX / "nominal_instance_coexistence.moss", s, "1\nhello\n")


def g4(s):
    cases(source_multihop=lambda: parity(FIX / "open_result_multihop.moss", s, "1\none\n"),
          ambiguous_source=lambda: check_negative(FIX / "open_result_ambiguous.moss", {"TYPE_AMBIGUOUS_OPEN_RESULT"}),
          unpinned_map=lambda: check_negative(G2 / "unpinned_map.moss", {"TYPE_AMBIGUOUS_OPEN_RESULT"}),
          unpinned_vector=lambda: check_negative(G2 / "unpinned_vector.moss", {"TYPE_AMBIGUOUS_OPEN_RESULT"}),
          source_free=lambda: g4_source_free(s))


def g4_source_free(s):
    env, _ = source_free(s / "sf4", PROVIDER_OPEN)
    root, _ = consumer(s / "sf4", "g2fresh", '''module Consumer
import Provider
fn main():
  ints = Provider.fresh()
  names = Provider.relay()
  ints.push(1)
  names.push("one")
  echo ints[0]
  echo names[0]
''')
    source_free_run(root, env, "1\none\n")
    _, ambiguous = consumer(s / "sf4", "g2ambiguous", '''module Consumer
import Provider
fn main():
  echo Provider.relay()
''')
    check_negative(ambiguous, {"TYPE_AMBIGUOUS_OPEN_RESULT"}, ("Provider",), env)


def g5(s):
    env, _ = source_free(s / "sf5", PROVIDER_METHOD)
    _, source = consumer(s / "sf5", "g2missing", '''module Consumer
import Provider
fn main():
  echo Provider.outer(1)
''')
    check_negative(source, {"MEMBER_NOT_FOUND", "UNKNOWN_METHOD", "TYPE_MEMBER_NOT_FOUND", "TYPE_MISMATCH"},
                   ("outer", "middle", "required", "Provider"), env)


def g6(s):
    parity(G2 / "known_open_field.moss", s, "42\n")
    check_negative(FIX / "unconstrained_receiver_field_negative.moss", {"UNTYPED_FIELD_ACCESS"})


def g7(s):
    cases(overlap=lambda: check_negative(G2 / "generic_overlap.moss", {"OWNERSHIP_CONFLICTING_ACCESS"}),
          use_after_consume=lambda: check_negative(G2 / "generic_use_after_consume.moss", {"OWNERSHIP_USE_AFTER_CONSUME"}),
          map_get=lambda: check_map_get_consistency(s))


def check_map_get_consistency(s):
    source = G2 / "map_get_empty.moss"
    checked = run(MOSS, "check", source, "--json")
    data = payload(checked)
    if data.get("ok"):
        parity(source, s, "2\n")
    else:
        assert checked.returncode != 0, data
        assert data["error"]["code"] == "TYPE_AMBIGUOUS_OPEN_RESULT", data


def g8(s):
    check = run(sys.executable, ROOT / "tests/tooling/check_phase25_recursive_layout.py", MOSS)
    assert check.returncode == 0, (check.stdout, check.stderr)
    check_negative(G2 / "changing_specialization_key.moss",
                   {"TYPE_RECURSIVE_SPECIALIZATION", "SPECIALIZATION_RECURSION"}, ("grow",))


def g9(s):
    env, artifacts = source_free(s / "sf9", PROVIDER_BOX)
    outputs = []
    for name, value, expected in (("g2consumera", "1", "1\n"),
                                  ("g2consumerb", '"hello"', "hello\n")):
        root, _ = consumer(s / "sf9", name, f'''module Consumer
import Provider
fn main():
  box = Provider.Box(value: {value})
  echo Provider.unbox(box)
''')
        data = source_free_run(root, env, expected)
        outputs.append(data)
    # Both independent consumer artifacts must bind the provider artifact;
    # a prototype-only Rust type check does not satisfy this gate.
    assert len(outputs) == 2 and list(artifacts.glob("*.rlib"))


def g10(s):
    # The key protocol belongs to Team S. A compiled-only Moss provider is the
    # acceptance surface, not a synthetic Rust Hash/Eq implementation.
    env, _ = source_free(s / "sf10", PROVIDER_BOX)
    root, _ = consumer(s / "sf10", "g2keyconsumer", '''module Consumer
import Provider
type UserKey:
  id: Int
  fn isequal(other: UserKey) -> Bool:
    return id == other.id
  fn hash() -> Int:
    return id
fn main():
  var table = Map()
  table[UserKey(id: 1)] = Provider.Box(value: 7)
  echo table.get(UserKey(id: 1), Provider.Box(value: 0)).value
''')
    source_free_run(root, env, "7\n")


def g11(s):
    parity(FIX / "open_result_multihop.moss", s, "1\none\n")
    env, _ = source_free(s / "sf11", PROVIDER_OPEN)
    root, _ = consumer(s / "sf11", "g2debug", '''module Consumer
import Provider
fn main():
  ints = Provider.fresh()
  ints.push(1)
  echo ints[0]
''')
    source_free_run(root, env, "1\n")
    debug = run(MARGO, "debug", cwd=root, env=env)
    assert debug.returncode != 0, (debug.stdout, debug.stderr)
    assert "FAST_DEBUG_NATIVE_DEPENDENCY" in debug.stdout + debug.stderr, (debug.stdout, debug.stderr)


GATES = [g1, g2, g3, g4, g5, g6, g7, g8, g9, g10, g11]


def main():
    (ROOT / "tmp").mkdir(exist_ok=True)
    failures = []
    with tempfile.TemporaryDirectory(prefix="phase25-g2-", dir=ROOT / "tmp") as directory:
        scratch = Path(directory)
        for number, gate in enumerate(GATES, 1):
            workspace = scratch / f"gate-{number}"
            workspace.mkdir()
            try:
                gate(workspace)
            except Exception as exc:
                failures.append(number)
                print(f"G2 gate {number}: FAIL: {exc}")
            else:
                print(f"G2 gate {number}: PASS")
    if failures:
        print(f"G2 conformance incomplete: {len(failures)}/11 gates failed: {failures}")
        return 1
    print("G2 conformance: 11/11 actual Moss gates passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
