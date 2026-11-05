#!/usr/bin/env python3
"""Phase 28 draft independent semantic oracle suite harness.

Evaluates the draft Phase 28 expectation table across:
1. Checker semantic verification
2. Fast Debug interpreted execution
3. Native Debug compilation and execution
4. Native Optimized (-O) compilation and execution
5. Occurrence-level native clone provenance validation
6. Structured rejection diagnostics tied to the fixture's source construct

Specification Authority:
tests/phase28/oracle/expectations.json (draft or certified frozen)
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
FIXTURES_DIR = Path(__file__).resolve().parent / "fixtures"
DEFAULT_TMP = ROOT / "tmp/phase28_oracle_r7_005_1"
MARGO = ROOT / "margo"

# Specification expectations and diagnostic normalization are independent oracle-owned data.
_expectation_doc = json.loads((Path(__file__).resolve().parent / "expectations.json").read_text())
_diagnostic_doc = json.loads((Path(__file__).resolve().parent / "diagnostic_mapping.json").read_text())
_runtime_callsite_doc = json.loads((Path(__file__).resolve().parent / "runtime_callsite_allowlist.json").read_text())
_certification_path = Path(__file__).resolve().parent / "certification.json"
_certification_doc = json.loads(_certification_path.read_text()) if _certification_path.exists() else None
_runtime_callsites = {item["callsite_id"]: item for item in _runtime_callsite_doc.get("callsites", [])}
_fixture_files = {
    "ORACLE-01-VEC-WRITE-THROUGH": "oracle_01_write_through_vector.moss",
    "ORACLE-02-INDEXED-FIELD-READ-NO-CLONE": "oracle_02_indexed_field_read_no_clone.moss",
    "ORACLE-03-COLL-ELEM-CONSUME-REJECT": "oracle_03_collection_element_consume_rejected.moss",
    "ORACLE-04-MAP-WRITE-THROUGH": "oracle_04_write_through_map_entry.moss",
    "ORACLE-05-MAP-GET-BORROW": "oracle_05_map_get_borrowed.moss",
    "ORACLE-06-MATCH-CONSUME-OPT-NATIVE": "oracle_06_match_consume_option_native.moss",
    "ORACLE-07-CATCHABLE-COLL-ERRORS": "oracle_07_catchable_collection_errors.moss",
    "ORACLE-08-USER-COPY-RESOLUTION": "oracle_08_user_defined_copy_resolution.moss",
    "ORACLE-09-USER-ENUM-SOME-NONE": "oracle_09_user_enum_some_none.moss",
    "ORACLE-10A-EXPLICIT-COPY-ONLY": "oracle_10a_explicit_copy_only.moss",
    "ORACLE-10B-CLONE-ALIAS-REJECTED": "oracle_10b_clone_alias_rejected.moss",
    "ORACLE-11-ASSIGN-READ-BORROW": "oracle_11_assignment_read_borrow.moss",
    "ORACLE-12-ASSIGN-WRITE-BORROW": "oracle_12_assignment_write_borrow.moss",
    "ORACLE-13-ASSIGN-CONSUME-RETURN": "oracle_13_assignment_consume_return.moss",
    "ORACLE-14-ASSIGN-CONSUME-BORROW-REJECT": "oracle_14_assignment_consume_borrowed_rejected.moss",
    "ORACLE-15-PLAIN-FIELD-CONSUME-REJECT": "oracle_15_plain_field_access_consume_rejected.moss",
    "ORACLE-16-TAKE-FRONTIER-ARCHIVE": "oracle_16_take_frontier_archive.moss",
    "ORACLE-17-TAKE-OPTION-FIELD": "oracle_17_take_option_field.moss",
    "ORACLE-18-TAKE-QUEUE-FIELD": "oracle_18_take_queue_field.moss",
    "ORACLE-19-TAKE-NESTED-VEC-OPTION": "oracle_19_take_nested_vector_option.moss",
    "ORACLE-20-TAKE-NESTED-MAP-VECTOR": "oracle_20_take_nested_map_vector.moss",
    "ORACLE-21-TAKE-LOCAL-REJECTED": "oracle_21_take_local_rejected.moss",
    "ORACLE-22-TAKE-UNIFIED-FIELD-REJECT": "oracle_22_take_unified_field_rejected.moss",
    "ORACLE-23-TAKE-VEC-ELEM-REJECTED": "oracle_23_take_vector_element_rejected.moss",
    "ORACLE-24-TAKE-CUSTOM-SLOT-REJECT": "oracle_24_take_custom_slot_rejected.moss",
    "ORACLE-25-TAKE-BEFORE-RAISE-MUTATION": "oracle_25_take_before_raise_no_rollback.moss",
    "ORACLE-26-VEC-REMOVE-API": "oracle_26_vector_remove_api.moss",
    "ORACLE-27-REBIND-AFTER-BORROWS": "oracle_27_rebind_after_borrows_end.moss",
    "ORACLE-28-REBIND-WHILE-BORROWED-REJECT": "oracle_28_rebind_while_borrowed_rejected.moss",
    "ORACLE-29-LOOP-CARRIED-OWNERSHIP": "oracle_29_loop_carried_ownership_rebind.moss",
    "ORACLE-30-JOIN-BORROW-FRESH-REJECT": "oracle_30_join_borrowed_and_fresh_rejected.moss",
    "ORACLE-31-JOIN-PROMOTED-CONSUME": "oracle_31_join_promoted_consume.moss",
    "ORACLE-32-MATCH-PAYLOAD-WRITE": "oracle_32_match_payload_write_borrow.moss",
    "ORACLE-33-MATCH-LAST-USE-OVERWRITE": "oracle_33_match_payload_last_use_overwrite.moss",
    "ORACLE-34-MATCH-OVERWRITE-WHILE-BORROWED-REJECT": "oracle_34_match_payload_overwrite_while_borrowed_rejected.moss",
    "ORACLE-35-MATCH-ORDINARY-CONSUME-REJECT": "oracle_35_match_ordinary_consume_rejected.moss",
    "ORACLE-36-MATCH-CONSUME-QUEUE-POP": "oracle_36_match_consume_queue_pop.moss",
    "ORACLE-37-MATCH-CONSUME-UNIFIED-FIELD-REJECT": "oracle_37_match_consume_unified_field_rejected.moss",
    "ORACLE-38-MAP-VALUES-BORROWED-VIEW": "oracle_38_map_values_borrowed_view.moss",
    "ORACLE-39-VEC-FOR-READ": "oracle_39_vector_for_read.moss",
    "ORACLE-40-VEC-FOR-WRITE": "oracle_40_vector_for_write.moss",
    "ORACLE-41-VEC-FOR-CONSUME-REJECT": "oracle_41_vector_for_consume_rejected.moss",
    "ORACLE-42-CUSTOM-WRAPPER-REMOVE": "oracle_42_custom_wrapper_remove.moss",
    "ORACLE-43-HANDLER-PRIMITIVE-REPLY": "oracle_43_handler_primitive_reply.moss",
    "ORACLE-44-HANDLER-PRIMITIVE-MUTATE-REJECT": "oracle_44_handler_primitive_mutate_rejected.moss",
    "ORACLE-45-HANDLER-PRIMITIVE-REBIND-LOCAL": "oracle_45_handler_primitive_rebind_local.moss",
    "ORACLE-46-HANDLER-NONPRIM-REPLY-REJECT": "oracle_46_handler_nonprimitive_reply_rejected.moss",
}
ORACLE_FIXTURES = []
for _expectation in _expectation_doc["fixtures"]:
    _fixture_id = _expectation["fixture_id"]
    _expected = _expectation["expected"]
    _diagnostic = _diagnostic_doc["mappings"].get(_fixture_id, {})
    ORACLE_FIXTURES.append({
        "id": _fixture_id,
        "defect_id": _expectation["defect_id"],
        "name": _fixture_id,
        "file": _expectation.get("fixture_file", _fixture_files.get(_fixture_id)),
        "category": "oracle",
        "spec_citation": _expectation["specification_basis"],
        "kind": "REJECTED" if _expectation["expectation"] == "REJECT" else "ACCEPTED",
        **({"expected_diagnostic": _diagnostic.get("primary", _diagnostic.get("normalized")), "alternative_diagnostics": _diagnostic.get("accepted_mappings", [])} if _diagnostic else {}),
        "expected": _expected,
        **({
            "expected_fast_debug": _expected["fast_debug"]["stdout"],
            "expected_native": _expected["native_debug"]["stdout"],
            "expected_fast_debug_status": _expected["fast_debug"]["status"],
            "expected_native_status": _expected["native_debug"]["status"],
            "expected_native_opt_status": _expected["native_optimized"]["status"],
            "authorized_explicit_copies": _expected["clone_audit"]["authorized_copy_calls"],
            "max_hidden_clones": _expected["clone_audit"]["max_unauthorized"],
        } if _expectation["expectation"] == "ACCEPT" else {}),
    })


def validate_mutation_assertions(fixture: dict) -> None:
    expected = fixture["expected"]
    fixture_id = fixture.get("id", fixture.get("fixture_id", "<unknown>"))
    assertions = expected.get("observable_mutations", [])
    for assertion in assertions:
        line_number = assertion.get("stdout_line")
        if not isinstance(line_number, int) or line_number < 1:
            raise ValueError(f"invalid observable mutation stdout line in {fixture_id}")
        for engine in ("fast_debug", "native_debug", "native_optimized"):
            stdout = expected[engine].get("stdout")
            lines = stdout.splitlines() if isinstance(stdout, str) else []
            actual = lines[line_number - 1] if line_number <= len(lines) else None
            if actual != assertion.get("expected"):
                raise ValueError(f"observable mutation expectation contradicts {engine} stdout in {fixture_id}: "
                                 f"line {line_number} is {actual!r}, assertion says {assertion.get('expected')!r}")


def validate_oracle_data() -> None:
    oracle_version = _expectation_doc.get("version")
    if oracle_version != 2 or len(_expectation_doc.get("fixtures", [])) != 60:
        raise ValueError("R7-005.1 package must be version 2 with exactly 60 fixtures")
    if _diagnostic_doc.get("version") != oracle_version:
        raise ValueError("diagnostic mapping version must match the expectation version")
    expectation_status = _expectation_doc.get("approval_status")
    mapping_status = _diagnostic_doc.get("approval_status")
    runtime_status = _runtime_callsite_doc.get("approval_status")
    if expectation_status == "CERTIFIED_FROZEN":
        if not isinstance(_certification_doc, dict):
            raise ValueError("certified expectation table requires digest-pinned certification.json")
        if (_certification_doc.get("format") != "moss-phase28-oracle-certification" or
                _certification_doc.get("version") != oracle_version):
            raise ValueError("unsupported oracle certification record")
        if _certification_doc.get("reviewer_verdict") != "GO" or not _certification_doc.get("approval_candidate_sha"):
            raise ValueError("certification record lacks approved candidate SHA or independent GO")
        if _certification_doc.get("holdout_result") != "PASS":
            raise ValueError("certification record lacks a passing reviewer-controlled holdout")
        expected_hash = _certification_doc.get("expectations_sha256")
        actual_hash = hashlib.sha256((Path(__file__).resolve().parent / "expectations.json").read_bytes()).hexdigest()
        if expected_hash != actual_hash:
            raise ValueError("frozen expectations digest mismatch; new L7 review is required")
        runtime_hash = _certification_doc.get("runtime_callsite_allowlist_sha256")
        actual_runtime_hash = hashlib.sha256((Path(__file__).resolve().parent / "runtime_callsite_allowlist.json").read_bytes()).hexdigest()
        if runtime_hash != actual_runtime_hash:
            raise ValueError("frozen runtime callsite registry digest mismatch; new independent review is required")
        if not _certification_doc.get("frozen_sha"):
            raise ValueError("certification record lacks exact frozen SHA")
        if runtime_status != "CERTIFIED_FROZEN":
            raise ValueError("certified expectations require a certified-frozen runtime registry")
        if mapping_status != "CERTIFIED_FROZEN":
            raise ValueError("certified expectations require a certified-frozen diagnostic mapping")
    elif expectation_status != "DRAFT_PENDING_L7_APPROVAL" or mapping_status != "DRAFT_PENDING_L7_APPROVAL":
        raise ValueError("expectation table and diagnostic mapping must share a supported approval state")
    if (_runtime_callsite_doc.get("format") != "moss-runtime-clone-callsites" or
            _runtime_callsite_doc.get("version") != 1 or
            runtime_status not in {"DRAFT_PENDING_INDEPENDENT_REVIEW", "CERTIFIED_FROZEN"} or
            _runtime_callsite_doc.get("owner") != "oracle-maintainers"):
        raise ValueError("runtime callsite classifications must remain in the reviewed oracle-owned registry")
    callsite_ids = [row.get("callsite_id") for row in _runtime_callsite_doc.get("callsites", [])]
    if len(callsite_ids) != len(set(callsite_ids)):
        raise ValueError("runtime callsite registry contains duplicate identities")
    for row in _runtime_callsite_doc.get("callsites", []):
        required = ("callsite_id", "origin_kind", "repository_evidence_path",
                    "repository_evidence_sha256", "repository_evidence_span",
                    "semantic_type", "ownership_fact", "semantic_identity",
                    "independent_reviewer", "review_record_path", "review_record_sha256")
        if any(not row.get(key) for key in required) or row.get("review_status") != "INDEPENDENTLY_REVIEWED":
            raise ValueError("runtime callsite registry entry lacks independent review or exact evidence")
        if row["origin_kind"] not in {"compiler_runtime_handle", "runtime_support"}:
            raise ValueError("runtime callsite registry has an unsupported origin kind")
        if row["independent_reviewer"] == _runtime_callsite_doc["owner"]:
            raise ValueError("runtime callsite registry cannot self-review its classifications")
        evidence_path = (ROOT / row["repository_evidence_path"]).resolve()
        if ROOT.resolve() not in evidence_path.parents or not evidence_path.is_file():
            raise ValueError("runtime callsite evidence must reference a repository file")
        evidence_bytes = evidence_path.read_bytes()
        if hashlib.sha256(evidence_bytes).hexdigest() != row["repository_evidence_sha256"]:
            raise ValueError(f"runtime callsite source digest is stale: {row['callsite_id']}")
        _span_offsets(evidence_bytes.decode(), row["repository_evidence_span"])
        review_path = (ROOT / row["review_record_path"]).resolve()
        if ROOT.resolve() not in review_path.parents or not review_path.is_file():
            raise ValueError("runtime callsite review record must be a repository file")
        review_bytes = review_path.read_bytes()
        if hashlib.sha256(review_bytes).hexdigest() != row["review_record_sha256"]:
            raise ValueError(f"runtime callsite review record digest is stale: {row['callsite_id']}")
        if row["callsite_id"].encode() not in review_bytes:
            raise ValueError("runtime callsite review record does not identify its callsite")
    ids = [item["id"] for item in ORACLE_FIXTURES]
    defects = [item["defect_id"] for item in ORACLE_FIXTURES]
    if len(ids) != len(set(ids)) or len(defects) != len(set(defects)):
        raise ValueError("fixture and permanent defect identifiers must be unique")
    rust_probe = '// ignored.clone()\nlet a = "ignored.clone()"; let b = r#"ignored.clone()"#; value.clone();'
    if len(clone_occurrences(rust_probe)) != 1:
        raise ValueError("Rust clone scanner must ignore comments and string literals")
    moss_probe = '# copy(ignored)\ntext = "copy(ignored)"\nvalue = copy(source)\n'
    if len(moss_copy_call_spans(moss_probe)) != 1:
        raise ValueError("Moss copy scanner must ignore comments and string literals")
    nested_probe = "value = copy(copy(x))\n"
    nested_spans = moss_copy_call_spans(nested_probe)
    if (len(nested_spans) != 2 or len({item["copy_id"] for item in nested_spans}) != 2 or
            nested_spans[0]["copy_id"] == nested_spans[1]["copy_id"]):
        raise ValueError("Moss copy scanner must independently identify nested calls")
    for item in ORACLE_FIXTURES:
        path = FIXTURES_DIR / item["file"]
        if not path.exists():
            raise ValueError(f"fixture path is missing: {item['id']} -> {path}")
        if item["kind"] == "REJECTED":
            spec = _diagnostic_doc["mappings"].get(item["id"])
            required = ("semantic_category", "codes", "severity", "message_regex", "source_line_regex")
            if not isinstance(spec, dict) or any(not spec.get(key) for key in required):
                raise ValueError(f"rejected fixture lacks structured oracle diagnostic contract: {item['id']}")
        if item["kind"] == "ACCEPTED":
            expected = item["expected"]
            for engine in ("fast_debug", "native_debug", "native_optimized"):
                if engine not in expected or expected[engine].get("status") not in {"execute", "panic"}:
                    raise ValueError(f"fixture lacks a specification-based {engine} result: {item['id']}")
            if "clone_audit" not in expected:
                raise ValueError(f"fixture lacks clone-audit expectation: {item['id']}")
            validate_mutation_assertions(item)
            source_files = list(path.rglob("*.moss")) if path.is_dir() else [path]
            source_copy_count = sum(len(moss_copy_call_spans(source.read_text()))
                                    for source in source_files)
            declared_copy_count = expected["clone_audit"]["authorized_copy_calls"]
            if source_copy_count != declared_copy_count:
                raise ValueError(f"source/table copy occurrence mismatch for {item['id']}: "
                    f"source={source_copy_count}, table={declared_copy_count}")
def run_process(cmd: list[str], cwd: Path | None = None, timeout: int = 15,
                env: dict | None = None) -> tuple[int, str, str, float]:
    start = time.perf_counter()
    try:
        proc = subprocess.run(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
            cwd=str(cwd or ROOT), timeout=timeout, env=env
        )
        elapsed = time.perf_counter() - start
        return proc.returncode, proc.stdout, proc.stderr, elapsed
    except subprocess.TimeoutExpired:
        elapsed = time.perf_counter() - start
        return -1, "", "TIMEOUT", elapsed


def validate_rejection_diagnostic(payload: object, spec: dict,
                                  source_path: Path) -> tuple[bool, str]:
    """Validate the structured checker error and anchor its cause to Moss source."""
    if not isinstance(payload, dict) or payload.get("ok") is not False:
        return False, "checker output is not a structured rejection response"
    error = payload.get("error")
    if not isinstance(error, dict):
        return False, "structured response lacks error object"
    if error.get("severity") != spec.get("severity", "error"):
        return False, f"severity mismatch: {error.get('severity')!r}"
    accepted_codes = spec.get("codes", [spec.get("code")])
    if error.get("code") not in accepted_codes:
        return False, f"diagnostic code mismatch: {error.get('code')!r}"
    msg = error.get("message")
    if not isinstance(msg, str) or not re.search(spec["message_regex"], msg, re.I):
        return False, f"diagnostic message does not match {spec['message_regex']!r}"
    reported_file = error.get("source_file")
    if not isinstance(reported_file, str) or Path(reported_file).resolve() != source_path.resolve():
        return False, f"source location file mismatch: {reported_file!r}"
    line = error.get("line")
    column = error.get("column")
    lines = source_path.read_text().splitlines()
    if (not isinstance(line, int) or line < 1 or line > len(lines) or
            not isinstance(column, int) or column < 1 or column > len(lines[line - 1]) + 1):
        return False, f"invalid diagnostic source location: {line!r}:{column!r}"
    if not re.search(spec["source_line_regex"], lines[line - 1], re.I):
        return False, f"diagnostic line {line} does not identify the rejected construct"
    return True, f"{spec.get('semantic_category')}:{error['code']}@{line}"


def expected_panic_matches(exit_code: int, stdout: str, stderr: str,
                           patterns: list[str], expected_stdout: str = "") -> tuple[bool, str]:
    """Accept only a non-timeout runtime panic carrying the specified evidence."""
    if exit_code == -1 or stderr == "TIMEOUT":
        return False, "execution timed out"
    if exit_code == 0:
        return False, "program completed instead of panicking"
    if stdout.strip() != expected_stdout:
        return False, f"panic stdout mismatch: {stdout.strip()!r}"
    diagnostic = stderr + "\n" + stdout
    if not any(re.search(pattern, diagnostic, re.I) for pattern in patterns):
        return False, "runtime failure lacks indexing-panic evidence"
    return True, "indexing panic confirmed"


def _position(source: str, offset: int) -> dict:
    line = source.count("\n", 0, offset) + 1
    line_start = source.rfind("\n", 0, offset) + 1
    return {"line": line, "column": offset - line_start + 1}


def _span_offsets(source: str, span: dict) -> tuple[int, int]:
    def offset(pos: dict) -> int:
        line, column = pos["line"], pos["column"]
        if line < 1 or column < 1:
            raise ValueError("span coordinates must be 1-based")
        lines = source.splitlines(keepends=True)
        if line > len(lines) or column - 1 > len(lines[line - 1]):
            raise ValueError("span coordinate is outside artifact")
        return sum(len(part) for part in lines[:line - 1]) + column - 1
    start, end = offset(span["start"]), offset(span["end"])
    if end < start:
        raise ValueError("span end precedes start")
    return start, end


def _span_key(span: dict) -> tuple[int, int, int, int]:
    return (span["start"]["line"], span["start"]["column"],
            span["end"]["line"], span["end"]["column"])


def moss_copy_call_spans(source: str) -> list[dict]:
    """Enumerate each copy(expr), including nested calls, with stable source IDs."""
    spans = []
    i = 0
    while i < len(source):
        if source[i] == "#":
            newline = source.find("\n", i)
            i = len(source) if newline < 0 else newline + 1
            continue
        if source[i] in ('"', "'"):
            quote = source[i]
            i += 1
            while i < len(source):
                if source[i] == "\\":
                    i += 2
                elif source[i] == quote:
                    i += 1
                    break
                else:
                    i += 1
            continue
        if source.startswith("copy", i) and (i == 0 or not (source[i - 1].isalnum() or source[i - 1] == "_")):
            end_name = i + 4
            if end_name == len(source) or not (source[end_name].isalnum() or source[end_name] == "_"):
                j = end_name
                while j < len(source) and source[j].isspace():
                    j += 1
                if j < len(source) and source[j] == "(":
                    depth = 0
                    k = j
                    quote = None
                    while k < len(source):
                        ch = source[k]
                        if quote:
                            if ch == "\\":
                                k += 2
                                continue
                            if ch == quote:
                                quote = None
                        elif ch in ('"', "'"):
                            quote = ch
                        elif ch == "#":
                            newline = source.find("\n", k)
                            k = len(source) if newline < 0 else newline
                        elif ch == "(":
                            depth += 1
                        elif ch == ")":
                            depth -= 1
                            if depth == 0:
                                k += 1
                                spans.append({"start": _position(source, i), "end": _position(source, k)})
                                # Continue inside this call so nested copy calls
                                # are independently enumerated.
                                i = end_name + 1
                                break
                        k += 1
                    else:
                        i = end_name
                    if i == end_name + 1:
                        continue
        i += 1
    spans.sort(key=lambda span: _span_key(span))
    for span in spans:
        start = span["start"]
        span["copy_id"] = f"copy@{start['line']}:{start['column']}"
    return spans


def clone_occurrences(rust: str) -> list[dict]:
    occurrences = []
    i = 0
    while i < len(rust):
        if rust.startswith("//", i):
            newline = rust.find("\n", i)
            i = len(rust) if newline < 0 else newline + 1
            continue
        if rust.startswith("/*", i):
            depth = 1
            i += 2
            while i < len(rust) and depth:
                if rust.startswith("/*", i):
                    depth += 1
                    i += 2
                elif rust.startswith("*/", i):
                    depth -= 1
                    i += 2
                else:
                    i += 1
            continue
        raw = re.match(r"r(#+)?\"", rust[i:])
        if raw:
            hashes = raw.group(1) or ""
            terminator = '"' + hashes
            start = i + raw.end()
            end = rust.find(terminator, start)
            i = len(rust) if end < 0 else end + len(terminator)
            continue
        if rust[i] == '"':
            i += 1
            while i < len(rust):
                if rust[i] == "\\":
                    i += 2
                elif rust[i] == '"':
                    i += 1
                    break
                else:
                    i += 1
            continue
        if rust[i] == "'":
            char_literal = re.match(r"'(?:\\.|[^'\\])'", rust[i:])
            if char_literal:
                i += char_literal.end()
                continue
        match = re.match(r"\.clone\s*\(\s*\)", rust[i:])
        if match:
            end = i + match.end()
            occurrences.append({"start": _position(rust, i), "end": _position(rust, end)})
            i = end
        else:
            i += 1
    return occurrences


def audit_clone_provenance(rust_path: Path, source_path: Path, mode: str,
                           expected_copy_calls: int) -> dict:
    """Require a bijective artifact map for every Rust clone occurrence."""
    if not rust_path.exists():
        return {"status": "RUST_MISSING", "authorized": 0, "unauthorized": 0, "unclassified": 0, "occurrences": []}
    rust = rust_path.read_text()
    source = source_path.read_text()
    rust_occurrences = clone_occurrences(rust)
    source_copies = moss_copy_call_spans(source)
    if len(source_copies) != expected_copy_calls:
        return {"status": "EXPECTATION_SOURCE_MISMATCH", "authorized": 0,
                "unauthorized": 0, "unclassified": len(rust_occurrences),
                "error": f"Moss source has {len(source_copies)} copy(...) calls; expectation table declares {expected_copy_calls}",
                "source_copy_spans": source_copies, "occurrences": rust_occurrences}
    # Exact convention: append `.mossclone.json` to the complete Rust path,
    # e.g. `generated.rs.mossclone.json`.
    sidecar = Path(str(rust_path) + ".mossclone.json")
    if not sidecar.exists():
        # Every generated artifact requires an attested v2 sidecar, even when
        # its current text contains no clone or explicit Moss copy call.
        return {"status": "PROVENANCE_MISSING", "authorized": 0,
                "unauthorized": 0, "unclassified": len(rust_occurrences),
                "source_copy_spans": source_copies, "occurrences": rust_occurrences}
    try:
        manifest = json.loads(sidecar.read_text())
        if manifest.get("format") != "moss-clone-provenance" or manifest.get("version") != 2:
            raise ValueError("unsupported clone provenance format")
        if manifest.get("mode") != mode:
            raise ValueError(f"mode mismatch: expected {mode}")
        if manifest.get("rust_sha256") != hashlib.sha256(rust.encode()).hexdigest():
            raise ValueError("generated Rust digest mismatch")
        if manifest.get("source_sha256") != hashlib.sha256(source.encode()).hexdigest():
            raise ValueError("Moss source digest mismatch")
        rows = manifest.get("occurrences")
        if not isinstance(rows, list):
            raise ValueError("occurrences is not an array")
        indexed = {}
        for row in rows:
            span = row.get("rust_span", {})
            key = _span_key(span)
            if key in indexed:
                raise ValueError(f"duplicate Rust occurrence mapping at {key}")
            indexed[key] = row
        rust_keys = {_span_key(x) for x in rust_occurrences}
        if set(indexed) != rust_keys:
            raise ValueError(f"incomplete or extra Rust clone mappings: rust={len(rust_keys)}, mapped={len(indexed)}")
        source_by_id = {}
        scanned_copy_ids = {_span_key(span): span["copy_id"] for span in source_copies}
        source_rows = manifest.get("source_copy_occurrences")
        if not isinstance(source_rows, list):
            raise ValueError("source_copy_occurrences is not an array")
        for source_row in source_rows:
            copy_id = source_row.get("copy_id")
            span = source_row.get("source_span", {})
            if not isinstance(copy_id, str) or not copy_id or copy_id in source_by_id:
                raise ValueError("missing or duplicate source copy identity")
            start, end = _span_offsets(source, span)
            if source[start:end] != "copy" and not re.fullmatch(r"copy\s*\(.*\)", source[start:end], re.S):
                raise ValueError(f"source copy span does not cover a copy(...) occurrence: {copy_id}")
            if scanned_copy_ids.get(_span_key(span)) != copy_id:
                raise ValueError(f"source copy identity does not match independent scanner: {copy_id}")
            if source_row.get("semantic_status") != "implemented":
                raise ValueError(f"source copy semantics not attested as implemented: {copy_id}")
            if not isinstance(source_row.get("rust_clone_spans"), list):
                raise ValueError(f"source copy lacks emitted clone list: {copy_id}")
            source_by_id[copy_id] = source_row
        source_keys = {_span_key(x) for x in source_copies}
        manifest_source_keys = {_span_key(x["source_span"]) for x in source_rows}
        if manifest_source_keys != source_keys or len(source_rows) != len(source_copies):
            raise ValueError("source-copy manifest does not exactly cover Moss copy(...) occurrences")
        explicit_by_id: dict[str, list[tuple[int, int, int, int]]] = {key: [] for key in source_by_id}
        unauthorized = 0
        unclassified = 0
        rust_by_key = {_span_key(x): x for x in rust_occurrences}
        for row in indexed.values():
            origin = row.get("origin_kind")
            evidence = row.get("evidence")
            if not isinstance(evidence, dict):
                raise ValueError("clone occurrence lacks independently checkable evidence")
            context_span = evidence.get("rust_context_span")
            context_hash = evidence.get("rust_context_sha256")
            if not isinstance(context_span, dict):
                raise ValueError("clone evidence lacks Rust context span")
            context_start, context_end = _span_offsets(rust, context_span)
            clone_start, clone_end = _span_offsets(rust, row["rust_span"])
            if context_start > clone_start or context_end < clone_end:
                raise ValueError("clone Rust context does not enclose the occurrence")
            if not isinstance(context_hash, str) or hashlib.sha256(rust[context_start:context_end].encode()).hexdigest() != context_hash:
                raise ValueError("clone Rust context evidence is stale or forged")
            if not all(isinstance(evidence.get(k), str) and evidence[k].strip()
                       for k in ("semantic_type", "ownership_fact", "semantic_identity")):
                raise ValueError("clone evidence lacks type, ownership, or semantic identity")
            if origin == "explicit_copy":
                copy_id = row.get("source_copy_id")
                if copy_id not in source_by_id:
                    raise ValueError("explicit clone points to unknown Moss copy occurrence")
                explicit_by_id[copy_id].append(_span_key(row["rust_span"]))
            elif origin in {"compiler_runtime_handle", "runtime_support"}:
                callsite_id = evidence.get("runtime_callsite_id")
                callsite = _runtime_callsites.get(callsite_id)
                if callsite is None:
                    raise ValueError("runtime clone classification is not in independently reviewed callsite registry")
                for key in ("origin_kind", "repository_evidence_path", "repository_evidence_sha256",
                            "repository_evidence_span", "semantic_type", "ownership_fact", "semantic_identity"):
                    actual = row.get("origin_kind") if key == "origin_kind" else evidence.get(key)
                    if actual != callsite.get(key):
                        raise ValueError(f"runtime clone evidence disagrees with reviewed callsite field: {key}")
            else:
                unauthorized += 1
                unclassified += 1
        for copy_id, source_row in source_by_id.items():
            declared = [_span_key(x) for x in source_row["rust_clone_spans"]]
            if declared != explicit_by_id[copy_id]:
                raise ValueError(f"source-to-Rust copy lowering links mismatch: {copy_id}")
        return {"status": "PASS" if unauthorized == 0 else "UNAUTHORIZED", "authorized": len(source_by_id),
                "unauthorized": unauthorized, "unclassified": unclassified, "occurrences": rows}
    except (OSError, ValueError, TypeError, KeyError, json.JSONDecodeError) as error:
        return {"status": "INVALID_PROVENANCE", "authorized": 0, "unauthorized": 0,
                "unclassified": len(rust_occurrences), "error": str(error), "occurrences": rust_occurrences}


def check_mutation_expectations(expected: dict, actual_stdout: str) -> list[dict]:
    lines = actual_stdout.splitlines()
    checks = []
    for assertion in expected.get("observable_mutations", []):
        line_number = assertion["stdout_line"]
        actual = lines[line_number - 1] if 0 < line_number <= len(lines) else None
        checks.append({"observable": assertion["observable"],
                       "expected": assertion["expected"], "actual": actual,
                       "status": "PASS" if actual == assertion["expected"] else "FAIL"})
    return checks


def evaluate_project_fixture(fixture: dict, compiler: Path, tmp_dir: Path,
                             fixture_dir: Path) -> dict:
    """Run a checked-in multi-package fixture through source and .mossi paths."""
    fid = fixture["id"]
    work = tmp_dir / "projects" / fid
    if work.exists():
        shutil.rmtree(work)
    shutil.copytree(fixture_dir, work)
    project = work / "consumer"
    provider = work / "provider"
    source = project / "src/main.moss"
    env = os.environ.copy()
    env["MOSS"] = str(compiler)
    provider_code, provider_out, provider_err, provider_time = run_process(
        [str(MARGO), "build", "--json"], cwd=provider,
        timeout=180, env=env)
    result = {
        "id": fid, "defect_id": fixture["defect_id"], "name": fixture["name"],
        "file": fixture["file"], "category": fixture["category"],
        "spec_citation": fixture["spec_citation"], "kind": fixture["kind"],
        "status": "PASS", "modes": {}, "engine_results": {},
        "expected": fixture.get("expected", {}), "failures": [],
        "clone_audit": {}, "explicit_copies": 0,
        "timings": {"provider_build": provider_time},
    }
    if provider_code != 0:
        result["status"] = "FAIL"
        result["modes"]["provider_mossi"] = "BUILD_FAIL"
        result["engine_results"]["provider_mossi"] = {"exit_code": provider_code,
            "stdout": provider_out, "stderr": provider_err}
        result["failures"].append(f"Provider .mossi build failed: {provider_out}{provider_err}")
        return result
    try:
        provider_build = json.loads(provider_out)
        provider_debug_rust = Path(provider_build["artifacts"]["artifacts"]["generated_rust"])
    except (json.JSONDecodeError, KeyError, TypeError):
        provider_debug_rust = None
    provider_interface = provider / "build/debug/provider.mossi"
    if not provider_interface.exists():
        result["status"] = "FAIL"
        result["modes"]["provider_mossi"] = "MISSING"
        result["failures"].append("Provider build did not produce provider.mossi")
        return result
    env["MOSS_MODULE_PATH"] = str(provider / "build/debug")
    check_code, check_out, check_err, check_time = run_process(
        [str(compiler), "check", "--json", str(source)], cwd=project,
        timeout=180, env=env)
    result["timings"]["checker"] = check_time
    result["engine_results"]["checker"] = {"exit_code": check_code,
                                               "stdout": check_out, "stderr": check_err}
    result["modes"]["checker"] = "PASS" if check_code == 0 else "UNEXPECTED_REJECT"
    if check_code != 0:
        result["status"] = "FAIL"
        result["failures"].append(f"Consumer checker rejected imported effect fixture: {check_out}{check_err}")
        return result
    effect_code, effect_out, effect_err, effect_time = run_process(
        [str(compiler), "effects", "provider__lookup", "--source", str(source), "--json"],
        cwd=project, timeout=180, env=env)
    result["timings"]["imported_effects"] = effect_time
    expected_raise_set = fixture["expected"]["imported_mossi_effects"]["raise_set"]
    try:
        effect_json = json.loads(effect_out)
        raise_set = effect_json["result"]["observable_effects"]["raise_set"]
        observed = raise_set == expected_raise_set
    except (json.JSONDecodeError, KeyError, TypeError):
        raise_set, observed = [], False
    result["engine_results"]["imported_mossi_effects"] = {"exit_code": effect_code,
        "raise_set": raise_set, "expected_raise_set": expected_raise_set}
    result["modes"]["imported_mossi_effects"] = "PASS" if observed else "MISSING_EFFECT"
    if effect_code != 0 or not observed:
        result["status"] = "FAIL"
        result["failures"].append(f"Imported .mossi omitted CollectionError.MissingKey raise effect: {effect_out}{effect_err}")
        return result
    fd_code, fd_out, fd_err, fd_time = run_process(
        [str(compiler), "run", "--interp", str(source)], cwd=project,
        timeout=180, env=env)
    result["timings"]["fast_debug"] = fd_time
    result["engine_results"]["fast_debug"] = {"expected_stdout": fixture["expected_fast_debug"],
        "actual_stdout": fd_out.strip(), "exit_code": fd_code}
    result["modes"]["fast_debug"] = "PASS" if fd_code == 0 and fd_out.strip() == fixture["expected_fast_debug"] else "FAIL"
    if result["modes"]["fast_debug"] == "FAIL":
        result["status"] = "FAIL"
        result["failures"].append(f"Fast Debug imported fixture mismatch: {fd_out}{fd_err}")
    for profile, release, mode in (("native_debug", False, "debug"),
                                    ("native_optimized", True, "optimized")):
        provider_rust = provider_debug_rust
        if release:
            provider_cmd = [str(MARGO), "build", "--release", "--json"]
            pcode, pout, perr, ptime = run_process(provider_cmd, cwd=provider,
                timeout=300, env=env)
            result["timings"]["provider_release_build"] = ptime
            try:
                provider_rust = Path(json.loads(pout)["artifacts"]["artifacts"]["generated_rust"])
            except (json.JSONDecodeError, KeyError, TypeError):
                provider_rust = None
            if pcode != 0:
                result["status"] = "FAIL"
                result["failures"].append(f"Provider optimized .mossi build failed: {pout}{perr}")
        cmd = [str(MARGO), "build", "--json"]
        if release:
            cmd.insert(2, "--release")
        code, stdout, stderr, elapsed = run_process(cmd, cwd=project, timeout=300, env=env)
        result["timings"][profile + "_build"] = elapsed
        try:
            payload = json.loads(stdout)
            binary = Path(payload["artifacts"]["artifacts"]["executable"])
        except (json.JSONDecodeError, KeyError, TypeError):
            binary = None
        if code != 0 or binary is None or not binary.exists():
            result["status"] = "FAIL"
            result["modes"][profile] = "BUILD_FAIL"
            result["engine_results"][profile] = {"exit_code": code, "stdout": stdout, "stderr": stderr}
            result["failures"].append(f"{profile} package build failed: {stderr}")
            continue
        try:
            generated_rust = Path(payload["artifacts"]["artifacts"]["generated_rust"])
        except (KeyError, TypeError):
            generated_rust = None
        if generated_rust is None or provider_rust is None:
            result["status"] = "FAIL"
            result["modes"][profile + "_clone_audit"] = "GENERATED_RUST_MISSING"
            result["failures"].append(f"{profile} Margo result lacks generated Rust artifact path")
        else:
            consumer_audit = audit_clone_provenance(generated_rust, source, mode, 0)
            provider_source = provider / "src/provider.moss"
            provider_audit = audit_clone_provenance(provider_rust, provider_source, mode, 0)
            result["clone_audit"][profile + "_consumer"] = consumer_audit
            result["clone_audit"][profile + "_provider"] = provider_audit
            for package_name, audit in (("consumer", consumer_audit), ("provider", provider_audit)):
                if audit["status"] != "PASS" or audit["unclassified"] or audit["unauthorized"]:
                    result["status"] = "FAIL"
                    result["modes"][profile + "_clone_audit"] = "FAIL"
                    result["failures"].append(f"{profile} {package_name} clone provenance: "
                        f"status={audit['status']}, unclassified={audit['unclassified']}, "
                        f"unauthorized={audit['unauthorized']}")
            result["modes"].setdefault(profile + "_clone_audit", "PASS")
        run_code, run_out, run_err, run_time = run_process([str(binary)], cwd=project, timeout=180, env=env)
        result["timings"][profile + "_execute"] = run_time
        expected = fixture["expected_native"]
        ok = run_code == 0 and run_out.strip() == expected
        result["modes"][profile] = "PASS" if ok else "FAIL"
        result["engine_results"][profile] = {"expected_stdout": expected,
            "actual_stdout": run_out.strip(), "exit_code": run_code, "stderr": run_err}
        if not ok:
            result["status"] = "FAIL"
            result["failures"].append(f"{profile} imported fixture mismatch: {run_out}{run_err}")
    return result


def evaluate_fixture(fixture: dict, compiler: Path, tmp_dir: Path) -> dict:
    fid = fixture["id"]
    fname = fixture["file"]
    kind = fixture["kind"]
    fpath = FIXTURES_DIR / fname

    if fpath.is_dir():
        return evaluate_project_fixture(fixture, compiler, tmp_dir, fpath)

    result = {
        "id": fid,
        "defect_id": fixture["defect_id"],
        "name": fixture["name"],
        "file": fname,
        "category": fixture["category"],
        "spec_citation": fixture["spec_citation"],
        "kind": kind,
        "status": "PASS",
        "modes": {},
        "engine_results": {},
        "expected": fixture.get("expected", {}),
        "failures": [],
        "clone_audit": {},
        "explicit_copies": fixture.get("authorized_explicit_copies", 0),
        "timings": {},
    }

    if not fpath.exists():
        result["status"] = "FAIL"
        result["failures"].append(f"Fixture file missing: {fpath}")
        return result

    # 1. Semantic Checker
    check_code, check_stdout, check_stderr, check_time = run_process(
        [str(compiler), "check", "--json", str(fpath)]
    )
    result["timings"]["checker"] = check_time
    combined_check = check_stdout + "\n" + check_stderr

    if kind == "REJECTED":
        if check_code == 0:
            result["status"] = "FAIL"
            result["modes"]["checker"] = "UNEXPECTED_ACCEPT"
            result["failures"].append(
                "Checker unexpectedly ACCEPTED invalid program; expected the oracle-mapped structured rejection"
            )
        else:
            try:
                diagnostic_payload = json.loads(check_stdout)
            except json.JSONDecodeError:
                diagnostic_payload = None
            mapping = _diagnostic_doc["mappings"][fid]
            diag_matched, diag_summary = validate_rejection_diagnostic(
                diagnostic_payload, mapping, fpath)
            if diag_matched:
                result["modes"]["checker"] = "PASS"
                result["engine_results"]["checker"] = {"status": "PASS", "exit_code": check_code, "diagnostic": diag_summary}
            else:
                result["status"] = "FAIL"
                result["modes"]["checker"] = "WRONG_DIAGNOSTIC"
                result["failures"].append(
                    f"Checker rejection was unrelated or malformed ({diag_summary}); output: {combined_check.strip()}"
                )
        # REJECTED programs do not execute in Fast Debug or Native
        return result

    # For ACCEPTED programs:
    if check_code != 0:
        result["status"] = "FAIL"
        result["modes"]["checker"] = "UNEXPECTED_REJECT"
        result["failures"].append(f"Checker unexpectedly REJECTED valid program: {combined_check.strip()}")
    else:
        result["modes"]["checker"] = "PASS"
        result["engine_results"]["checker"] = {"status": "PASS", "exit_code": check_code}

    if check_code == 0 and "inferred_effects" in fixture["expected"]:
        effect_expectation = fixture["expected"]["inferred_effects"]
        effect_code, effect_out, effect_err, effect_time = run_process(
            [str(compiler), "effects", effect_expectation["target"],
             "--source", str(fpath), "--json"])
        result["timings"]["inferred_effects"] = effect_time
        try:
            effect_result = json.loads(effect_out)["result"]["observable_effects"]
            actual_raises = effect_result["raise_set"]
        except (json.JSONDecodeError, KeyError, TypeError):
            actual_raises = []
        effect_ok = effect_code == 0 and actual_raises == effect_expectation["raise_set"]
        result["engine_results"]["inferred_effects"] = {
            "expected_raise_set": effect_expectation["raise_set"],
            "actual_raise_set": actual_raises, "exit_code": effect_code}
        result["modes"]["inferred_effects"] = "PASS" if effect_ok else "RAISE_SET_MISMATCH"
        if not effect_ok:
            result["status"] = "FAIL"
            result["failures"].append(f"Inferred raise-set mismatch: expected {effect_expectation['raise_set']}, got {actual_raises}; {effect_err}")

    # 2. Fast Debug Execution
    fd_code, fd_stdout, fd_stderr, fd_time = run_process(
        [str(compiler), "run", "--interp", str(fpath)]
    )
    result["timings"]["fast_debug"] = fd_time
    actual_fd = fd_stdout.strip()
    exp_fd = fixture["expected_fast_debug"]
    result["engine_results"]["fast_debug"] = {"expected_stdout": exp_fd, "actual_stdout": actual_fd, "exit_code": fd_code}
    mutation_checks = check_mutation_expectations(fixture["expected"], actual_fd)
    result["engine_results"]["fast_debug"]["mutation_checks"] = mutation_checks
    if any(item["status"] == "FAIL" for item in mutation_checks):
        result["status"] = "FAIL"
        result["failures"].append("Fast Debug observable mutation expectation failed")

    if fixture.get("expected_fast_debug_status") == "panic":
        patterns = fixture["expected"]["fast_debug"]["panic_patterns"]
        panic_ok, panic_reason = expected_panic_matches(fd_code, fd_stdout, fd_stderr, patterns,
            fixture["expected"]["fast_debug"].get("stdout", ""))
        if panic_ok:
            result["modes"]["fast_debug"] = "EXPECTED_PANIC"
            result["engine_results"]["fast_debug"]["status"] = "PASS_EXPECTED_PANIC"
        else:
            result["status"] = "FAIL"
            result["modes"]["fast_debug"] = "PANIC_MISMATCH"
            result["failures"].append(f"Fast Debug expected indexing panic: {panic_reason}; stderr={fd_stderr.strip()}")
    elif fd_code != 0:
        result["status"] = "FAIL"
        result["modes"]["fast_debug"] = "CRASH"
        result["failures"].append(f"Fast Debug crashed (exit {fd_code}): {fd_stderr.strip()}")
    elif actual_fd != exp_fd:
        result["status"] = "FAIL"
        result["modes"]["fast_debug"] = "VALUE_MISMATCH"
        result["failures"].append(f"Fast Debug value mismatch: expected {exp_fd!r}, got {actual_fd!r}")
    else:
        result["modes"]["fast_debug"] = "PASS"
        result["engine_results"]["fast_debug"]["status"] = "PASS"

    # 3. Native Debug Lowering & Rustc Compilation
    out_rs = tmp_dir / f"{fid}.rs"
    out_bin = tmp_dir / f"{fid}_bin"
    emit_code, emit_stdout, emit_stderr, emit_time = run_process(
        [str(compiler), str(fpath), "-o", str(out_rs)]
    )
    result["timings"]["native_emit"] = emit_time

    if emit_code != 0:
        result["status"] = "FAIL"
        result["modes"]["native_debug"] = "EMIT_FAIL"
        result["failures"].append(f"Native emission failed: {emit_stderr.strip()}")
    else:
        clone_audit = audit_clone_provenance(out_rs, fpath, "debug", fixture.get("authorized_explicit_copies", 0))
        result["clone_audit"]["native_debug"] = clone_audit
        if (clone_audit["status"] != "PASS" or clone_audit["unclassified"] != 0 or
                clone_audit["unauthorized"] > fixture.get("max_hidden_clones", 0)):
            result["status"] = "FAIL"
            result["failures"].append("Native debug clone provenance audit: " +
                f"status={clone_audit['status']}, unclassified={clone_audit['unclassified']}, " +
                f"unauthorized={clone_audit['unauthorized']}, error={clone_audit.get('error', '')}")

        # rustc compilation
        rustc_code, rustc_stdout, rustc_stderr, rustc_time = run_process(
            ["rustc", str(out_rs), "-o", str(out_bin)]
        )
        result["timings"]["rustc_debug"] = rustc_time
        if rustc_code != 0:
            result["status"] = "FAIL"
            result["modes"]["native_debug"] = "RUSTC_FAIL"
            result["failures"].append(f"rustc compilation failed: {rustc_stderr.strip()[:300]}")
        else:
            # Native execution
            nat_code, nat_stdout, nat_stderr, nat_time = run_process([str(out_bin)])
            result["timings"]["native_exec"] = nat_time
            actual_nat = nat_stdout.strip()
            exp_nat = fixture["expected_native"]
            result["engine_results"]["native_debug"] = {"expected_stdout": exp_nat, "actual_stdout": actual_nat, "exit_code": nat_code}
            mutation_checks = check_mutation_expectations(fixture["expected"], actual_nat)
            result["engine_results"]["native_debug"]["mutation_checks"] = mutation_checks
            if any(item["status"] == "FAIL" for item in mutation_checks):
                result["status"] = "FAIL"
                result["failures"].append("Native debug observable mutation expectation failed")
            if fixture.get("expected_native_status") == "panic":
                patterns = fixture["expected"]["native_debug"]["panic_patterns"]
                panic_ok, panic_reason = expected_panic_matches(nat_code, nat_stdout, nat_stderr, patterns,
                    fixture["expected"]["native_debug"].get("stdout", ""))
                if panic_ok:
                    result["modes"]["native_debug"] = "EXPECTED_PANIC"
                    result["engine_results"]["native_debug"]["status"] = "PASS_EXPECTED_PANIC"
                else:
                    result["status"] = "FAIL"
                    result["modes"]["native_debug"] = "PANIC_MISMATCH"
                    result["failures"].append(f"Native debug expected indexing panic: {panic_reason}; stderr={nat_stderr.strip()}")
            elif nat_code != 0:
                result["status"] = "FAIL"
                result["modes"]["native_debug"] = "CRASH"
                result["failures"].append(f"Native binary crashed (exit {nat_code}): {nat_stderr.strip()}")
            elif actual_nat != exp_nat:
                result["status"] = "FAIL"
                result["modes"]["native_debug"] = "VALUE_MISMATCH"
                result["failures"].append(f"Native value mismatch: expected {exp_nat!r}, got {actual_nat!r}")
            else:
                result["modes"]["native_debug"] = "PASS"
                result["engine_results"]["native_debug"]["status"] = "PASS"

    # 4. Native Optimized (-O) Lowering & Rustc Compilation
    out_opt_rs = tmp_dir / f"{fid}_opt.rs"
    out_opt_bin = tmp_dir / f"{fid}_opt_bin"
    emit_opt_code, emit_opt_stdout, emit_opt_stderr, emit_opt_time = run_process(
        [str(compiler), "-O", str(fpath), "-o", str(out_opt_rs)]
    )
    result["timings"]["native_opt_emit"] = emit_opt_time

    if emit_opt_code != 0:
        result["status"] = "FAIL"
        result["modes"]["native_opt"] = "EMIT_FAIL"
        result["failures"].append(f"Native -O emission failed: {emit_opt_stderr.strip()}")
    else:
        clone_audit = audit_clone_provenance(out_opt_rs, fpath, "optimized", fixture.get("authorized_explicit_copies", 0))
        result["clone_audit"]["native_optimized"] = clone_audit
        if (clone_audit["status"] != "PASS" or clone_audit["unclassified"] != 0 or
                clone_audit["unauthorized"] > fixture.get("max_hidden_clones", 0)):
            result["status"] = "FAIL"
            result["failures"].append("Native optimized clone provenance audit: " +
                f"status={clone_audit['status']}, unclassified={clone_audit['unclassified']}, " +
                f"unauthorized={clone_audit['unauthorized']}, error={clone_audit.get('error', '')}")
        rustc_opt_code, rustc_opt_stdout, rustc_opt_stderr, rustc_opt_time = run_process(
            ["rustc", "-O", str(out_opt_rs), "-o", str(out_opt_bin)]
        )
        result["timings"]["rustc_opt"] = rustc_opt_time
        if rustc_opt_code != 0:
            result["status"] = "FAIL"
            result["modes"]["native_opt"] = "RUSTC_FAIL"
            result["failures"].append(f"rustc -O compilation failed: {rustc_opt_stderr.strip()[:300]}")
        else:
            nat_opt_code, nat_opt_stdout, nat_opt_stderr, nat_opt_time = run_process([str(out_opt_bin)])
            result["timings"]["native_opt_exec"] = nat_opt_time
            actual_nat_opt = nat_opt_stdout.strip()
            exp_nat_opt = fixture["expected_native"]
            result["engine_results"]["native_optimized"] = {"expected_stdout": exp_nat_opt, "actual_stdout": actual_nat_opt, "exit_code": nat_opt_code}
            mutation_checks = check_mutation_expectations(fixture["expected"], actual_nat_opt)
            result["engine_results"]["native_optimized"]["mutation_checks"] = mutation_checks
            if any(item["status"] == "FAIL" for item in mutation_checks):
                result["status"] = "FAIL"
                result["failures"].append("Native optimized observable mutation expectation failed")
            if fixture.get("expected_native_opt_status") == "panic":
                patterns = fixture["expected"]["native_optimized"]["panic_patterns"]
                panic_ok, panic_reason = expected_panic_matches(nat_opt_code, nat_opt_stdout, nat_opt_stderr, patterns,
                    fixture["expected"]["native_optimized"].get("stdout", ""))
                if panic_ok:
                    result["modes"]["native_opt"] = "EXPECTED_PANIC"
                    result["engine_results"]["native_optimized"]["status"] = "PASS_EXPECTED_PANIC"
                else:
                    result["status"] = "FAIL"
                    result["modes"]["native_opt"] = "PANIC_MISMATCH"
                    result["failures"].append(f"Native optimized expected indexing panic: {panic_reason}; stderr={nat_opt_stderr.strip()}")
            elif nat_opt_code != 0:
                result["status"] = "FAIL"
                result["modes"]["native_opt"] = "CRASH"
                result["failures"].append(f"Native -O binary crashed (exit {nat_opt_code}): {nat_opt_stderr.strip()}")
            elif actual_nat_opt != exp_nat_opt:
                result["status"] = "FAIL"
                result["modes"]["native_opt"] = "VALUE_MISMATCH"
                result["failures"].append(f"Native -O value mismatch: expected {exp_nat_opt!r}, got {actual_nat_opt!r}")
            else:
                result["modes"]["native_opt"] = "PASS"
                result["engine_results"]["native_optimized"]["status"] = "PASS"

    return result


def generate_failure_ledger(results: list[dict], compiler_desc: str) -> str:
    lines = []
    lines.append(f"# Phase 28 Independent Semantic Oracle — Failure Ledger")
    lines.append(f"\nEvaluated against: `{compiler_desc}`")
    lines.append(f"Specification Baseline: P28-R7-005 expectation table ({_expectation_doc['approval_status']}) and L7-approved language addendum.\n")

    failed_cases = [r for r in results if r["status"] == "FAIL"]
    passed_cases = [r for r in results if r["status"] == "PASS"]

    lines.append(f"## Summary Statistics")
    lines.append(f"- **Total Fixtures:** {len(results)}")
    lines.append(f"- **Passing all applicable gates:** {len(passed_cases)}")
    lines.append(f"- **Failing at least one semantic or infrastructure gate:** {len(failed_cases)}")
    lines.append(f"- **Semantic failures excluding clone provenance:** {sum(r.get('semantic_status') == 'FAIL' for r in results)}")
    lines.append(f"- **Fixtures blocked by missing/invalid provenance:** {sum(r.get('clone_provenance_status') == 'BLOCKED_MISSING_OR_INVALID' for r in results)}")
    total_unclassified = sum(sum(m.get("unclassified", 0) for m in r.get("clone_audit", {}).values()) for r in results)
    total_unauthorized = sum(sum(m.get("unauthorized", 0) for m in r.get("clone_audit", {}).values()) for r in results)
    lines.append(f"- **Unverified generated `.clone()` occurrences:** {total_unclassified}")
    lines.append(f"- **Unauthorized rows with submitted provenance:** {total_unauthorized}")
    lines.append("")

    lines.append("## Detailed Defect Entries\n")
    lines.append("| Failure ID | Fixture ID | Category | Mode | Description | Specification Citation |")
    lines.append("| --- | --- | --- | --- | --- | --- |")

    for r in failed_cases:
        fail_id = r["defect_id"]
        modes_str = ", ".join(f"{k}:{v}" for k, v in r["modes"].items() if v != "PASS") or "CLONES"
        desc = "; ".join(r["failures"]).replace("\n", " ")[:120]
        spec = r["spec_citation"]
        lines.append(f"| `{fail_id}` | `{r['id']}` | {r['category']} | `{modes_str}` | {desc} | {spec} |")

    lines.append("\n## Per-Fixture Detail\n")
    for r in results:
        status_symbol = "✅ PASS" if r["status"] == "PASS" else "❌ FAIL"
        lines.append(f"### {r['id']}: {r['name']} ({status_symbol})")
        lines.append(f"- **Category:** {r['category']}")
        lines.append(f"- **Spec Rule:** {r['spec_citation']}")
        lines.append(f"- **Expected Outcome:** {r['kind']}")
        modes_detail = " | ".join(f"{k}: {v}" for k, v in r["modes"].items())
        lines.append(f"- **Engine Modes:** {modes_detail}")
        lines.append(f"- **Clone Provenance:** `{json.dumps(r['clone_audit'], sort_keys=True)}`")
        lines.append(f"- **Semantic Status:** `{r.get('semantic_status', 'not classified')}`")
        lines.append(f"- **Clone Provenance Gate:** `{r.get('clone_provenance_status', 'not classified')}`")
        if r["failures"]:
            lines.append(f"- **Observed Failures:**")
            for f in r["failures"]:
                lines.append(f"  - `{f}`")
        lines.append("")

    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Phase 28 Independent Semantic Oracle Suite")
    parser.add_argument("--compiler", type=Path, default=ROOT / "moss", help="Path to moss compiler binary")
    parser.add_argument("--tmp", type=Path, default=DEFAULT_TMP, help="Scratch directory for artifacts")
    parser.add_argument("--json-out", type=Path, default=None, help="Output JSON results path")
    parser.add_argument("--ledger-out", type=Path, default=None, help="Output failure ledger Markdown path")
    parser.add_argument("--validate-only", action="store_true", help="Validate oracle-owned data and fixture coverage without invoking a compiler")
    args = parser.parse_args()

    try:
        validate_oracle_data()
    except ValueError as error:
        print(f"Invalid oracle data: {error}", file=sys.stderr)
        return 2
    if args.validate_only:
        print(f"Oracle data valid: {len(ORACLE_FIXTURES)} fixtures; state={_expectation_doc['approval_status']}")
        return 0

    compiler = args.compiler.resolve()
    if not compiler.exists():
        print(f"Error: compiler executable not found at {compiler}", file=sys.stderr)
        return 2

    tmp_dir = args.tmp.resolve()
    tmp_dir.mkdir(parents=True, exist_ok=True)

    print(f"================================================================================")
    print(f"Phase 28 Independent Semantic Oracle Suite")
    print(f"Compiler: {compiler}")
    print(f"Fixtures: {len(ORACLE_FIXTURES)}")
    print(f"Specification: P28-R7-005 expectation table ({_expectation_doc['approval_status']}) and language addendum")
    print(f"================================================================================\n")

    results = []
    passed = 0
    failed = 0

    for fixture in ORACLE_FIXTURES:
        res = evaluate_fixture(fixture, compiler, tmp_dir)
        res["semantic_failures"] = [failure for failure in res["failures"]
                                     if "clone provenance" not in failure.lower()]
        res["semantic_status"] = "FAIL" if res["semantic_failures"] else "PASS"
        clone_statuses = [audit.get("status") for audit in res.get("clone_audit", {}).values()]
        if not clone_statuses:
            res["clone_provenance_status"] = "NOT_APPLICABLE"
        elif all(status == "PASS" for status in clone_statuses):
            res["clone_provenance_status"] = "PASS"
        elif any(status in {"PROVENANCE_MISSING", "INVALID_PROVENANCE"}
                 for status in clone_statuses):
            res["clone_provenance_status"] = "BLOCKED_MISSING_OR_INVALID"
        else:
            res["clone_provenance_status"] = "FAIL"
        results.append(res)
        status_tag = "[PASS]" if res["status"] == "PASS" else "[FAIL]"
        modes_str = " ".join(f"{k}={v}" for k, v in res["modes"].items())
        clone_str = f"clone-unclassified={sum(x.get('unclassified', 0) for x in res['clone_audit'].values())}"
        print(f"{status_tag} {res['id']:<35} | {res['category']:<25} | semantic={res['semantic_status']} provenance={res['clone_provenance_status']} {modes_str} {clone_str}")
        if res["failures"]:
            for fail in res["failures"]:
                print(f"       -> {fail}")
        if res["status"] == "PASS":
            passed += 1
        else:
            failed += 1

    print("\n--------------------------------------------------------------------------------")
    print(f"Total: {len(results)} | Passed all applicable gates: {passed} | Failed at least one gate: {failed}")
    semantic_failed = sum(result["semantic_status"] == "FAIL" for result in results)
    provenance_blocked = sum(result["clone_provenance_status"] == "BLOCKED_MISSING_OR_INVALID" for result in results)
    print(f"Semantic failures (excluding provenance): {semantic_failed}")
    print(f"Fixtures blocked by missing/invalid provenance: {provenance_blocked}")
    total_unclassified = sum(sum(m.get("unclassified", 0) for m in r.get("clone_audit", {}).values()) for r in results)
    total_unauthorized = sum(sum(m.get("unauthorized", 0) for m in r.get("clone_audit", {}).values()) for r in results)
    print(f"Observed generated Rust .clone() occurrences unverified={total_unclassified}; unauthorized={total_unauthorized} only counts rows with provenance")
    print("--------------------------------------------------------------------------------\n")

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(results, indent=2))
        print(f"Results written to {args.json_out}")

    if args.ledger_out:
        args.ledger_out.parent.mkdir(parents=True, exist_ok=True)
        ledger_text = generate_failure_ledger(results, str(compiler))
        args.ledger_out.write_text(ledger_text)
        print(f"Failure ledger written to {args.ledger_out}")

    # Note: Oracle harness returns 0 when execution completes successfully,
    # regardless of whether compiler has defects, but prints clear status.
    return 0


if __name__ == "__main__":
    sys.exit(main())
