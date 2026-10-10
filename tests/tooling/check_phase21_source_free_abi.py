#!/usr/bin/env python3
"""Exercise the ABI-v8 .mossi loader with provider source removed."""

import json
from pathlib import Path
import shutil
import subprocess
import sys


repo = Path(__file__).resolve().parents[2]
compiler = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()


def run(args, *, cwd=None, expected=0):
    completed = subprocess.run(
        [str(item) for item in args], cwd=cwd, text=True,
        capture_output=True, timeout=180)
    assert completed.returncode == expected, (
        args, completed.returncode, completed.stdout, completed.stderr)
    return completed


def run_two_crate_rust_abi_fixture():
    rustc = shutil.which("rustc")
    assert rustc is not None, "rustc is required for the Phase 21 Rust ABI fixture"
    fixture = repo / "tests/tooling"
    rust_out = out / "rust-abi"
    rust_out.mkdir(parents=True)
    provider_source = rust_out / "provider.rs"
    consumer_source = rust_out / "consumer.rs"
    shutil.copyfile(
        fixture / "phase21_rust_abi_provider.rs.fixture", provider_source)
    shutil.copyfile(
        fixture / "phase21_rust_abi_consumer.rs.fixture", consumer_source)
    provider_rlib = rust_out / "libphase21_provider.rlib"
    run([
        rustc,
        "--edition=2021",
        "--crate-name=phase21_provider",
        "--crate-type=rlib",
        "-Cpanic=abort",
        "-Dwarnings",
        provider_source,
        "-o",
        provider_rlib,
    ], cwd=rust_out)

    # Prove the consumer only sees the materialized provider metadata/object.
    provider_source.unlink()
    assert not provider_source.exists()
    consumer_executable = rust_out / "phase21_consumer"
    run([
        rustc,
        "--edition=2021",
        "--crate-name=phase21_consumer",
        "-Cpanic=abort",
        "-Dwarnings",
        consumer_source,
        "--extern",
        f"phase21_provider={provider_rlib}",
        "-o",
        consumer_executable,
    ], cwd=rust_out)
    result = run([consumer_executable], cwd=rust_out)
    assert result.stdout.strip() == (
        "phase21 two-crate source-free Rust ABI fixture passed")


if out.exists():
    shutil.rmtree(out)
(out / "src").mkdir(parents=True)
(out / "moss.toml").write_text(
    '[project]\nname="phase21_provider"\nversion="0.1.0"\n')
provider = out / "src/provider.moss"
provider.write_text('''module provider
export enum FileError:
  Full(code: Int)
export enum BadPayload:
  Text(value: String)
export enum BadCapture:
  Open(file: Int)
export domain Worker:
  value: Int
  fn Bump():
    value = value + 1
  fn Read() -> Int:
    reply value
''')
main = out / "src/main.moss"
main.write_text('''module app
import provider
fn main():
  worker = provider.Worker(value: 3)
  try:
    message worker.Bump()
  recover:
    pass
  echo message worker.Read()
''')

first = json.loads(run([compiler, "build", "--json"], cwd=out).stdout)
assert run([first["result"]["artifacts"]["executable"]]).stdout.strip() == "4"
interface_path = out / "build/debug/provider.mossi"
original = interface_path.read_text()
assert "native_abi 8\n" in original
for required in (
    'handler_abi "Bump"',
    'handler_outcome_contract "Bump" "__MossBodyOutcome_provider__Worker_Bump" "unit"',
    'handler_wrapper_contract "Bump" nested=propagate '
    'root=join,close_fileio,release_guards,acquire_failure_locks,execute_arm,fulfill_reply',
    'handler_reply_contract "Bump" "unit" 1 1',
    'handler_owned_type_shapes "Bump" 0',
    'handler_raise_tags "Bump" 0',
    'handler_raise_projections "Bump" 0',
    'handler_raise_bridges "Bump" 0',
    'handler_failure_arms "Bump" 0',
    'handler_raising_nodes "Bump" 0',
    'handler_exceptional_destinations "Bump" 0',
    'handler_exceptional_edges "Bump" 0',
    'handler_exceptional_consumers "Bump" 63',
):
    assert required in original, required

# Freeze a nonempty provider contract as if Agent A had produced it. The
# checked provider source is then removed. The consumer must load the arm,
# owned capture, reply and exceptional-CFG records from .mossi alone.
synthetic = original.replace(
    'raise_set=-;may_diverge=0;unresolved=0\n'
    '  handler_state_effects "Bump"',
    'raise_set=enum:provider::FileError:Full;may_diverge=0;unresolved=0\n'
    '  handler_state_effects "Bump"',
    1,
)
synthetic = synthetic.replace(
    'enum_case "Open" 1 "file" "int"',
    'enum_case "Open" 1 "file" "FileIO"')
synthetic = synthetic.replace(
    '  handler_owned_type_shapes "Bump" 0\n',
    '  handler_owned_type_shapes "Bump" 1\n'
    '  handler_owned_type_shape "Bump" "Int" "0:3:Int:0:"\n',
)
synthetic = synthetic.replace(
    '  handler_raise_tags "Bump" 0\n',
    '  handler_raise_tags "Bump" 1\n'
    '  handler_raise_tag "Bump" 0 "enum:provider::FileError:Full" 1\n'
    '  handler_raise_payload "Bump" 0 "code" "Int" 0 1 1 '
    '"0:3:Int:0:"\n',
)
synthetic = synthetic.replace(
    '  handler_raise_projections "Bump" 0\n',
    '  handler_raise_projections "Bump" 1\n'
    '  handler_raise_projection "Bump" 0 "__MossPayload_Bump_0" '
    '"__MossResidual_Bump_0" "__MossProjection_Bump_0" '
    '"__moss_project_Bump_0" "__moss_rebuild_Bump_0"\n',
)
synthetic = synthetic.replace(
    '  handler_raise_bridges "Bump" 0\n',
    '  handler_raise_bridges "Bump" 1\n'
    '  handler_raise_bridge "Bump" 0 "provider::Store.Load" '
    '"provider::__MossOutcome_Store_Load" '
    '"provider::__MossFailureFrame_Store_Load" "FromStoreLoad" '
    '"__moss_bridge_Bump_from_Store_Load" 1\n'
    '  handler_raise_bridge_tag "Bump" 0 0 0 '
    '"enum:provider::FileError:Full"\n',
)
synthetic = synthetic.replace(
    '  handler_failure_arms "Bump" 0\n',
    '  handler_failure_arms "Bump" 1\n'
    '  handler_failure_arm "Bump" 0 "enum:provider::FileError:Full" '
    '"__moss_on_fail_provider__Worker_Bump_0" 1 1 0 1 "value" 0\n'
    '  handler_failure_capture "Bump" 0 "attempt" "Int" "attempt" 1 1 '
    '"0:3:Int:0:"\n',
)
synthetic = synthetic.replace(
    '  handler_raising_nodes "Bump" 0\n',
    '  handler_raising_nodes "Bump" 1\n'
    '  handler_raising_node "Bump" 77 0 0 0 0 1 '
    '"enum:provider::FileError:Full"\n',
)
synthetic = synthetic.replace(
    '  handler_exceptional_destinations "Bump" 0\n',
    '  handler_exceptional_destinations "Bump" 1\n'
    '  handler_exceptional_destination "Bump" 0 1 0 0 1 '
    '"enum:provider::FileError:Full"\n',
)
synthetic = synthetic.replace(
    '  handler_exceptional_edges "Bump" 0\n',
    '  handler_exceptional_edges "Bump" 1\n'
    '  handler_exceptional_edge "Bump" 77 0 1 63 1 '
    '"enum:provider::FileError:Full"\n',
)
assert synthetic != original
interface_path.write_text(synthetic)
provider.rename(provider.with_suffix(".removed"))
run([compiler, "--check", main], cwd=out)
rebuilt = json.loads(run([compiler, "build", "--json"], cwd=out).stdout)
assert run([rebuilt["result"]["artifacts"]["executable"]]).stdout.strip() == "4"

# v7 is intentionally incompatible because projection, bridge, structural
# type, CFG and Root contracts changed. It must request a full provider rebuild.
interface_path.write_text(synthetic.replace("native_abi 8\n", "native_abi 7\n"))
v7 = run([compiler, "--check", main], cwd=out, expected=1)
assert "incompatible native calling convention" in v7.stderr
assert "rebuild its .mossi provider" in v7.stderr

# ABI-v8 with missing exceptional-consumer metadata also fails closed.
interface_path.write_text(synthetic.replace(
    '  handler_exceptional_consumers "Bump" 63\n', ""))
missing = run([compiler, "--check", main], cwd=out, expected=1)
assert "lacks ABI-v8 handler metadata" in missing.stderr

for required_record in (
    '  handler_reply_contract "Bump" "unit" 1 1\n',
    '  handler_raising_nodes "Bump" 1\n',
    '  handler_raise_projections "Bump" 1\n',
    '  handler_raise_bridges "Bump" 1\n',
    '  handler_owned_type_shapes "Bump" 1\n',
    '  handler_exceptional_destinations "Bump" 1\n',
):
    interface_path.write_text(synthetic.replace(required_record, ""))
    missing = run([compiler, "--check", main], cwd=out, expected=1)
    assert "lacks ABI-v8 handler metadata" in missing.stderr

# Every effect fact is mandatory in v8, even when its value is false.
interface_path.write_text(synthetic.replace(";fileio=0", "", 1))
missing_effect = run([compiler, "--check", main], cwd=out, expected=1)
assert "lacks Phase 21 observable effects" in missing_effect.stderr

interface_path.write_text(synthetic)
run([compiler, "--check", main], cwd=out)

invalid_destination = synthetic.replace(
    'handler_exceptional_edge "Bump" 77 0 1 63 1',
    'handler_exceptional_edge "Bump" 77 99 1 63 1')
interface_path.write_text(invalid_destination)
invalid = run([compiler, "--check", main], cwd=out, expected=1)
assert "invalid ABI-v8 handler contract" in invalid.stderr

impossible_route = synthetic.replace(
    'handler_exceptional_edge "Bump" 77 0 1 63 1 '
    '"enum:provider::FileError:Full"',
    'handler_exceptional_edge "Bump" 77 0 1 63 1 "scalar:Int"')
interface_path.write_text(impossible_route)
invalid = run([compiler, "--check", main], cwd=out, expected=1)
assert "invalid ABI-v8 handler contract" in invalid.stderr

duplicate_route = synthetic.replace(
    'handler_exceptional_edge "Bump" 77 0 1 63 1 '
    '"enum:provider::FileError:Full"',
    'handler_exceptional_edge "Bump" 77 0 1 63 2 '
    '"enum:provider::FileError:Full" "enum:provider::FileError:Full"')
interface_path.write_text(duplicate_route)
invalid = run([compiler, "--check", main], cwd=out, expected=1)
assert "duplicate or incomplete exceptional edge" in invalid.stderr

# The loader derives legality from the resolved type graph. Claimed owned and
# bounded bits cannot make a nested unbounded payload or borrow safe.
bad_nested_payload = synthetic.replace(
    '"code" "Int" 0 1 1',
    '"code" "provider::BadPayload" 1 1 1')
interface_path.write_text(bad_nested_payload)
bad = run([compiler, "--check", main], cwd=out, expected=1)
assert "type shape disagrees with its resolved type" in bad.stderr

bad_borrowed_payload = synthetic.replace(
    '"code" "Int" 0 1 1', '"code" "&Int" 0 1 1')
interface_path.write_text(bad_borrowed_payload)
bad = run([compiler, "--check", main], cwd=out, expected=1)
assert "type shape disagrees with its resolved type" in bad.stderr

unknown_scalar_payload = synthetic.replace(
    '"code" "Int" 0 1 1 "0:3:Int:0:"',
    '"code" "Secret" 0 1 1 "0:6:Secret:0:"')
unknown_scalar_payload = unknown_scalar_payload.replace(
    '  handler_owned_type_shapes "Bump" 1\n'
    '  handler_owned_type_shape "Bump" "Int" "0:3:Int:0:"\n',
    '  handler_owned_type_shapes "Bump" 2\n'
    '  handler_owned_type_shape "Bump" "Int" "0:3:Int:0:"\n'
    '  handler_owned_type_shape "Bump" "Secret" "0:6:Secret:0:"\n')
interface_path.write_text(unknown_scalar_payload)
bad = run([compiler, "--check", main], cwd=out, expected=1)
assert "type shape disagrees with its resolved type" in bad.stderr

wrong_variant_field = synthetic.replace(
    '"code" "Int" 0 1 1 "0:3:Int:0:"',
    '"other" "Int" 0 1 1 "0:3:Int:0:"')
interface_path.write_text(wrong_variant_field)
bad = run([compiler, "--check", main], cwd=out, expected=1)
assert "disagrees with its enum variant" in bad.stderr

bad_nested_capture = synthetic.replace(
    '"attempt" "Int" "attempt" 1 1',
    '"attempt" "provider::BadCapture" "attempt" 1 1')
interface_path.write_text(bad_nested_capture)
bad = run([compiler, "--check", main], cwd=out, expected=1)
assert "type shape disagrees with its resolved type" in bad.stderr

# Owned unbounded non-capability values remain legal for on_fail captures.
owned_string_capture = synthetic.replace(
    '"attempt" "Int" "attempt" 1 1 "0:3:Int:0:"',
    '"attempt" "String" "attempt" 1 1 "3:6:String:0:"')
owned_string_capture = owned_string_capture.replace(
    '  handler_owned_type_shapes "Bump" 1\n'
    '  handler_owned_type_shape "Bump" "Int" "0:3:Int:0:"\n',
    '  handler_owned_type_shapes "Bump" 2\n'
    '  handler_owned_type_shape "Bump" "Int" "0:3:Int:0:"\n'
    '  handler_owned_type_shape "Bump" "String" "3:6:String:0:"\n')
interface_path.write_text(owned_string_capture)
run([compiler, "--check", main], cwd=out)

# A provider-private aggregate remains legal source-free when its complete,
# counted declaration includes every dependency. The loader independently
# checks intrinsic child kinds, so this is not a free-form safety assertion.
private_capture = synthetic.replace(
    '"attempt" "Int" "attempt" 1 1 "0:3:Int:0:"',
    '"attempt" "ProviderPrivate" "attempt" 1 1 '
    '"2:15:ProviderPrivate:2:0:3:Int:0:3:6:String:0:"')
private_capture = private_capture.replace(
    '  handler_owned_type_shapes "Bump" 1\n'
    '  handler_owned_type_shape "Bump" "Int" "0:3:Int:0:"\n',
    '  handler_owned_type_shapes "Bump" 3\n'
    '  handler_owned_type_shape "Bump" "Int" "0:3:Int:0:"\n'
    '  handler_owned_type_shape "Bump" "ProviderPrivate" '
    '"2:15:ProviderPrivate:2:0:3:Int:0:3:6:String:0:"\n'
    '  handler_owned_type_shape "Bump" "String" "3:6:String:0:"\n')
interface_path.write_text(private_capture)
run([compiler, "--check", main], cwd=out)

interface_path.write_text(synthetic)
run_two_crate_rust_abi_fixture()
print(
    "Phase 21.0 source-free ABI-v8 metadata, fail-closed loader, and "
    "two-crate Rust ABI checks passed.")
