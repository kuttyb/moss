#!/usr/bin/env python3
"""Keep ORACLE-10A runtime clone evidence distinct from authorization."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "docs/phase28/team-c/P28_R7_010_ORACLE10A_RUNTIME_CLONE_EVIDENCE.json"
REPORT = ROOT / "docs/phase28/team-c/P28_R7_010_GENERATION_SITE_AUDIT.md"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
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

    print("phase28 R7-010 ORACLE-10A runtime clone evidence: PASS "
          "(6 runtime sites and 2 string materializers remain unclassified)")


if __name__ == "__main__":
    main()
