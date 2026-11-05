"""Compile and independently verify native provenance-v3 raw-Rust derivation."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / "tmp/team-b/provenance-v3-builder"
OUT.mkdir(parents=True, exist_ok=True)
source_path = OUT / "source.moss"
exe = OUT / "builder-test"
subprocess.run([
    "c++", "-std=c++17", "-I", ROOT / "src",
    ROOT / "tests/phase28/r7_010/team_b/clone_provenance_v3_builder_test.cpp",
    "-o", exe,
], cwd=ROOT, check=True)
result = subprocess.run([exe, source_path], cwd=ROOT, text=True,
                        capture_output=True, check=True)
manifest = json.loads(result.stdout)
assert set(manifest) == {
    "format", "version", "mode", "rust_sha256", "source_sha256",
    "source_copy_occurrences", "checked_copy_facts",
    "root_ingress_domain_captures", "occurrences",
}
assert manifest["root_ingress_domain_captures"] == []
assert manifest["format"] == "moss-clone-provenance" and manifest["version"] == 3
assert len(manifest["source_copy_occurrences"]) == 1
assert len(manifest["checked_copy_facts"]) == 1
group = manifest["checked_copy_facts"][0]
assert len(group["facts"]) == 2
identities = []
for fact in group["facts"]:
    canonical = json.dumps(fact, sort_keys=True, separators=(",", ":"),
                           ensure_ascii=False, allow_nan=False).encode()
    identities.append("typed-copy-fact-v1:" + hashlib.sha256(canonical).hexdigest())
assert [row["origin_kind"] for row in manifest["occurrences"]] == [
    "explicit_copy", "explicit_copy", "unclassified"]
assert {row["evidence"]["semantic_identity"] for row in manifest["occurrences"][:2]} == set(identities)
assert len(manifest["source_copy_occurrences"][0]["rust_clone_spans"]) == 2
assert manifest["occurrences"][2]["source_copy_id"] is None
print("provenance v3 builder: exact fact IDs, raw helper links, clone spans and unclassified rows PASS")

# Exercise the production artifact writer and independent oracle validator on
# real generated Rust, then verify that malformed sidecars fail closed.
oracle_spec = importlib.util.spec_from_file_location(
    "phase28_oracle_harness",
    ROOT / "tests/phase28/oracle_r7_005_1/oracle_harness.py")
oracle = importlib.util.module_from_spec(oracle_spec)
sys.modules[oracle_spec.name] = oracle
oracle_spec.loader.exec_module(oracle)
fixture = ROOT / "tests/phase28/oracle_r7_005_1/fixtures/oracle_10a_explicit_copy_only.moss"
compiler = ROOT / "moss"
real_dir = ROOT / "tmp/team-b/provenance-v3-real"
if real_dir.exists():
    shutil.rmtree(real_dir)
real_dir.mkdir(parents=True)
rust_path = real_dir / "oracle.rs"
subprocess.run([compiler, fixture, "-o", rust_path], cwd=ROOT, check=True,
               capture_output=True, text=True)
authority = oracle.query_copy_authority(compiler, fixture)
assert authority and authority.get("status") == "available", authority
root_authority = oracle.query_root_ingress_capture_authority(compiler, fixture)
assert root_authority and root_authority.get("status") == "available", root_authority
real_audit = oracle.audit_clone_provenance(
    rust_path, fixture, "debug", 1, authority, root_authority)
assert (real_audit.get("status"), real_audit.get("authorized"),
        real_audit.get("unauthorized"), real_audit.get("unclassified")) == (
            "UNAUTHORIZED", 1, 3, 3), real_audit
sidecar = Path(str(rust_path) + ".mossclone.json")
original = sidecar.read_text()
original_manifest = json.loads(original)

def rejects_mutation(mutator):
    candidate = json.loads(original)
    mutator(candidate)
    sidecar.write_text(json.dumps(candidate))
    result = oracle.audit_clone_provenance(
        rust_path, fixture, "debug", 1, authority, root_authority)
    assert result.get("status") == "INVALID_PROVENANCE", result

try:
    sidecar.unlink()
    missing = oracle.audit_clone_provenance(
        rust_path, fixture, "debug", 1, authority, root_authority)
    assert missing.get("status") == "PROVENANCE_MISSING", missing
    sidecar.write_text(original)

    rejects_mutation(lambda value: value["occurrences"].append(value["occurrences"][0]))
    rejects_mutation(lambda value: value.update(rust_sha256="0" * 64))
    rejects_mutation(lambda value: value["occurrences"][-1]["evidence"].update(
        semantic_identity="typed-copy-fact-v1:" + "0" * 64))
    explicit_index = next(index for index, row in enumerate(original_manifest["occurrences"])
                          if row["origin_kind"] == "explicit_copy")
    rejects_mutation(lambda value: value["occurrences"][explicit_index]["evidence"].update(
        generated_function_variant_id="rust-function-variant-v1:" + "0" * 64))
    rejects_mutation(lambda value: value["occurrences"][explicit_index]["evidence"].update(
        generated_function_symbol="forged_body"))
    rejects_mutation(lambda value: value["occurrences"][explicit_index]["evidence"].update(
        checked_callable_symbols=["forged_body"]))
    rejects_mutation(lambda value: value["occurrences"][explicit_index]["evidence"][
        "generated_function_span"]["start"].update(column=2))

    # Coordinate a raw artifact marker/declaration rename with a forged
    # checked_callable_symbols sidecar list. The oracle must compare callable
    # symbols with independent checked-program authority, not trust the list
    # supplied by the same sidecar being audited.
    original_rust = rust_path.read_text()
    explicit_row = original_manifest["occurrences"][explicit_index]
    clone_begin, _ = oracle._span_offsets(original_rust,
                                           explicit_row["rust_span"])
    variant_identity, variant_symbol, variant_begin, variant_end, variant_kind = \
        oracle._tooling_function_variant_at(original_rust, clone_begin)
    assert variant_kind == "main" and variant_symbol == "main"
    marker = ("// Moss tooling begin|main|" + variant_identity +
              "|main|")
    forged_marker = ("// Moss tooling begin|main|" + variant_identity +
                     "|xain|")
    assert original_rust.count(marker) == 1
    assert original_rust.count("\nfn main() {") == 1
    forged_rust = original_rust.replace(marker, forged_marker).replace(
        "\nfn main() {", "\nfn xain() {")
    assert len(forged_rust) == len(original_rust)
    forged_manifest = json.loads(original)
    forged_row = forged_manifest["occurrences"][explicit_index]
    forged_evidence = forged_row["evidence"]
    forged_evidence["generated_function_symbol"] = "xain"
    forged_evidence["checked_callable_symbols"] = ["xain"]
    forged_variant = oracle._tooling_function_variant_at(forged_rust,
                                                          clone_begin)
    forged_evidence["generated_function_variant_id"] = \
        oracle._function_variant_identity(forged_rust, *forged_variant[:4])
    forged_manifest["rust_sha256"] = hashlib.sha256(
        forged_rust.encode()).hexdigest()
    rust_path.write_text(forged_rust)
    sidecar.write_text(json.dumps(forged_manifest))
    coordinated_forgery = oracle.audit_clone_provenance(
        rust_path, fixture, "debug", 1, authority, root_authority)
    assert coordinated_forgery.get("status") == "INVALID_PROVENANCE", \
        coordinated_forgery
    rust_path.write_text(original_rust)
    sidecar.write_text(original)

    rejects_mutation(lambda value: value["checked_copy_facts"].append(
        value["checked_copy_facts"][0]))
    runtime_indices = [index for index, row in enumerate(original_manifest["occurrences"])
                       if row["origin_kind"] == "unclassified"]
    assert len(runtime_indices) == 3, runtime_indices
    for runtime_index in runtime_indices:
        rejects_mutation(lambda value, i=runtime_index:
                         value["occurrences"].pop(i))
        rejects_mutation(lambda value, i=runtime_index:
                         value["occurrences"][i].update(origin_kind="runtime_support"))
finally:
    if "original_rust" in locals():
        rust_path.write_text(original_rust)
    sidecar.write_text(original)
print("provenance v3 production sidecar: exact copy fact accepted; missing, duplicate, forged and corrupt cases fail closed")

# Keep the checker/emitter projection covered for an ordinary named function,
# whose semantic context includes a source-location suffix in some paths.
function_fixture = real_dir / "function_copy.moss"
function_fixture.write_text("""type Record:
  tag: Int

fn duplicate(values: Vector[Record]) -> Record:
  return copy(values[0])

fn main():
  var values = [Record(tag: 1)]
  let result = duplicate(values)
  echo result.tag
""")
function_rust = real_dir / "function_copy.rs"
subprocess.run([compiler, function_fixture, "-o", function_rust], cwd=ROOT,
               check=True, capture_output=True, text=True)
function_authority = oracle.query_copy_authority(compiler, function_fixture)
assert function_authority and function_authority.get("status") == "available", \
    function_authority
function_root_authority = oracle.query_root_ingress_capture_authority(
    compiler, function_fixture)
function_audit = oracle.audit_clone_provenance(
    function_rust, function_fixture, "debug", 1, function_authority,
    function_root_authority)
assert function_audit.get("status") in {"PASS", "UNAUTHORIZED"} and \
       function_audit.get("authorized", 0) >= 1, function_audit
print("provenance v3 ordinary-function callable projection: PASS")
