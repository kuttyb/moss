"""Compile and independently verify native provenance-v3 raw-Rust derivation."""
import hashlib
import json
from copy import deepcopy
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
assert len(manifest["source_copy_occurrences"][0]["rust_clone_spans"]) == 2
assert manifest["occurrences"][2]["source_copy_id"] is None


def explicit_copy_fact_links(candidate):
    """Validate per-occurrence authority links without assuming global order."""
    facts_by_copy = {}
    for wrapper in candidate["checked_copy_facts"]:
        facts = wrapper["facts"]
        ids = []
        by_id = {}
        for fact in facts:
            canonical = json.dumps(fact, sort_keys=True, separators=(",", ":"),
                                   ensure_ascii=False, allow_nan=False).encode()
            fact_id = "typed-copy-fact-v1:" + hashlib.sha256(canonical).hexdigest()
            ids.append(fact_id)
            by_id[fact_id] = fact
        if len(ids) != len(set(ids)) or ids != sorted(ids):
            return False
        facts_by_copy[wrapper["copy_id"]] = by_id

    for occurrence in candidate["occurrences"]:
        if occurrence["origin_kind"] != "explicit_copy":
            continue
        copy_id = occurrence["source_copy_id"]
        fact_id = occurrence["evidence"]["semantic_identity"]
        fact = facts_by_copy.get(copy_id, {}).get(fact_id)
        if not fact:
            return False
        if occurrence["evidence"]["semantic_type"] != fact["result_type_key"]:
            return False
        if occurrence["evidence"]["ownership_fact"] != fact["required_source_access"]:
            return False
    return True


assert explicit_copy_fact_links(manifest)
# Raw occurrence order and canonical fact order are independent. The exact
# fact-to-occurrence links must remain valid if Rust occurrences are permuted.
permuted = deepcopy(manifest)
permuted["occurrences"].reverse()
assert explicit_copy_fact_links(permuted)
# A forged link and a duplicate checker fact must still fail the link check.
forged = deepcopy(manifest)
forged["occurrences"][0]["evidence"]["semantic_identity"] = \
    "typed-copy-fact-v1:" + "0" * 64
assert not explicit_copy_fact_links(forged)
duplicate = deepcopy(manifest)
duplicate["checked_copy_facts"][0]["facts"].append(
    deepcopy(duplicate["checked_copy_facts"][0]["facts"][0]))
assert not explicit_copy_fact_links(duplicate)
print("provenance v3 builder: exact fact IDs, raw helper links, clone spans and unclassified rows PASS")
