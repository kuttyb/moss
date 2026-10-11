#!/usr/bin/env python3
"""Exercise ABI-v9 .mossi loading with provider source removed."""

import hashlib
import json
from pathlib import Path
import re
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


def write_interface(path, text):
    """Rebind a test-only artifact copy to probe parser checks past the v9 seal.

    Ordinary consumers never rewrite the artifact. This helper deliberately
    edits both metadata and its paired test rlib so legacy structural-invalid
    records can still reach the loader after the artifact-bound check.
    """
    if not hasattr(write_interface, "baselines"):
        write_interface.baselines = {}
    if path not in write_interface.baselines:
        rlib = path.with_suffix(".rlib")
        if not rlib.exists():
            rlib = path.with_name("lib" + path.stem + ".rlib")
        original_bytes = rlib.read_bytes()
        original_digest = re.search(
            r"(?m)^provider_contract_digest sha256:([0-9a-f]{64})$",
            path.read_text()).group(1)
        write_interface.baselines[path] = (rlib, original_bytes, original_digest)
    rlib, original_bytes, original_digest = write_interface.baselines[path]
    contract, semantic = text.split("\ncontract\n", 1)[1].split(
        "\nsemantic_exports\n", 1)
    def hash_section(section):
        value = 14695981039346656037
        for byte in section.encode():
            value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
        return f"{value:016x}"
    for key, section in (("concrete_interface_hash", contract + "\n"),
                         ("interface_hash", contract + "\n"),
                         ("generic_semantic_hash", semantic)):
        text, count = re.subn(rf"(?m)^{key} [0-9a-f]{{16}}$",
                              f"{key} {hash_section(section)}", text)
        assert count == 1, (key, count)
    material, count = re.subn(
        r"(?m)^provider_contract_digest sha256:[0-9a-f]{64}\n", "", text)
    assert count == 1
    digest = hashlib.sha256(material.encode()).hexdigest()
    text, count = re.subn(
        r"(?m)^provider_contract_digest sha256:[0-9a-f]{64}$",
        "provider_contract_digest sha256:" + digest, text)
    assert count == 1
    path.write_text(text)
    sealed = original_bytes.replace(original_digest.encode(), digest.encode())
    assert sealed != original_bytes or digest == original_digest
    rlib.write_bytes(sealed)


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


def run_source_free_raised_handler_fixture():
    """Exercise a real raised provider result after its Moss source is removed."""
    raised_out = out / "raised-handler"
    (raised_out / "src").mkdir(parents=True)
    (raised_out / "moss.toml").write_text(
        '[project]\nname="phase21_raised_provider"\nversion="0.1.0"\n')
    provider = raised_out / "src/service.moss"
    provider.write_text('''module service
export enum ServiceError:
  Bad(code: Int)
export domain Worker:
  fn Run() -> Int:
    raise ServiceError.Bad(code: 4)
''')
    main = raised_out / "src/main.moss"
    main.write_text('''module app
import service
domain Caller:
  domainroutes(worker: service.Worker)
  fn Run() -> Int:
    result = 0
    try:
      result = message worker.Run()
    recover service.ServiceError.Bad(code):
      result = code
    reply result
fn main():
  worker = service.Worker()
  caller = Caller(worker: worker)
  echo message caller.Run()
''')
    first = json.loads(run([compiler, "build", "--json"], cwd=raised_out).stdout)
    assert run([first["result"]["artifacts"]["executable"]]).stdout.strip() == "4"
    interface_path = raised_out / "build/debug/service.mossi"
    assert 'handler_raise_tags "Run" 1' in interface_path.read_text()
    provider.rename(provider.with_suffix(".removed"))
    rebuilt = json.loads(run([compiler, "build", "--json"], cwd=raised_out).stdout)
    assert run([rebuilt["result"]["artifacts"]["executable"]]).stdout.strip() == "4"
    main.write_text('''module app
import service
fn main():
  worker = service.Worker()
  echo message worker.Run()
''')
    unhandled = run([compiler, "--check", main], cwd=raised_out, expected=1)
    assert "main may not let typed errors escape" in unhandled.stderr


def run_source_free_owned_capture_fixture():
    """A provider-owned frame carries a non-Copy local without provider source."""
    capture_out = out / "owned-capture"
    (capture_out / "src").mkdir(parents=True)
    (capture_out / "moss.toml").write_text(
        '[project]\nname="phase21_owned_capture"\nversion="0.1.0"\n')
    provider = capture_out / "src/service.moss"
    provider.write_text('''module service
export enum ServiceError:
  Bad
export domain Worker:
  fn Run() -> String:
    attempt = "hello"
    raise ServiceError.Bad
  on_fail ServiceError.Bad:
    reply attempt
  fn Join(flag: Bool) -> String:
    if flag:
      let joined = "yes"
    else:
      let joined = "no"
    raise ServiceError.Bad
  on_fail ServiceError.Bad:
    reply joined
''')
    (capture_out / "src/main.moss").write_text('''module app
import service
fn main():
  worker = service.Worker()
  echo message worker.Run()
  echo message worker.Join(true)
  echo message worker.Join(false)
''')
    built = json.loads(run([compiler, "build", "--json"], cwd=capture_out).stdout)
    assert run([built["result"]["artifacts"]["executable"]]).stdout == (
        "hello\nyes\nno\n")
    interface = (capture_out / "build/debug/service.mossi").read_text()
    assert 'handler_failure_capture "Run" 0 "attempt" "string"' in interface
    assert 'handler_failure_capture "Join" 0 "joined" "string"' in interface
    provider.rename(provider.with_suffix(".removed"))
    rebuilt = json.loads(run([compiler, "build", "--json"], cwd=capture_out).stdout)
    assert run([rebuilt["result"]["artifacts"]["executable"]]).stdout == (
        "hello\nyes\nno\n")


def run_source_free_on_fail_trailer_fixture():
    """Execute a real provider trailer with only its .mossi and rlib present."""
    trailer_out = out / "on-fail-trailer"
    (trailer_out / "src").mkdir(parents=True)
    (trailer_out / "moss.toml").write_text(
        '[project]\nname="phase21_on_fail_trailer"\nversion="0.1.0"\n')
    provider = trailer_out / "src/service.moss"
    provider.write_text('''module service
export enum ServiceError:
  Bad(code: Int)
export domain Worker:
  count: Int
  fn Run() -> Int:
    raise ServiceError.Bad(code: 4)
  on_fail ServiceError.Bad(code):
    count = count + 1
    echo code
    reply count
''')
    provider_only = json.loads(run([compiler, "build", "--json"], cwd=trailer_out).stdout)
    assert provider_only["result"]["artifacts"]["module_rlibs"]
    interface = (trailer_out / "build/debug/service.mossi").read_text()
    assert '  handler_state_effects "Run" 0 0 0' in interface
    arm_record = next(line for line in interface.splitlines()
                      if line.startswith('  handler_failure_arm "Run" 0 '))
    assert ' 1 "count" 1 "count" 0' in arm_record, arm_record
    main = trailer_out / "src/main.moss"
    main.write_text('''module app
import service
fn main():
  worker = service.Worker(count: 3)
  echo message worker.Run()
''')
    source_backed = json.loads(run([compiler, "build", "--json"], cwd=trailer_out).stdout)
    assert run([source_backed["result"]["artifacts"]["executable"]]).stdout == "4\n4\n"
    provider.rename(provider.with_suffix(".removed"))
    # The provider artifact exports a real typed arm body independently of
    # its Moss source.
    arm_probe = trailer_out / "arm_probe.rs"
    arm_probe.write_text('''extern crate moss_service;
fn main() {
    let mut body_state = moss_service::service__WorkerRunState {
        __lifetime: std::marker::PhantomData,
    };
    let error = match moss_service::__moss_body_service__Worker_Run(&mut body_state) {
        Err(error) => error,
        Ok(_) => std::process::abort(),
    };
    let mut count = 3_i64;
    let mut failure_state = moss_service::service__WorkerRunProviderFailState {
        count: &mut count,
        __lifetime: std::marker::PhantomData,
    };
    let result = moss_service::__moss_on_fail_service__Worker_Run_0(
        &mut failure_state, error,
    );
    println!("{}", result);
}
''')
    arm_executable = trailer_out / "arm_probe"
    run([
        shutil.which("rustc"), "--edition=2021", "-Cpanic=abort", "-Dwarnings",
        arm_probe, "--extern",
        f"moss_service={trailer_out / 'build/debug/libmoss_service.rlib'}",
        "-o", arm_executable,
    ], cwd=trailer_out)
    assert run([arm_executable], cwd=trailer_out).stdout == "4\n4\n"
    source_free = json.loads(run([compiler, "build", "--json"], cwd=trailer_out).stdout)
    assert run([source_free["result"]["artifacts"]["executable"]]).stdout == "4\n4\n"
    main.write_text('''module app
import service
fn main():
  worker = service.Worker(count: 3)
  try:
    echo message worker.Run()
  recover service.ServiceError.Bad(code):
    echo code
''')
    source_free_recovered = json.loads(run(
        [compiler, "build", "--json"], cwd=trailer_out).stdout)
    assert run([source_free_recovered["result"]["artifacts"]["executable"]]).stdout == "4\n4\n"
    main.write_text('''module app
import service
domain Caller:
  domainroutes(worker: service.Worker)
  fn Run() -> Int:
    result = 0
    try:
      result = message worker.Run()
    recover service.ServiceError.Bad(code):
      result = code
    reply result
fn main():
  worker = service.Worker(count: 3)
  caller = Caller(worker: worker)
  echo message caller.Run()
''')
    nested = json.loads(run([compiler, "build", "--json"], cwd=trailer_out).stdout)
    assert run([nested["result"]["artifacts"]["executable"]]).stdout == "4\n"


def run_source_free_reporting_route_fixture():
    route_out = out / "reporting-route"
    (route_out / "src").mkdir(parents=True)
    (route_out / "moss.toml").write_text(
        '[project]\nname="phase21_reporting_route"\nversion="0.1.0"\n')
    provider = route_out / "src/service.moss"
    provider.write_text('''module service
export enum E:
  Bad
export domain Reporter:
  count: Int
  fn Record():
    count = count + 1
  fn Count() -> Int:
    reply count
export domain Worker:
  domainroutes(reporter: Reporter)
  fn Run():
    raise E.Bad
  on_fail E.Bad:
    message reporter.Record()
''')
    (route_out / "src/main.moss").write_text('''module app
import service
fn main():
  reporter = service.Reporter(count: 0)
  worker = service.Worker(reporter: reporter)
  message worker.Run()
  echo message reporter.Count()
''')
    backed = json.loads(run([compiler, "build", "--json"], cwd=route_out).stdout)
    assert run([backed["result"]["artifacts"]["executable"]]).stdout == "1\n"
    provider.rename(provider.with_suffix(".removed"))
    source_free = json.loads(run([compiler, "build", "--json"], cwd=route_out).stdout)
    assert run([source_free["result"]["artifacts"]["executable"]]).stdout == "1\n"


def run_source_free_multi_enum_fixture():
    multi_out = out / "multi-enum"
    (multi_out / "src").mkdir(parents=True)
    (multi_out / "moss.toml").write_text(
        '[project]\nname="phase21_multi_enum"\nversion="0.1.0"\n')
    provider = multi_out / "src/service.moss"
    provider.write_text('''module service
export enum E:
  A
export enum F:
  B
export domain Worker:
  count: Int
  fn Run(which: Int) -> Int:
    if which == 1:
      raise E.A
    raise F.B
  on_fail E.A:
    count = count + 1
    reply count
  on_fail F.B:
    count = count + 2
    reply count
''')
    main = multi_out / "src/main.moss"
    main.write_text('''module app
import service
fn main():
  worker = service.Worker(count: 0)
  echo message worker.Run(1)
  echo message worker.Run(2)
''')
    backed = json.loads(run([compiler, "build", "--json"], cwd=multi_out).stdout)
    assert run([backed["result"]["artifacts"]["executable"]]).stdout == "1\n3\n"
    provider.rename(provider.with_suffix(".removed"))
    source_free = json.loads(run([compiler, "build", "--json"], cwd=multi_out).stdout)
    assert run([source_free["result"]["artifacts"]["executable"]]).stdout == "1\n3\n"
    interface_path = multi_out / "build/debug/service.mossi"
    original = interface_path.read_text()
    switched = original.replace(
        '"__moss_on_fail_service__Worker_Run_0"',
        '"__moss_on_fail_service__Worker_Run_TEMP"').replace(
        '"__moss_on_fail_service__Worker_Run_1"',
        '"__moss_on_fail_service__Worker_Run_0"').replace(
        '"__moss_on_fail_service__Worker_Run_TEMP"',
        '"__moss_on_fail_service__Worker_Run_1"')
    assert switched != original
    # A same-shape symbol swap previously compiled and aborted at runtime.
    interface_path.write_text(switched)
    tampered = run([compiler, "--check", main], cwd=multi_out, expected=1)
    assert "provider metadata does not match its declared contract digest" in tampered.stderr
    # Recomputing the stored hashes must still fail the canonical symbol check.
    write_interface(interface_path, switched)
    forged = run([compiler, "--check", main], cwd=multi_out, expected=1)
    assert "invalid ABI-v8 failure-arm symbol" in forged.stderr
    interface_path.write_text(original)


def run_actual_exceptional_site_fixture():
    graph_out = out / "actual-cfg"
    (graph_out / "src").mkdir(parents=True)
    (graph_out / "moss.toml").write_text(
        '[project]\nname="phase21_actual_cfg"\nversion="0.1.0"\n')
    (graph_out / "src/service.moss").write_text('''module service
export enum E:
  Bad
export domain Worker:
  fn Run(which: Int):
    if which == 0:
      raise E.Bad
    attempt = 7
    try:
      if which == 1:
        raise E.Bad
    recover E.Bad:
      pass
    raise E.Bad
  on_fail E.Bad:
    pass
''')
    (graph_out / "src/main.moss").write_text('''module app
import service
fn main():
  worker = service.Worker()
  message worker.Run(2)
''')
    built = json.loads(run([compiler, "build", "--json"], cwd=graph_out).stdout)
    assert run([built["result"]["artifacts"]["executable"]]).stdout == ""
    interface = (graph_out / "build/debug/service.mossi").read_text()
    assert 'handler_raising_nodes "Run" 3' in interface, interface
    assert 'handler_exceptional_edges "Run" 3' in interface, interface
    assert 'handler_exceptional_destinations "Run" 2' in interface, interface
    # The two handler exits have the same identity but different source nodes:
    # one occurs before `attempt` is initialized, one after it. The middle
    # site routes to its lexical recover arm.
    edges = sorted({line for line in interface.splitlines()
                    if line.startswith('  handler_exceptional_edge "Run" ')})
    assert len(edges) == 3, edges
    assert sum(' 0 63 1 ' in line for line in edges) == 1, edges
    assert sum(' 1 63 1 ' in line for line in edges) == 2, edges


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
assert "native_abi 9\n" in original
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

# Forge a nonempty provider contract to validate the loader's arm, owned
# capture, reply and exceptional-CFG checks with provider source removed.
# The forged contract cannot be linked to the nonraising provider binary.
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
write_interface(interface_path, synthetic)
provider.rename(provider.with_suffix(".removed"))
# A forged interface is structurally parseable but has no matching provider
# symbols. Native linking remains the materialization boundary.
forged = run([compiler, "build", "--json"], cwd=out, expected=1)
assert "error" in forged.stdout or forged.stderr, forged

# v7 is intentionally incompatible because projection, bridge, structural
# type, CFG and Root contracts changed. It must request a full provider rebuild.
write_interface(interface_path, synthetic.replace("native_abi 9\n", "native_abi 7\n"))
v7 = run([compiler, "--check", main], cwd=out, expected=1)
assert "incompatible native calling convention" in v7.stderr
assert "rebuild its .mossi provider" in v7.stderr

# ABI-v8 with missing exceptional-consumer metadata also fails closed.
write_interface(interface_path, synthetic.replace(
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
    write_interface(interface_path, synthetic.replace(required_record, ""))
    missing = run([compiler, "--check", main], cwd=out, expected=1)
    assert "lacks ABI-v8 handler metadata" in missing.stderr

# Every effect fact is mandatory in v8, even when its value is false.
write_interface(interface_path, synthetic.replace(";fileio=0", "", 1))
missing_effect = run([compiler, "--check", main], cwd=out, expected=1)
assert "lacks Phase 21 observable effects" in missing_effect.stderr

write_interface(interface_path, synthetic)
run([compiler, "--check", main], cwd=out)

invalid_destination = synthetic.replace(
    'handler_exceptional_edge "Bump" 77 0 1 63 1',
    'handler_exceptional_edge "Bump" 77 99 1 63 1')
write_interface(interface_path, invalid_destination)
invalid = run([compiler, "--check", main], cwd=out, expected=1)
assert "invalid ABI-v8 handler contract" in invalid.stderr

impossible_route = synthetic.replace(
    'handler_exceptional_edge "Bump" 77 0 1 63 1 '
    '"enum:provider::FileError:Full"',
    'handler_exceptional_edge "Bump" 77 0 1 63 1 "scalar:Int"')
write_interface(interface_path, impossible_route)
invalid = run([compiler, "--check", main], cwd=out, expected=1)
assert "invalid ABI-v8 handler contract" in invalid.stderr

duplicate_route = synthetic.replace(
    'handler_exceptional_edge "Bump" 77 0 1 63 1 '
    '"enum:provider::FileError:Full"',
    'handler_exceptional_edge "Bump" 77 0 1 63 2 '
    '"enum:provider::FileError:Full" "enum:provider::FileError:Full"')
write_interface(interface_path, duplicate_route)
invalid = run([compiler, "--check", main], cwd=out, expected=1)
assert "duplicate or incomplete exceptional edge" in invalid.stderr

# The loader derives legality from the resolved type graph. Claimed owned and
# bounded bits cannot make a nested unbounded payload or borrow safe.
bad_nested_payload = synthetic.replace(
    '"code" "Int" 0 1 1',
    '"code" "provider::BadPayload" 1 1 1')
write_interface(interface_path, bad_nested_payload)
bad = run([compiler, "--check", main], cwd=out, expected=1)
assert "type shape disagrees with its resolved type" in bad.stderr

bad_borrowed_payload = synthetic.replace(
    '"code" "Int" 0 1 1', '"code" "&Int" 0 1 1')
write_interface(interface_path, bad_borrowed_payload)
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
write_interface(interface_path, unknown_scalar_payload)
bad = run([compiler, "--check", main], cwd=out, expected=1)
assert "type shape disagrees with its resolved type" in bad.stderr

wrong_variant_field = synthetic.replace(
    '"code" "Int" 0 1 1 "0:3:Int:0:"',
    '"other" "Int" 0 1 1 "0:3:Int:0:"')
write_interface(interface_path, wrong_variant_field)
bad = run([compiler, "--check", main], cwd=out, expected=1)
assert "disagrees with its enum variant" in bad.stderr

bad_nested_capture = synthetic.replace(
    '"attempt" "Int" "attempt" 1 1',
    '"attempt" "provider::BadCapture" "attempt" 1 1')
write_interface(interface_path, bad_nested_capture)
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
write_interface(interface_path, owned_string_capture)
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
write_interface(interface_path, private_capture)
run([compiler, "--check", main], cwd=out)

write_interface(interface_path, synthetic)
run_two_crate_rust_abi_fixture()
run_source_free_raised_handler_fixture()
run_source_free_owned_capture_fixture()
run_source_free_on_fail_trailer_fixture()
run_source_free_reporting_route_fixture()
run_source_free_multi_enum_fixture()
run_actual_exceptional_site_fixture()
print(
    "Phase 21.0 source-free ABI-v8 metadata, Root trailer dispatch, "
    "nested propagation, and native raised-handler checks passed.")
