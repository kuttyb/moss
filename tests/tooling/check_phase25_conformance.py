#!/usr/bin/env python3
"""Executable Phase 25 G2 gates. Each gate must prove its Moss contract.

The G0 Rust feasibility prototype is deliberately not an acceptance gate: it
cannot establish compiled-only Moss provider/consumer behavior.
"""

import json
import hashlib
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

# Production stays on ABI v9 until the joint Team S/Team G cutover; the
# source-free gates prove the staged ABI-v10 path (TEAM_G_ABI_V10_STAGING.md).
os.environ["MOSS_STAGED_ABI"] = "v10"


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


def check_negative_at(source, codes, line):
    error = check_negative(source, codes)
    assert error.get("line") == line, (source, error)
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

PROVIDER_FRESH = '''module Provider
export fn fresh():
  return []
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

export fn unbox(item: Box):
  return item.value
'''

PROVIDER_SIMPLE = '''module Provider
export fn answer() -> Int:
  return 42
'''

PROVIDER_KEY = '''module Provider
export type UserKey:
  id: Int

  fn isequal(other: UserKey) -> Bool:
    return id == other.id

  fn hash() -> Int:
    return id

export fn seed() -> Map[UserKey, Int]:
  var values = Map()
  values[UserKey(id: 1)] = 7
  return values

export fn lookup(values: Map[UserKey, Int], key: UserKey) -> Int:
  return values.get(key, 0)
'''


def source_free(scratch, provider_source):
    provider = scratch / "provider"
    write_package(provider, "g2provider", provider_source)
    built = run(MARGO, "build", "--json", cwd=provider)
    assert built.returncode == 0 and payload(built).get("ok"), (built.stdout, built.stderr)
    artifacts = scratch / "artifacts"
    artifacts.mkdir()
    # Publish each interface with its selected provider rlib. The build's
    # `libmoss_<Module>.rlib` is a rustc discovery alias that every consumer
    # build recreates from the selected rlib; it is not provider ABI data.
    interfaces = list((provider / "build/debug").glob("*.mossi"))
    files = interfaces + [file.with_name(f"lib{file.stem}.rlib") for file in interfaces]
    assert interfaces and all(file.exists() for file in files), \
        "provider build emitted no semantic interface/native artifacts"
    assert interfaces and all(
        file.read_bytes().startswith(b"moss-module-interface-v3\n")
        and b"\nschema_version 10\n" in file.read_bytes()
        and b"\nnative_abi 10\n" in file.read_bytes()
        for file in interfaces), "provider did not emit ABI-v10 .mossi"
    for file in files:
        shutil.copy2(file, artifacts / file.name)
    shutil.rmtree(provider / "src")
    assert not (provider / "src").exists(), "provider source directory still reachable"
    env = os.environ.copy()
    env["MOSS_MODULE_PATH"] = str(artifacts)
    env["MOSS_STAGED_ABI"] = "v10"
    return env, artifacts


def consumer(scratch, name, source):
    root = scratch / name
    write_package(root, name, source)
    return root, root / "src/main.moss"


def source_free_run(root, env, expected):
    built = run(MARGO, "build", "--json", cwd=root, env=env)
    data = payload(built)
    assert built.returncode == 0 and data.get("ok"), (root, data, built.stderr)
    executable = data["artifacts"]["artifacts"]["executable"]
    executed = run(executable, cwd=root, env=env)
    assert executed.returncode == 0 and executed.stdout == expected, (root, executed.stdout, expected, executed.stderr)
    return data


def artifact_error(source, artifacts, code):
    env = os.environ.copy()
    env["MOSS_MODULE_PATH"] = str(artifacts)
    error = check_negative(source, {code}, env=env)
    assert error.get("source_file"), error


def clone_artifacts(source, destination):
    shutil.copytree(source, destination)
    interfaces = list(destination.glob("*.mossi"))
    libraries = list(destination.glob("*.rlib"))
    assert len(interfaces) == len(libraries) == 1, (interfaces, libraries)
    return interfaces[0], libraries[0]


def replace_one(source, before, after):
    assert before in source, f"expected ABI-v10 record absent: {before!r}"
    return source.replace(before, after, 1)


def g9_records(s):
    env, pristine = source_free(s / "record-provider", PROVIDER_SIMPLE)
    root, consumer_source = consumer(s / "record-provider", "g2records", '''module Consumer
import Provider
fn main():
  echo Provider.answer()
''')
    source_free_run(root, env, "42\n")

    corrupt, _ = clone_artifacts(pristine, s / "corrupt")
    raw = corrupt.read_bytes()
    corrupt.write_bytes(raw[:-1] + bytes([raw[-1] ^ 1]))
    artifact_error(consumer_source, corrupt.parent, "SOURCE_FREE_CONTRACT_INVALID")

    v9, _ = clone_artifacts(pristine, s / "v9")
    v9.write_bytes(replace_one(v9.read_bytes(), b"moss-module-interface-v3\n",
                               b"moss-module-interface-v2\n"))
    artifact_error(consumer_source, v9.parent, "SOURCE_FREE_ABI_REBUILD_REQUIRED")

    forged, _ = clone_artifacts(pristine, s / "forged")
    original = forged.read_bytes()
    changed = replace_one(original, b"module_id module-v1:g2provider@0.1.0::Provider",
                          b"module_id module-v1:g2imposter@0.1.0::Provider")
    digest_line = next(line for line in changed.splitlines(keepends=True)
                       if line.startswith(b"provider_contract_digest sha256:"))
    changed = changed.replace(digest_line, b"", 1)
    digest = hashlib.sha256(changed).hexdigest().encode()
    forged.write_bytes(changed.replace(b"moss-module-interface-v3\n",
        b"moss-module-interface-v3\nprovider_contract_digest sha256:" + digest + b"\n", 1))
    artifact_error(consumer_source, forged.parent,
                   "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH")

    _, newer = source_free(s / "record-provider-new", PROVIDER_SIMPLE.replace("42", "43"))
    stale, stale_lib = clone_artifacts(pristine, s / "stale")
    newer_interface = next(newer.glob("*.mossi"))
    stale.write_bytes(newer_interface.read_bytes())
    assert stale_lib.exists()
    artifact_error(consumer_source, stale.parent,
                   "SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH")


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
          unpinned_map=lambda: check_negative_at(G2 / "unpinned_map.moss", {"ECHO_DISPLAY_UNSUPPORTED"}, 2),
          unpinned_vector=lambda: check_negative_at(G2 / "unpinned_vector.moss", {"ECHO_DISPLAY_UNSUPPORTED"}, 2),
          echo_local_vector=lambda: check_negative_at(G2 / "echo_local_unpinned_vector.moss", {"ECHO_DISPLAY_UNSUPPORTED"}, 3),
          echo_local_map=lambda: check_negative_at(G2 / "echo_local_unpinned_map.moss", {"ECHO_DISPLAY_UNSUPPORTED"}, 3),
          unresolved_elsewhere=lambda: check_negative_at(G2 / "unpinned_local_without_echo.moss", {"TYPE_AMBIGUOUS_OPEN_RESULT"}, 2),
          source_free=lambda: g4_source_free(s))


def g4_source_free(s):
    env, _ = source_free(s / "sf4", PROVIDER_FRESH)
    root, _ = consumer(s / "sf4", "g2fresh", '''module Consumer
import Provider
fn main():
  ints = Provider.fresh()
  ints.push(1)
  echo ints[0]
''')
    source_free_run(root, env, "1\n")
    relay_env, _ = source_free(s / "sf4-relay", PROVIDER_OPEN)
    _, ambiguous = consumer(s / "sf4", "g2ambiguous", '''module Consumer
import Provider
fn main():
  echo Provider.relay()
''')
    check_negative(ambiguous, {"SOURCE_FREE_CHECKED_BODY_UNAVAILABLE"},
                   ("Provider.relay",), relay_env)


def g5(s):
    env, _ = source_free(s / "sf5", PROVIDER_METHOD)
    _, source = consumer(s / "sf5", "g2missing", '''module Consumer
import Provider
fn main():
  echo Provider.outer(1)
''')
    # ABI-v10 rejects this legacy-only generic call chain before method
    # resolution; the typed checked-body path must not rebuild it from source.
    error = check_negative(source, {"SOURCE_FREE_CHECKED_BODY_UNAVAILABLE"},
                           ("Provider.outer", "callable-v1:"), env)
    assert error["source_file"].endswith("Provider.mossi"), error
    related = error.get("related") or []
    assert len(related) >= 1, "missing checked provider provenance"
    assert any((note.get("source") or {}).get("file", "").endswith("provider.moss")
               for note in related), related


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
    cases(two_consumers=lambda: g9_two_consumers(s),
          sealed_records=lambda: g9_records(s))


def g9_two_consumers(s):
    env, artifacts = source_free(s / "sf9", PROVIDER_BOX)
    outputs = []
    for name, value, expected in (("g2consumera", "1", "1\n"),
                                  ("g2consumerb", '"hello"', "hello\n")):
        root, _ = consumer(s / "sf9", name, f'''module Consumer
import Provider
fn main():
  item = Provider.Box(value: {value})
  echo Provider.unbox(item)
''')
        data = source_free_run(root, env, expected)
        outputs.append(data)
    # Both independent consumer artifacts must bind the provider artifact;
    # a prototype-only Rust type check does not satisfy this gate.
    assert len(outputs) == 2 and list(artifacts.glob("*.rlib"))


def g10(s):
    cases(identity_conflict=g10_identity_conflict,
          compiled_only_custom_key=lambda: g10_compiled_only(s))


def g10_identity_conflict():
    source = G2 / "key_proof_identity_conflict.moss"
    check_negative_at(source, {"TYPE_MISMATCH"}, 22)


def g10_compiled_only(s):
    # The key protocol belongs to Team S. A compiled-only Moss provider is the
    # acceptance surface, not a synthetic Rust Hash/Eq implementation.
    env, _ = source_free(s / "sf10", PROVIDER_KEY)
    for name in ("g2keyconsumera", "g2keyconsumerb"):
        root, _ = consumer(s / "sf10", name, '''module Consumer
import Provider
fn main():
  table = Provider.seed()
  echo Provider.lookup(table, Provider.UserKey(id: 1))
''')
        source_free_run(root, env, "7\n")


def g11(s):
    parity(FIX / "open_result_multihop.moss", s, "1\none\n")
    env, _ = source_free(s / "sf11", PROVIDER_FRESH)
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
