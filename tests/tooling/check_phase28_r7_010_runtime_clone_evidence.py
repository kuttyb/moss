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
RUNTIME_INVENTORY = ROOT / "tests/phase28/oracle_r7_005_1/runtime_clone_site_inventory.json"

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


def normalized_marker_stripped_digest(rust_bytes: bytes) -> str:
    stripped = re.sub(
        rb"/\* MOSS_RUNTIME_CLONE_SITE:r7-010-runtime-\d{2} \*/\s*", b"", rust_bytes)
    helper_ids = re.findall(rb"__moss_copy_value_([0-9a-f]{16})", stripped)
    fact_ids = re.findall(rb"typed-copy-fact-v1:([0-9a-f]{64})", stripped)
    assert len(set(helper_ids)) == len(fact_ids) == 1 and set(helper_ids) == {fact_ids[0][:16]}, (
        helper_ids, fact_ids)
    normalized = re.sub(rb"__moss_copy_value_[0-9a-f]{16}",
                        b"__moss_copy_value_<stable-id>", stripped)
    normalized = re.sub(rb"typed-copy-fact-v1:[0-9a-f]{64}",
                        b"typed-copy-fact-v1:<stable-id>", normalized)
    return hashlib.sha256(normalized).hexdigest()


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
    assert len(operator_spans["qualified_clone"]) == 7


def audit_runtime_sidecar(rows: list[dict], rust: str) -> None:
    inventory = json.loads(RUNTIME_INVENTORY.read_text())
    assert inventory["schema"] == "moss-runtime-clone-site-inventory-v1"
    expected = {row["site_id"]: row for row in inventory["sites"]}
    reviewed_types = {
        "r7-010-runtime-01": "Option<Arc<MossGlobalExec>>",
        "r7-010-runtime-02": "Option<Vec<usize>>",
        "r7-010-runtime-03": "MossBranchScope",
        "r7-010-runtime-04": "Arc<MossFairNode>",
        "r7-010-runtime-05": "Arc<MossFairWaiter>",
        "r7-010-runtime-06": "Arc<MossBranchScopeState>",
        "r7-010-runtime-08": "Arc<MossGlobalExec>",
        "r7-010-runtime-09": "Arc<MossGlobalExec>",
        "r7-010-runtime-10": "Arc<(Mutex<Option<R>>, Condvar)>",
        "r7-010-runtime-11": "Arc<MossGlobalExec>",
        "file-chunk-01": "Arc<Mutex<Option<ChunkOutcome>>>",
        "fileio-read-borrow-01": "Arc<MossFairMutex<Option<FileIOInner>>>",
    }
    assert {key: row["semantic_type"] for key, row in expected.items()} == reviewed_types
    generated_type_evidence = {
        "r7-010-runtime-01": "active_executor: std::option::Option<std::sync::Arc<MossGlobalExec>>",
        "r7-010-runtime-02": "affinity: std::option::Option<std::vec::Vec<usize>>",
        "r7-010-runtime-03": "pub struct MossBranchScope",
        "r7-010-runtime-04": "node: std::sync::Arc<MossFairNode>",
        "r7-010-runtime-05": "std::collections::VecDeque<std::sync::Arc<MossFairWaiter>>",
        "r7-010-runtime-06": "std::sync::Arc<MossBranchScopeState>",
        "r7-010-runtime-08": "std::sync::Arc<MossGlobalExec>",
        "r7-010-runtime-09": "active_executor: std::option::Option<std::sync::Arc<MossGlobalExec>>",
        "r7-010-runtime-10": "std::sync::Arc<(std::sync::Mutex<std::option::Option<R>>, std::sync::Condvar)>",
        "r7-010-runtime-11": "active_executor: std::option::Option<std::sync::Arc<MossGlobalExec>>",
        "file-chunk-01": "std::vec::Vec<std::sync::Arc<std::sync::Mutex<Option<__MossChunkOutcome_",
        "fileio-read-borrow-01": "inner: Arc<MossFairMutex<Option<FileIOInner>>>",
    }
    observed = {(row["site_id"], row["generated_occurrence"]): row for row in rows}
    assert len(observed) == len(rows), "duplicate sidecar runtime site"
    required = {site_id for site_id in expected if site_id.startswith("r7-010-runtime-")}
    if "MossFileIOReadBorrow" in rust:
        required.add("fileio-read-borrow-01")
    chunk_expansions = rust.count("Moss backend: FileIO CHUNK PIPELINE -- eligible")
    if chunk_expansions:
        required.add("file-chunk-01")
    observed_ids = {site_id for site_id, _ in observed}
    assert required <= observed_ids, f"missing runtime site observations: {required - observed_ids}"
    for site_id in observed_ids:
        indices = sorted(index for observed_id, index in observed if observed_id == site_id)
        assert indices == list(range(len(indices))), (site_id, indices)
        assert site_id == "file-chunk-01" or indices == [0], (site_id, indices)
        if site_id == "file-chunk-01":
            assert indices == list(range(chunk_expansions)), (site_id, indices, chunk_expansions)
    lines = rust.splitlines(keepends=True)
    markers = {}
    for match in re.finditer(r"/\* MOSS_RUNTIME_CLONE_SITE:([^ ]+) \*/", rust):
        markers.setdefault(match.group(1), []).append(match.start())
    def offset(point: dict) -> int:
        line, col = point["line"], point["column"]
        assert 1 <= line <= len(lines) and col >= 1
        return sum(map(len, lines[:line - 1])) + col - 1
    for (site_id, _), row in observed.items():
        spec = expected[site_id]
        assert row["provenance_status"] == "source_site_only_unapproved"
        assert row["source_file"] == spec["source_file"]
        assert row["source_sha256"] == spec["source_sha256"]
        assert row["source_marker"] == spec["marker"]
        assert row["semantic_type"] == spec["semantic_type"]
        assert row["operator_kind"] == spec["operator_kind"]
        assert row["expected_expression"] == spec["expected_expression"]
        assert generated_type_evidence[site_id] in rust, (
            f"generated Rust type does not match reviewed source-site type: {site_id}")
        assert row["source_span"] == spec["source_span"]
        span = row["rust_span"]
        context = row["rust_context_span"]
        begin, end = offset(span["start"]), offset(span["end"])
        context_begin, context_end = offset(context["start"]), offset(context["end"])
        assert 0 <= context_begin <= begin < end <= context_end <= len(rust)
        operator = rust[begin:end]
        assert (operator == ".clone()" if row["operator_kind"] == "direct_method"
                else operator.startswith("Arc::clone(") and operator.endswith(")"))
        assert hashlib.sha256(rust[context_begin:context_end].encode()).hexdigest() == row["rust_context_sha256"]
        marker_positions = markers.get(site_id, [])
        site_rows = sorted((candidate for key, candidate in observed.items()
                            if key[0] == site_id),
                           key=lambda candidate: candidate["generated_occurrence"])
        assert len(marker_positions) == len(site_rows), (site_id, marker_positions, site_rows)
        marker_at = marker_positions[row["generated_occurrence"]]
        assert rust.rfind("\n", 0, marker_at) == rust.rfind("\n", 0, begin)
        assert marker_at < begin
        source = (ROOT / spec["source_file"]).read_bytes()
        assert hashlib.sha256(source).hexdigest() == spec["source_sha256"]
        begin, end = spec["source_span"]["source_begin"], spec["source_span"]["source_end"]
        assert source[begin:end] == f"/* {spec['marker']} */".encode()
    for mutate in (
        lambda values: values[:-1],
        lambda values: values + [values[0]],
        lambda values: [{**values[0], "source_sha256": "0" * 64}, *values[1:]],
        lambda values: [{**values[0], "source_span": {**values[0]["source_span"], "source_begin": 0}}, *values[1:]],
        lambda values: [{**values[0], "semantic_type": "Arc<Vec<u8>>"}, *values[1:]],
        lambda values: [{**values[0], "operator_kind": "direct_method"}, *values[1:]],
    ):
        try:
            harness.validate_runtime_clone_site_observations(mutate(rows), rust)
        except ValueError:
            pass
        else:
            raise AssertionError("runtime sidecar mutation was accepted")


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
        assert "MossWorkerContextGuard::enter(worker_id, gx)" in rust
        assert "Cell<*const std::sync::Arc<MossGlobalExec>>" in rust
        assert "MOSS_RUNTIME_CLONE_SITE:r7-010-runtime-07" not in rust
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

        # Copy-helper identities include the absolute Moss source path, so a
        # raw artifact digest changes across isolated checkout paths. Keep
        # exact helper/fact identity matching and normalize only those tokens.
        normalized_digest = normalized_marker_stripped_digest(rust_bytes)
        assert normalized_digest == (
            "cf0bd5e74c782d1e860d77345d1908e3a0cf6eb881d16f27139a81bdddd34329")

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

        # Rebuild identical Moss bytes under another checkout-like source path.
        # Semantic helper/fact identity is source-content-bound and therefore
        # stable, while each serialized fact must still retain its exact path
        # and be checked against that artifact's source bytes.
        alternate_source = Path(tmp) / "alternate-checkout/tests/phase28/oracle_r7_005_1/fixtures/oracle_10a_explicit_copy_only.moss"
        alternate_source.parent.mkdir(parents=True)
        alternate_source.write_bytes(source_path.read_bytes())
        alternate_rust_path = Path(tmp) / "alternate-checkout/oracle-10a.rs"
        alternate_build = subprocess.run([str(compiler), str(alternate_source), "-o",
                                          str(alternate_rust_path)], capture_output=True,
                                         text=True, timeout=120)
        assert alternate_build.returncode == 0, (alternate_build.stdout, alternate_build.stderr)
        alternate_rust_bytes = alternate_rust_path.read_bytes()
        assert normalized_marker_stripped_digest(alternate_rust_bytes) == normalized_digest
        raw_runtime_stripped = re.sub(
            rb"/\* MOSS_RUNTIME_CLONE_SITE:r7-010-runtime-\d{2} \*/\s*", b"", rust_bytes)
        raw_alternate_stripped = re.sub(
            rb"/\* MOSS_RUNTIME_CLONE_SITE:r7-010-runtime-\d{2} \*/\s*", b"", alternate_rust_bytes)
        primary_fact_ids = re.findall(
            rb"typed-copy-fact-v1:([0-9a-f]{64})", raw_runtime_stripped)
        alternate_fact_ids = re.findall(
            rb"typed-copy-fact-v1:([0-9a-f]{64})", raw_alternate_stripped)
        assert primary_fact_ids == alternate_fact_ids, (
            "identical source occurrences must retain the same checker-fact/helper identity")
        assert re.findall(rb"__moss_copy_value_[0-9a-f]{16}", raw_runtime_stripped) == \
            re.findall(rb"__moss_copy_value_[0-9a-f]{16}", raw_alternate_stripped)
        alternate_copy_authority = harness.query_copy_authority(compiler, alternate_source)
        primary_copy_authority = harness.query_copy_authority(compiler, source_path)
        assert primary_copy_authority["facts"][0]["source_file"] == str(source_path.resolve())
        assert alternate_copy_authority["facts"][0]["source_file"] == str(alternate_source.resolve())
        assert primary_copy_authority["facts"][0]["source_file"] != \
            alternate_copy_authority["facts"][0]["source_file"]
        assert harness.typed_copy_fact_identity(primary_copy_authority["facts"][0]) == \
            harness.typed_copy_fact_identity(alternate_copy_authority["facts"][0])
        alternate_ingress_authority = harness.query_root_ingress_capture_authority(
            compiler, alternate_source)
        alternate_audit = harness.audit_clone_provenance(
            alternate_rust_path, alternate_source, "debug", 1,
            alternate_copy_authority, alternate_ingress_authority)
        assert (alternate_audit.get("status"), alternate_audit.get("authorized"),
                alternate_audit.get("unauthorized"), alternate_audit.get("unclassified")) == (
                    audit.get("status"), audit.get("authorized"), audit.get("unauthorized"),
                    audit.get("unclassified")), alternate_audit
        sidecar = json.loads(Path(str(rust_path) + ".mossclone.json").read_text())
        audit_runtime_sidecar(sidecar["runtime_clone_site_observations"], rust)
        assert [row["origin_kind"] for row in sidecar["occurrences"]].count("explicit_copy") == 1
        assert [row["origin_kind"] for row in sidecar["occurrences"]].count("unclassified") == 3

        # The generated FileChunk site is gensymmed. Its identity is bound by
        # the exact C++ source marker and by the result-slot / branch scope
        # shape, not by a guessed variable name.
        chunk_source = ROOT / "tests/tooling/fixtures/phase21_chunk_fileio_raise.moss"
        chunk_rust_path = Path(tmp) / "fileio-chunk.rs"
        chunk_build = subprocess.run([str(compiler), str(chunk_source), "-o", str(chunk_rust_path)],
                                     capture_output=True, text=True, timeout=120)
        assert chunk_build.returncode == 0, (chunk_build.stdout, chunk_build.stderr)
        chunk_rust = chunk_rust_path.read_text()
        chunk_sidecar = json.loads(Path(str(chunk_rust_path) + ".mossclone.json").read_text())
        chunk_rows = chunk_sidecar["runtime_clone_site_observations"]
        audit_runtime_sidecar(chunk_rows, chunk_rust)
        assert {row["site_id"] for row in chunk_rows} >= {
            "file-chunk-01", "fileio-read-borrow-01"}
        (ROOT / "tmp/phase21-chunk-fileio-raise.data").write_bytes(b"abc")
        chunk_binary = Path(tmp) / "fileio-chunk-bin"
        rustc = subprocess.run(["rustc", "--edition", "2021", "--cfg", "moss_perf",
                                str(chunk_rust_path), "-o", str(chunk_binary)],
                               capture_output=True, text=True, timeout=120)
        assert rustc.returncode == 0, (rustc.stdout, rustc.stderr)
        chunk_run = subprocess.run([str(chunk_binary)], cwd=ROOT,
                                   capture_output=True, text=True, timeout=60)
        assert chunk_run.returncode == 0 and chunk_run.stdout.strip() == "3", (
            chunk_run.returncode, chunk_run.stdout, chunk_run.stderr)
        for changed in (
            [{**row, "rust_context_sha256": "0" * 64} if row["site_id"] == "file-chunk-01" else row
             for row in chunk_rows],
            [row for row in chunk_rows if row["site_id"] != "file-chunk-01"],
        ):
            try:
                harness.validate_runtime_clone_site_observations(changed, chunk_rust)
            except ValueError:
                pass
            else:
                raise AssertionError("FileChunk sidecar context/coverage corruption was accepted")
        chunk_marker = "/* MOSS_RUNTIME_CLONE_SITE:file-chunk-01 */"
        fileio_call = "/* MOSS_RUNTIME_CLONE_SITE:fileio-read-borrow-01 */ Arc::clone(&file.inner)"
        relocated_chunk_rust = chunk_rust.replace(chunk_marker, "", 1).replace(
            fileio_call, chunk_marker + " " + fileio_call, 1)
        try:
            harness.validate_runtime_clone_site_observations(chunk_rows, relocated_chunk_rust)
        except ValueError:
            pass
        else:
            raise AssertionError("FileChunk marker relocation to another Arc call was accepted")

        multi_source = ROOT / "tests/tooling/fixtures/phase21c_native_ordered_raise.moss"
        multi_rust_path = Path(tmp) / "fileio-chunk-multiple.rs"
        multi_build = subprocess.run([str(compiler), str(multi_source), "-o", str(multi_rust_path)],
                                     capture_output=True, text=True, timeout=120)
        assert multi_build.returncode == 0, (multi_build.stdout, multi_build.stderr)
        multi_rust = multi_rust_path.read_text()
        multi_sidecar = json.loads(Path(str(multi_rust_path) + ".mossclone.json").read_text())
        multi_rows = multi_sidecar["runtime_clone_site_observations"]
        audit_runtime_sidecar(multi_rows, multi_rust)
        multi_expansions = sorted(row["generated_occurrence"] for row in multi_rows
                                  if row["site_id"] == "file-chunk-01")
        assert multi_expansions == [0, 1], multi_expansions
        try:
            harness.validate_runtime_clone_site_observations(
                [row for row in multi_rows
                 if not (row["site_id"] == "file-chunk-01" and
                         row["generated_occurrence"] == 1)], multi_rust)
        except ValueError:
            pass
        else:
            raise AssertionError("dropping one repeated FileChunk expansion was accepted")

    print("phase28 R7-010 runtime clone evidence: PASS "
          "(historical artifact record validated; current output has 10 marked runtime sites, "
          "1 checked copy and 3 unauthorized/unclassified direct clones; markers grant no authorization)")


if __name__ == "__main__":
    main()
