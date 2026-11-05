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
    "source_copy_occurrences", "checked_copy_facts", "occurrences",
}
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
real_audit = oracle.audit_clone_provenance(rust_path, fixture, "debug", 1, authority)
assert (real_audit.get("status"), real_audit.get("authorized"),
        real_audit.get("unauthorized"), real_audit.get("unclassified")) == (
            "UNAUTHORIZED", 1, 6, 6), real_audit
sidecar = Path(str(rust_path) + ".mossclone.json")
original = sidecar.read_text()
original_manifest = json.loads(original)

def rejects_mutation(mutator):
    candidate = json.loads(original)
    mutator(candidate)
    sidecar.write_text(json.dumps(candidate))
    result = oracle.audit_clone_provenance(rust_path, fixture, "debug", 1, authority)
    assert result.get("status") == "INVALID_PROVENANCE", result

try:
    sidecar.unlink()
    missing = oracle.audit_clone_provenance(rust_path, fixture, "debug", 1, authority)
    assert missing.get("status") == "PROVENANCE_MISSING", missing
    sidecar.write_text(original)

    rejects_mutation(lambda value: value["occurrences"].append(value["occurrences"][0]))
    rejects_mutation(lambda value: value.update(rust_sha256="0" * 64))
    rejects_mutation(lambda value: value["occurrences"][-1]["evidence"].update(
        semantic_identity="typed-copy-fact-v1:" + "0" * 64))
    rejects_mutation(lambda value: value["checked_copy_facts"].append(
        value["checked_copy_facts"][0]))
    runtime_indices = [index for index, row in enumerate(original_manifest["occurrences"])
                       if row["origin_kind"] == "unclassified"]
    assert len(runtime_indices) == 6, runtime_indices
    for runtime_index in runtime_indices:
        rejects_mutation(lambda value, i=runtime_index:
                         value["occurrences"].pop(i))
        rejects_mutation(lambda value, i=runtime_index:
                         value["occurrences"][i].update(origin_kind="runtime_support"))
finally:
    sidecar.write_text(original)
print("provenance v3 production sidecar: exact copy fact accepted; missing, duplicate, forged and corrupt cases fail closed")
