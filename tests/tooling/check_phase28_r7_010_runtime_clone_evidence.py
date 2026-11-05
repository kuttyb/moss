#!/usr/bin/env python3
"""Keep ORACLE-10A runtime clone evidence distinct from authorization."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "docs/phase28/team-c/P28_R7_010_ORACLE10A_RUNTIME_CLONE_EVIDENCE.json"
REPORT = ROOT / "docs/phase28/team-c/P28_R7_010_GENERATION_SITE_AUDIT.md"
HARNESS_PATH = ROOT / "tests/phase28/oracle_r7_005_1/oracle_harness.py"
SCANNER_PATH = ROOT / "tests/tooling/rust_duplication_scanner.py"

spec = importlib.util.spec_from_file_location("oracle_harness", HARNESS_PATH)
harness = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = harness
spec.loader.exec_module(harness)
scanner_spec = importlib.util.spec_from_file_location("rust_duplication_scanner", SCANNER_PATH)
scanner = importlib.util.module_from_spec(scanner_spec)
sys.modules[scanner_spec.name] = scanner
scanner_spec.loader.exec_module(scanner)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git_blob_sha(commit: str, path: str) -> str:
    content = subprocess.run(["git", "show", f"{commit}:{path}"], cwd=ROOT,
                             check=True, capture_output=True).stdout
    return hashlib.sha256(content).hexdigest()


def audit_marked_runtime_sites(rust: str) -> None:
    comments: list[dict] = []
    masked = scanner.mask_non_code(rust, comments)
    expected = {
        "r7-010-runtime-01": ("dot_clone", "self.lock_inner().active_executor.clone()"),
        "r7-010-runtime-02": ("dot_clone", "self.affinity.clone()"),
        "r7-010-runtime-03": ("dot_clone", "s.clone().into_raw_owned()"),
        "r7-010-runtime-04": ("qualified_clone", "std::sync::Arc::clone(&node)"),
        "r7-010-runtime-05": ("qualified_clone", "std::sync::Arc::clone(&waiter)"),
        "r7-010-runtime-06": ("qualified_clone", "std::sync::Arc::clone(scope)"),
        "r7-010-runtime-07": ("qualified_clone", "MossWorkerContextGuard::enter(worker_id, std::sync::Arc::clone(gx))"),
        "r7-010-runtime-08": ("qualified_clone", "let gx2 = std::sync::Arc::clone(gx)"),
        "r7-010-runtime-09": ("qualified_clone", "inner.active_executor = Some(std::sync::Arc::clone(&gx))"),
        "r7-010-runtime-10": ("qualified_clone", "let reply = std::sync::Arc::clone(&reply_slot)"),
        "r7-010-runtime-11": ("qualified_clone", "std::sync::Arc::clone(inner.active_executor.as_ref().expect(\"active executor missing in ACTIVE state\"))"),
    }
    observed = {}
    for comment in comments:
        match = re.fullmatch(r"\s*MOSS_RUNTIME_CLONE_SITE:(r7-010-runtime-\d{2})\s*", comment["text"])
        if match:
            marker = match.group(1)
            assert marker not in observed, f"duplicate generated marker: {marker}"
            observed[marker] = comment
    assert set(observed) == set(expected), (sorted(observed), sorted(expected))
    operator_spans = {name: scanner.operator_spans(masked, name)
                      for name in ("dot_clone", "qualified_clone")}
    bound = set()
    for marker, (kind, expected_expression) in expected.items():
        comment = observed[marker]
        marker_line = rust.count("\n", 0, comment["start"])
        line_start = rust.rfind("\n", 0, comment["start"]) + 1
        line_end = rust.find("\n", comment["end"])
        line_end = len(rust) if line_end < 0 else line_end
        line = rust[line_start:line_end]
        marker_text = f"/* MOSS_RUNTIME_CLONE_SITE:{marker} */"
        normalized_line = re.sub(r"\(\s+", "(", re.sub(
            r"\s+", " ", line.replace(marker_text, " ")))
        assert line.count(marker_text) == 1 and expected_expression in normalized_line, (
            f"{marker} is not anchored to its expected generated expression")
        matching = [span for span in operator_spans[kind]
                    if rust.count("\n", 0, span[0]) == marker_line]
        assert len(matching) == 1, f"{marker} does not bind to exactly one {kind}: {matching}"
        span = matching[0]
        assert comment["start"] < span[0], f"{marker} must precede its operator"
        assert span not in bound, f"multiple markers map to one operator: {marker}"
        bound.add(span)
    assert len(operator_spans["dot_clone"]) == 4  # three runtime sites plus one checked copy
    assert len(operator_spans["qualified_clone"]) == 8


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: check_phase28_r7_010_runtime_clone_evidence.py COMPILER CANONICAL_SOURCE")

    evidence = json.loads(EVIDENCE.read_text())
    artifact = evidence["artifact"]
    assert artifact["rust_sha256"] == (
        "b51fd33939f319e1eaf1fc6b1e08f3b64be3ee06a93db598eaacd976913013bd")
    assert artifact["sidecar_runtime_clone_status"] == "unclassified"
    report = REPORT.read_text()
    assert artifact["rust_sha256"] in report
    assert "all six runtime sites\nremain unclassified pending L7-approved provenance" in report
    sites = evidence["runtime_clone_sites"]
    assert len(sites) == artifact["sidecar_runtime_clone_count"] == 6
    assert all(site["authorization"] == "unclassified" for site in sites)
    assert [site["rust_line"] for site in sites] == [2014, 2241, 2357, 2907, 2949, 3112]

    # This evidence describes a historical artifact. Validate its source links
    # at the recorded producer commit; current runtime templates have since
    # been repaired and must be checked through the live marked-site audit.
    producer = artifact["producer_commit"]
    subprocess.run(["git", "cat-file", "-e", f"{producer}^{{commit}}"],
                   cwd=ROOT, check=True, capture_output=True)
    assert git_blob_sha(producer, "src/executor_runtime.cpp") == evidence["source_files"]["src/executor_runtime.cpp"]
    assert git_blob_sha(producer, "src/char_string_ordering_runtime.cpp") == evidence["source_files"]["src/char_string_ordering_runtime.cpp"]
    fixture_rel = "tests/phase28/oracle_r7_005_1/fixtures/oracle_10a_explicit_copy_only.moss"
    assert git_blob_sha(producer, fixture_rel) == artifact["source_sha256"]

    compiler = Path(sys.argv[1]).resolve()
    source_path = Path(sys.argv[2]).resolve()
    assert compiler.is_file() and os.access(compiler, os.X_OK), compiler
    assert source_path.resolve() == (ROOT / fixture_rel).resolve()

    slices = evidence["string_slice_materializations"]
    assert len(slices) == 2
    assert all(item["authorization"] == "unclassified" for item in slices)
    for item in slices:
        rel, line_text = item["source"].split(":")
        assert item["rust_expression"] in (ROOT / rel).read_text().splitlines()[int(line_text) - 1]
    error = evidence["raised_error_clone_invocations"]
    assert error["observed_in_artifact"] == 0
    assert error["artifact_clone_scan_total"] == 7
    assert "not semantic authorization" in " ".join(evidence["limitations"])

    with tempfile.TemporaryDirectory(prefix="r7-010-runtime-evidence-", dir=ROOT / "tmp") as tmp:
        rust_path = Path(tmp) / "oracle.rs"
        generated = subprocess.run([str(compiler), str(source_path), "-o", str(rust_path)],
                                   capture_output=True, text=True, timeout=120)
        assert generated.returncode == 0, (generated.stdout, generated.stderr)
        rust_bytes = rust_path.read_bytes()
        rust = rust_bytes.decode("utf-8")
        rust_lines = rust.splitlines()

        # Every current runtime clone candidate has a unique emitted marker,
        # and each marker binds to one expected Rust duplication operator.
        audit_marked_runtime_sites(rust)
        # Marker parsing must fail closed under omission, duplication and text
        # hidden inside a Rust literal.
        marker = "/* MOSS_RUNTIME_CLONE_SITE:r7-010-runtime-04 */"
        assert marker in rust
        for corrupted in (rust.replace(marker, "", 1),
                          rust.replace(marker, marker + marker, 1),
                          rust.replace(marker, "", 1) +
                          '\nlet decoy = "/* MOSS_RUNTIME_CLONE_SITE:r7-010-runtime-04 */";\n'):
            try:
                audit_marked_runtime_sites(corrupted)
            except AssertionError:
                pass
            else:
                raise AssertionError("runtime marker corruption was accepted")
        swapped = rust.replace("r7-010-runtime-01", "r7-010-runtime-SWAP", 1)
        swapped = swapped.replace("r7-010-runtime-02", "r7-010-runtime-01", 1)
        swapped = swapped.replace("r7-010-runtime-SWAP", "r7-010-runtime-02", 1)
        try:
            audit_marked_runtime_sites(swapped)
        except AssertionError:
            pass
        else:
            raise AssertionError("runtime marker identity swap was accepted")
        relocated = rust.splitlines()
        relocated_index = next(index for index, line in enumerate(relocated)
                               if "MOSS_RUNTIME_CLONE_SITE:r7-010-runtime-01" in line)
        relocated_line = relocated[relocated_index]
        relocated_line = relocated_line.replace(
            "/* MOSS_RUNTIME_CLONE_SITE:r7-010-runtime-01 */ ", "", 1)
        relocated_line = relocated_line.replace(
            "self.lock_inner().active_executor.clone()",
            "self.lock_inner().active_executor.clone() /* MOSS_RUNTIME_CLONE_SITE:r7-010-runtime-01 */", 1)
        relocated[relocated_index] = relocated_line
        try:
            audit_marked_runtime_sites("\n".join(relocated) + "\n")
        except AssertionError:
            pass
        else:
            raise AssertionError("runtime marker relocation was accepted")

        marker_stripped = re.sub(
            rb"/\* MOSS_RUNTIME_CLONE_SITE:r7-010-runtime-\d{2} \*/\s*", b"", rust_bytes)
        assert hashlib.sha256(marker_stripped).hexdigest() == (
            "801517768d0e9493b90c66e2aff18f760fdbcbf37b82874fedf5b220c0a60bca")

        # Generated runtime lines remain byte-identical to the reviewed
        # implementation after removing marker comments; markers are locators,
        # never authorization evidence.
        assert "active_executor: std::option::Option<std::sync::Arc<MossGlobalExec>>" in rust
        assert "pub affinity: std::option::Option<std::vec::Vec<usize>>" in rust
        assert "state: std::sync::Arc<MossBranchScopeState>" in rust

        authority = harness.query_copy_authority(compiler, source_path)
        assert isinstance(authority, dict) and authority.get("status") == "available", authority
        ingress_authority = harness.query_root_ingress_capture_authority(compiler, source_path)
        assert isinstance(ingress_authority, dict) and ingress_authority.get("status") == "available", ingress_authority
        audit = harness.audit_clone_provenance(rust_path, source_path, "debug", 1,
                                               authority, ingress_authority)
        assert (audit.get("status"), audit.get("authorized"), audit.get("unauthorized"),
                audit.get("unclassified")) == ("UNAUTHORIZED", 1, 3, 3), audit
        sidecar = json.loads(Path(str(rust_path) + ".mossclone.json").read_text())
        assert [row["origin_kind"] for row in sidecar["occurrences"]].count("explicit_copy") == 1
        assert [row["origin_kind"] for row in sidecar["occurrences"]].count("unclassified") == 3

    print("phase28 R7-010 runtime clone evidence: PASS "
          "(historical artifact record validated; current output has 11 marked runtime sites, "
          "1 checked copy and 3 unauthorized/unclassified direct clones; markers grant no authorization)")


if __name__ == "__main__":
    main()
