#!/usr/bin/env python3
"""Keep ORACLE-10A runtime clone evidence distinct from authorization."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "docs/phase28/team-c/P28_R7_010_ORACLE10A_RUNTIME_CLONE_EVIDENCE.json"
REPORT = ROOT / "docs/phase28/team-c/P28_R7_010_GENERATION_SITE_AUDIT.md"
HARNESS_PATH = ROOT / "tests/phase28/oracle_r7_005_1/oracle_harness.py"

spec = importlib.util.spec_from_file_location("oracle_harness", HARNESS_PATH)
harness = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = harness
spec.loader.exec_module(harness)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


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

    compiler = Path(sys.argv[1]).resolve()
    source_path = Path(sys.argv[2]).resolve()
    assert compiler.is_file() and os.access(compiler, os.X_OK), compiler
    assert sha256(source_path) == artifact["source_sha256"]

    runtime_path = ROOT / "src/executor_runtime.cpp"
    slice_path = ROOT / "src/char_string_ordering_runtime.cpp"
    assert sha256(runtime_path) == evidence["source_files"]["src/executor_runtime.cpp"]
    assert sha256(slice_path) == evidence["source_files"]["src/char_string_ordering_runtime.cpp"]
    for site in sites:
        rel, line_text = site["source"].split(":")
        line = int(line_text)
        source_lines = (ROOT / rel).read_text().splitlines()
        assert site["rust_expression"] in source_lines[line - 1], site

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
        assert hashlib.sha256(rust_bytes).hexdigest() == artifact["rust_sha256"]
        rust = rust_bytes.decode("utf-8")
        rust_lines = rust.splitlines()

        expected_clone_lines = [site["rust_line"] for site in sites] + [3448]
        clone_occurrences = harness.clone_occurrences(rust)
        observed_clone_lines = [occurrence["start"]["line"] for occurrence in clone_occurrences]
        assert observed_clone_lines == expected_clone_lines, observed_clone_lines
        for site in sites:
            assert site["rust_expression"] in rust_lines[site["rust_line"] - 1], site
        assert "(*value).clone()" in rust_lines[3448 - 1]

        # Cross-check declared runtime types against the generated artifact,
        # rather than trusting the prose/JSON type labels alone.
        for type_snippet in (
                "active_executor: std::option::Option<std::sync::Arc<MossGlobalExec>>",
                "pub affinity: std::option::Option<std::vec::Vec<usize>>",
                "state: std::sync::Arc<MossBranchScopeState>",
                "static MOSS_CURRENT_WORKER_EXEC: std::cell::RefCell<std::option::Option<std::sync::Arc<MossGlobalExec>>>"):
            assert type_snippet in rust, type_snippet
        assert "pub priority: std::option::Option<i32>" in rust

        for item in slices:
            assert item["rust_expression"] in rust_lines[item["rust_line"] - 1], item

        authority = harness.query_copy_authority(compiler, source_path)
        assert isinstance(authority, dict) and authority.get("status") == "available", authority
        audit = harness.audit_clone_provenance(rust_path, source_path, "debug", 1, authority)
        assert (audit.get("status"), audit.get("authorized"), audit.get("unauthorized"),
                audit.get("unclassified")) == ("UNAUTHORIZED", 1, 6, 6), audit
        sidecar = json.loads(Path(str(rust_path) + ".mossclone.json").read_text())
        assert [row["origin_kind"] for row in sidecar["occurrences"]].count("explicit_copy") == 1
        assert [row["origin_kind"] for row in sidecar["occurrences"]].count("unclassified") == 6

    print("phase28 R7-010 ORACLE-10A runtime clone evidence: PASS "
          "(pinned artifact bytes; 1 checked copy, 6 unauthorized/unclassified runtime clones; "
          "2 string materializers; 0 raised-error clone calls)")


if __name__ == "__main__":
    main()
