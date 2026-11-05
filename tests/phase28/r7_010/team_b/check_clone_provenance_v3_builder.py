"""Compile and independently verify native provenance-v3 raw-Rust derivation."""
import hashlib
import json
from pathlib import Path
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
assert [row["evidence"]["semantic_identity"] for row in manifest["occurrences"][:2]] == identities
assert len(manifest["source_copy_occurrences"][0]["rust_clone_spans"]) == 2
assert manifest["occurrences"][2]["source_copy_id"] is None
print("provenance v3 builder: exact fact IDs, raw helper links, clone spans and unclassified rows PASS")
