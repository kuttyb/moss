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
sys.path.insert(0, str(ROOT / "tests" / "tooling"))
from rust_duplication_scanner import OPERATORS as RUST_DUPLICATION_OPERATORS
from rust_duplication_scanner import mask_non_code as mask_rust_non_code
from rust_duplication_scanner import operator_spans as rust_operator_spans
from rust_duplication_scanner import code_tokens as rust_code_tokens

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
    if oracle_version != 2 or len(_expectation_doc.get("fixtures", [])) != 62:
        raise ValueError("P28-R7-011 oracle package must be version 2 with exactly 62 fixtures")
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


def _source_byte_position(source: str, offset: int) -> dict:
    """Return Moss source coordinates with byte-counted columns."""
    line = source.count("\n", 0, offset) + 1
    line_start = source.rfind("\n", 0, offset) + 1
    return {"line": line,
            "column": len(source[line_start:offset].encode("utf-8")) + 1}


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


def _source_byte_span_offsets(source: str, span: dict) -> tuple[int, int]:
    """Map byte-column source spans to Python character offsets safely."""
    encoded = source.encode("utf-8")
    line_starts = [0]
    line_starts.extend(index + 1 for index, byte in enumerate(encoded) if byte == 0x0A)

    def offset(pos: dict) -> int:
        line, column = pos["line"], pos["column"]
        if line < 1 or column < 1 or line > len(line_starts):
            raise ValueError("source byte span coordinates are outside artifact")
        start = line_starts[line - 1]
        line_end = encoded.find(b"\n", start)
        if line_end < 0:
            line_end = len(encoded)
        byte_offset = start + column - 1
        if byte_offset > line_end:
            raise ValueError("source byte column is outside its line")
        try:
            return len(encoded[:byte_offset].decode("utf-8"))
        except UnicodeDecodeError as error:
            raise ValueError("source byte column splits a UTF-8 code point") from error

    start, end = offset(span["start"]), offset(span["end"])
    if end < start:
        raise ValueError("source span end precedes start")
    return start, end


def _tooling_function_variant_at(rust: str, position: int) -> tuple[str, str, int, int, str]:
    function_prefix = "// Moss tooling begin|function|"
    main_prefix = "// Moss tooling begin|main|"
    method_prefix = "// Moss tooling begin|method|"
    end_prefix = "// Moss tooling end|"
    function_begin = rust.rfind(function_prefix, 0, position + 1)
    main_begin = rust.rfind(main_prefix, 0, position + 1)
    method_begin = rust.rfind(method_prefix, 0, position + 1)
    prefix, kind = max(((function_begin, function_prefix, "function"),
                        (main_begin, main_prefix, "main"),
                        (method_begin, method_prefix, "method")),
                       key=lambda row: row[0])[1:]
    begin = max(function_begin, main_begin, method_begin)
    if begin < 0:
        raise ValueError("explicit-copy clone is outside a tooling function variant")
    line_end = rust.find("\n", begin)
    if line_end < 0 or position < line_end:
        raise ValueError("malformed tooling function variant marker")
    identity_start = begin + len(prefix)
    identity_end = rust.find("|", identity_start)
    symbol_start = identity_end + 1
    symbol_end = rust.find("|", symbol_start)
    if identity_end < 0 or symbol_end < 0 or symbol_end == symbol_start or symbol_end >= line_end:
        raise ValueError("tooling function variant lacks exact identity or generated symbol")
    identity = rust[identity_start:identity_end]
    symbol = rust[symbol_start:symbol_end]
    end_marker = end_prefix + identity
    end = rust.find(end_marker, line_end)
    next_function_candidates = [rust.find(candidate, line_end)
                                for candidate in (function_prefix, main_prefix,
                                                  method_prefix)]
    next_function_candidates = [candidate for candidate in next_function_candidates
                                if candidate >= 0]
    next_function = min(next_function_candidates) if next_function_candidates else -1
    if end < 0 or (next_function >= 0 and next_function < end) or position >= end:
        raise ValueError("tooling function variant is unterminated or overlapping")
    return identity, symbol, begin, end, kind


def _function_variant_identity(rust: str, identity: str, symbol: str,
                               begin: int, end: int) -> str:
    material = identity + "\n" + symbol + "\n" + rust[begin:end]
    return "rust-function-variant-v1:" + hashlib.sha256(material.encode()).hexdigest()


def _variant_matches_checked_copy(fact: dict, semantic_identity: str,
                                   generated_symbol: str,
                                   checked_symbols: list) -> bool:
    normalize = lambda value: re.sub(r"@\d+(?:#.*)?$", "", value)
    expected = fact.get("specialization_context") or \
        fact.get("provenance", {}).get("enclosing_callable_id", "")
    if normalize(semantic_identity) != normalize(expected):
        return False
    if not isinstance(checked_symbols, list) or not checked_symbols:
        return False
    return generated_symbol in checked_symbols


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
                                spans.append({"start": _source_byte_position(source, i),
                                              "end": _source_byte_position(source, k)})
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


def query_copy_authority(compiler: Path, source_path: Path,
                        env: dict | None = None) -> dict | None:
    """Ask the checker at a copy occurrence for unit-complete copy authority."""
    source = source_path.read_bytes().decode("utf-8")
    copies = moss_copy_call_spans(source)
    if not copies:
        return None
    first = copies[0]["start"]
    target = f"at:{first['line']}:{first['column']}"
    successful_authorities = []
    failures = []
    # Keep the exact source location fixed. Kinds only resolve ambiguity at
    # that location; they never fall back to a different expression/binding.
    for kind in (None, "binding", "call", "statement", "function", "method", "handler"):
        args = [str(compiler), "ownership", target, "--source", str(source_path)]
        if kind is not None:
            args.extend(["--kind", kind])
        args.append("--json")
        code, stdout, stderr, _ = run_process(args, env=env)
        if code != 0:
            failures.append((stderr or stdout).strip())
            continue
        try:
            payload = json.loads(stdout)
            result = payload.get("result")
            authority = result.get("copy_authority") if isinstance(result, dict) else None
        except (json.JSONDecodeError, AttributeError):
            failures.append("ownership query returned malformed JSON")
            continue
        if isinstance(authority, dict) and authority.get("status") == "available":
            successful_authorities.append(authority)
    if not successful_authorities:
        reason = next((failure for failure in failures if failure), "ownership queries omitted copy_authority")
        return {"status": "unavailable", "reason": reason}
    canonical = [json.dumps(item, ensure_ascii=False, sort_keys=True,
                            separators=(",", ":"), allow_nan=False)
                for item in successful_authorities]
    if any(item != canonical[0] for item in canonical[1:]):
        return {"status": "conflicting", "reason":
                "ownership query retries returned different source-complete copy_authority"}
    return successful_authorities[0]


def query_root_ingress_capture_authority(compiler: Path,
                                        source_path: Path,
                                        env: dict | None = None) -> dict:
    """Read source-complete Root-ingress capture facts from the checker."""
    targets = ["main"]
    code, stdout, stderr, _ = run_process([
        str(compiler), "ownership", targets[0], "--source", str(source_path),
        "--json"], env=env)
    if code != 0:
        symbols_code, symbols_stdout, symbols_stderr, _ = run_process([
            str(compiler), "symbols", "--source", str(source_path), "--json"],
            env=env)
        if symbols_code == 0:
            try:
                symbols = json.loads(symbols_stdout).get("result", {}).get(
                    "symbols", [])
                callable_symbol = next((item for item in symbols
                    if item.get("kind") == "function" and
                    isinstance(item.get("entity_id"), str)), None)
                if callable_symbol:
                    target = callable_symbol["entity_id"].split(":")[-1]
                    code, stdout, stderr, _ = run_process([
                        str(compiler), "ownership", target, "--source",
                        str(source_path), "--json"], env=env)
            except (json.JSONDecodeError, AttributeError, IndexError):
                stderr = symbols_stderr or symbols_stdout
    if code != 0:
        return {"status": "unavailable", "reason": (stderr or stdout).strip()}
    try:
        payload = json.loads(stdout)
        result = payload.get("result")
        authority = result.get("root_ingress_capture_authority") \
            if isinstance(result, dict) else None
    except (json.JSONDecodeError, AttributeError):
        return {"status": "unavailable", "reason": "ownership query returned malformed JSON"}
    if not isinstance(authority, dict):
        return {"status": "unavailable", "reason": "ownership query omitted Root-ingress capture authority"}
    return authority


def validate_root_ingress_capture_authority(
        source: str, source_path: Path, sidecar_rows: list[dict],
        authority: dict | None, rust: str,
        rust_occurrences: list[dict], occurrence_rows: list[dict]) -> dict[str, dict]:
    """Bind each sidecar capture marker and clone to a live exact checker fact."""
    if not isinstance(authority, dict) or authority.get("status") != "available":
        reason = authority.get("reason", "Root-ingress checker authority is unavailable") \
            if isinstance(authority, dict) else "Root-ingress checker authority is unavailable"
        raise LookupError(reason)
    if set(authority) != {"status", "facts"} or not isinstance(authority["facts"], list):
        raise ValueError("Root-ingress capture authority has missing or unknown fields")
    source_digest = "sha256:" + hashlib.sha256(source.encode("utf-8")).hexdigest()
    expected_path = str(source_path.resolve())
    authority_by_id: dict[str, dict] = {}
    fact_keys = {
        "availability", "compiler_generated_capture_only", "concrete_graph_identity",
        "domain_instance_identity", "domain_type_identity", "est_arena_index",
        "est_fact_context", "message_expression_occurrence_id", "origin_id",
        "permits_moss_value_copy", "receiver_binding", "receiver_definition_identity",
        "receiver_expression_occurrence_id", "route_capability_identity",
        "selected_call_edge_est_arena_index", "selected_call_edge_identity",
        "selected_call_edge_invocation_kind", "selected_call_edge_line",
        "selected_call_edge_message_expression_occurrence_id",
        "selected_call_edge_occurrence", "selected_call_edge_receiver",
        "selected_call_edge_receiver_expression_occurrence_id",
        "selected_call_edge_source", "selected_call_edge_source_file",
        "selected_call_edge_source_fingerprint", "selected_call_edge_target",
        "selected_handler_identity", "source_file", "source_fingerprint",
        "source_line", "specialization_context", "statement_occurrence_id",
        "synchronous_root_ingress",
    }
    for row in authority["facts"]:
        if not isinstance(row, dict) or set(row) != {
                "capture_id", "source_file", "source_sha256", "checked_fact"}:
            raise ValueError("Root-ingress checker authority row has missing or unknown fields")
        fact = row["checked_fact"]
        if not isinstance(fact, dict) or set(fact) != fact_keys:
            raise ValueError("checked Root-ingress fact has missing or unknown fields")
        canonical = json.dumps(fact, ensure_ascii=False, sort_keys=True,
                               separators=(",", ":"), allow_nan=False).encode("utf-8")
        capture_id = "root-ingress-domain-capture-fact-v1:" + hashlib.sha256(canonical).hexdigest()
        if row["capture_id"] != capture_id or capture_id in authority_by_id:
            raise ValueError("duplicate or malformed Root-ingress checked fact identity")
        if (row["source_file"] != expected_path or fact["source_file"] != expected_path or
                row["source_sha256"] != source_digest):
            raise ValueError("Root-ingress checked fact source identity or digest mismatch")
        if (fact["availability"] != "Present" or
                fact["synchronous_root_ingress"] is not True or
                fact["compiler_generated_capture_only"] is not True or
                fact["permits_moss_value_copy"] is not False or
                fact["selected_call_edge_source"] != "main" or
                fact["selected_call_edge_invocation_kind"] != "synchronous_message" or
                fact["selected_call_edge_receiver"] != fact["receiver_binding"] or
                fact["selected_call_edge_source_file"] != expected_path or
                fact["selected_call_edge_line"] != fact["source_line"] or
                fact["selected_call_edge_source_fingerprint"] != fact["source_fingerprint"] or
                fact["selected_call_edge_message_expression_occurrence_id"] !=
                    fact["message_expression_occurrence_id"] or
                fact["selected_call_edge_receiver_expression_occurrence_id"] !=
                    fact["receiver_expression_occurrence_id"]):
            raise ValueError("Root-ingress checker fact violates its capture-only contract")
        for name in ("concrete_graph_identity", "domain_instance_identity", "domain_type_identity",
                     "est_fact_context", "receiver_binding", "receiver_definition_identity",
                     "route_capability_identity", "selected_call_edge_identity",
                     "selected_call_edge_target", "selected_handler_identity",
                     "specialization_context"):
            if not isinstance(fact[name], str) or not fact[name]:
                raise ValueError(f"Root-ingress checked fact lacks {name}")
        authority_by_id[capture_id] = fact

    if not isinstance(sidecar_rows, list):
        raise ValueError("root_ingress_domain_captures is not an array")
    raw_markers = re.findall(
        r"/\* Moss root ingress capture fact: ([^\s]+) \*/", rust)
    if len(raw_markers) != len(set(raw_markers)):
        raise ValueError("duplicate Root-ingress capture marker in Rust artifact")
    sidecar_by_id: dict[str, dict] = {}
    for row in sidecar_rows:
        if not isinstance(row, dict) or set(row) != {
                "capture_id", "checked_fact", "source_sha256", "rust_span"}:
            raise ValueError("Root-ingress sidecar row has missing or unknown fields")
        capture_id = row["capture_id"]
        if capture_id in sidecar_by_id or authority_by_id.get(capture_id) != row["checked_fact"]:
            raise ValueError("Root-ingress sidecar row differs from exact checker authority")
        if row["source_sha256"] != source_digest:
            raise ValueError("Root-ingress sidecar source digest mismatch")
        span = row["rust_span"]
        if not isinstance(span, dict) or set(span) != {"start", "end"}:
            raise ValueError("Root-ingress sidecar Rust span is malformed")
        key = _span_key(span)
        if key not in {_span_key(occurrence) for occurrence in rust_occurrences}:
            raise ValueError("Root-ingress sidecar span is not a Rust clone occurrence")
        clone_start, _ = _span_offsets(rust, span)
        marker = f"/* Moss root ingress capture fact: {capture_id} */"
        before = rust[:clone_start].rstrip()
        if not before.endswith(marker):
            raise ValueError("Root-ingress sidecar clone lacks its exact adjacent marker")
        marker_start = len(before) - len(marker)
        receiver_prefix = before[:marker_start].rstrip()
        receiver_match = re.search(r"([A-Za-z_][A-Za-z0-9_]*)$", receiver_prefix)
        if not receiver_match or receiver_match.group(1) != row["checked_fact"]["receiver_binding"]:
            raise ValueError("Root-ingress marker is not adjacent to its checked receiver")
        sidecar_by_id[capture_id] = row
    if set(sidecar_by_id) - set(authority_by_id):
        raise ValueError("Root-ingress sidecar contains a fact absent from checker authority")
    if set(raw_markers) != set(sidecar_by_id):
        raise ValueError("Root-ingress marker IDs and sidecar rows do not match")

    roots_by_id: dict[str, list[dict]] = {}
    for occurrence in occurrence_rows:
        if occurrence.get("origin_kind") != "root_ingress_domain_capture":
            continue
        evidence = occurrence.get("evidence")
        if not isinstance(evidence, dict):
            raise ValueError("Root-ingress clone occurrence lacks evidence")
        capture_id = evidence.get("semantic_identity")
        capture = sidecar_by_id.get(capture_id)
        if (capture is None or occurrence.get("source_copy_id") is not None or
                occurrence.get("rust_span") != capture["rust_span"] or
                evidence.get("semantic_type") !=
                    capture["checked_fact"]["domain_type_identity"] or
                evidence.get("ownership_fact") !=
                    "compiler_generated_capture_only;permits_moss_value_copy=false"):
            raise ValueError("Root-ingress clone occurrence differs from its checker fact")
        roots_by_id.setdefault(capture_id, []).append(occurrence)
    if set(roots_by_id) != set(sidecar_by_id) or any(
            len(rows) != 1 for rows in roots_by_id.values()):
        raise ValueError("Root-ingress checker facts and clone occurrences are not one-to-one")
    return sidecar_by_id


def clone_occurrences(rust: str) -> list[dict]:
    masked = mask_rust_non_code(rust)
    return [{"start": _position(rust, start), "end": _position(rust, end)}
            for start, end in rust_operator_spans(masked, "dot_clone")]


COPY_AUTHORITY_FACT_KEYS = {
    "source_file", "source_sha256", "est_arena_index", "copy_expression_id",
    "source_expression_id", "copy_occurrence_span", "source_occurrence_span",
    "statement_occurrence_id", "specialization_identity",
    "specialization_context", "est_fact_context", "source_type_key",
    "result_type_key", "required_source_access", "creates_independent_owner",
    "availability", "provenance",
}
COPY_AUTHORITY_SPAN_KEYS = {
    "start_line", "start_column", "end_line", "end_column",
    "source_begin", "source_end",
}


def typed_copy_fact_identity(fact: dict) -> str:
    # File paths remain in the exact checker fact and are independently
    # checked against the source artifact. They identify where the evidence
    # was observed, but should not perturb the semantic occurrence identity
    # when identical source bytes are checked from another checkout root.
    # Bind both path fields to the checked content digest for identity only.
    identity_fact = dict(fact)
    source_hash = identity_fact.get("source_sha256")
    if not isinstance(source_hash, str) or not source_hash.startswith("sha256:"):
        raise ValueError("checked copy fact lacks its source content digest")
    identity_fact["source_file"] = source_hash
    provenance = identity_fact.get("provenance")
    if not isinstance(provenance, dict):
        raise ValueError("checked copy fact lacks provenance")
    identity_provenance = dict(provenance)
    identity_provenance["source_file"] = source_hash
    identity_fact["provenance"] = identity_provenance
    canonical = json.dumps(identity_fact, ensure_ascii=False, sort_keys=True,
                           separators=(",", ":"), allow_nan=False).encode("utf-8")
    return "typed-copy-fact-v1:" + hashlib.sha256(canonical).hexdigest()


def _fact_span_matches_source(span: dict, source: str) -> tuple[int, int]:
    if not isinstance(span, dict) or set(span) != COPY_AUTHORITY_SPAN_KEYS:
        raise ValueError("checked copy fact span has an invalid schema")
    for name in COPY_AUTHORITY_SPAN_KEYS:
        if not isinstance(span[name], int) or isinstance(span[name], bool):
            raise ValueError(f"checked copy fact span field is not an integer: {name}")
    start_line = span["start_line"]
    start_column = span["start_column"]
    end_line = span["end_line"]
    end_column = span["end_column"]
    start = _source_byte_span_offsets(source,
        {"start": {"line": start_line, "column": start_column},
         "end": {"line": start_line, "column": start_column}})[0]
    end = _source_byte_span_offsets(source,
        {"start": {"line": end_line, "column": end_column},
         "end": {"line": end_line, "column": end_column}})[1]
    encoded_start = len(source[:start].encode("utf-8"))
    encoded_end = len(source[:end].encode("utf-8"))
    if span["source_begin"] != encoded_start or span["source_end"] != encoded_end:
        raise ValueError("checked copy fact byte range does not match its source coordinates")
    if encoded_end <= encoded_start:
        raise ValueError("checked copy fact span is empty or reversed")
    return start, end


def _flat_fact_span_key(span: dict) -> tuple[int, int, int, int]:
    return (span["start_line"], span["start_column"],
            span["end_line"], span["end_column"])


def _nested_span_key(span: dict) -> tuple[int, int, int, int]:
    return (span["start"]["line"], span["start"]["column"],
            span["end"]["line"], span["end"]["column"])


def validate_checked_copy_authority(source: str, source_path: Path,
                                    source_copies: list[dict], source_rows: list[dict],
                                    sidecar_fact_groups: list[dict] | None,
                                    authority: dict | None) -> dict[str, dict]:
    """Match sidecar copy rows to exact live checker TypedCopyFactV1 query rows."""
    if not source_copies:
        if source_rows or sidecar_fact_groups:
            raise ValueError("sidecar includes copy rows for source with no copy(...) calls")
        return {}
    if isinstance(authority, dict) and authority.get("status") == "conflicting":
        raise ValueError(authority.get("reason", "copy authority queries conflict"))
    if not isinstance(authority, dict) or authority.get("status") != "available":
        raise LookupError("checked TypedCopyFactV1 authority is unavailable")
    if set(authority) != {"status", "facts", "callable_projections"}:
        raise ValueError("copy_authority top-level schema has missing or unknown fields")
    facts = authority.get("facts")
    if not isinstance(facts, list):
        raise ValueError("copy_authority facts is not an array")
    projection_rows = authority.get("callable_projections")
    if not isinstance(projection_rows, list):
        raise ValueError("copy authority callable projections are malformed")
    projections: dict[str, list[str]] = {}
    for row in projection_rows:
        if (not isinstance(row, dict) or set(row) != {"context", "symbols"} or
                not isinstance(row["context"], str) or not row["context"] or
                not isinstance(row["symbols"], list) or not row["symbols"] or
                any(not isinstance(symbol, str) or not symbol for symbol in row["symbols"]) or
                row["symbols"] != sorted(set(row["symbols"])) or
                row["context"] in projections):
            raise ValueError("copy authority callable projection row is invalid")
        projections[row["context"]] = row["symbols"]
    fact_contexts = {fact.get("specialization_context") or
                     fact.get("provenance", {}).get("enclosing_callable_id")
                     for fact in facts if isinstance(fact, dict)}
    if not fact_contexts or set(projections) != fact_contexts:
        raise ValueError("copy authority callable projections do not exactly cover checked facts")
    source_digest = "sha256:" + hashlib.sha256(source.encode("utf-8")).hexdigest()
    expected_path = str(source_path.resolve())
    copies_by_key = {_nested_span_key(span): span for span in source_copies}
    facts_by_copy_key: dict[tuple[int, int, int, int], list[dict]] = {}
    identities: set[str] = set()
    for fact in facts:
        if not isinstance(fact, dict) or set(fact) != COPY_AUTHORITY_FACT_KEYS:
            raise ValueError("checked TypedCopyFactV1 row has missing or unknown fields")
        if fact["availability"] != "Present":
            raise ValueError("checked copy fact is not available")
        if fact["source_file"] != expected_path or fact["source_sha256"] != source_digest:
            raise ValueError("checked copy fact source identity or digest mismatch")
        if fact["required_source_access"] != "READ" or fact["creates_independent_owner"] is not True:
            raise ValueError("checked copy fact does not authorize an independent READ copy")
        if not all(isinstance(fact[k], str) and fact[k] for k in
                   ("source_type_key", "result_type_key")):
            raise ValueError("checked copy fact source/result types are absent")
        if not all(isinstance(fact[k], str) for k in
                   ("specialization_identity", "specialization_context", "est_fact_context")):
            raise ValueError("checked copy fact specialization identity fields are malformed")
        if not fact["est_fact_context"]:
            raise ValueError("checked copy fact lacks fact context")
        for name in ("est_arena_index", "copy_expression_id", "source_expression_id",
                     "statement_occurrence_id"):
            if not isinstance(fact[name], int) or isinstance(fact[name], bool) or fact[name] < 0:
                raise ValueError(f"checked copy fact identity is invalid: {name}")
        provenance = fact["provenance"]
        if not isinstance(provenance, dict) or set(provenance) != {
                "source_file", "line", "defining_module_id", "enclosing_callable_id"}:
            raise ValueError("checked copy fact provenance schema is invalid")
        if provenance["source_file"] != expected_path or not isinstance(provenance["line"], int) or \
                provenance["line"] < 1 or not all(isinstance(provenance[k], str) and provenance[k]
                    for k in ("defining_module_id", "enclosing_callable_id")):
            raise ValueError("checked copy fact provenance does not identify its source callable")
        copy_start, copy_end = _fact_span_matches_source(fact["copy_occurrence_span"], source)
        source_start, source_end = _fact_span_matches_source(fact["source_occurrence_span"], source)
        copy_span_key = _flat_fact_span_key(fact["copy_occurrence_span"])
        scanned = copies_by_key.get(copy_span_key)
        if scanned is None:
            raise ValueError("checked copy fact span does not match a scanned source copy call")
        scan_start, scan_end = _source_byte_span_offsets(source, scanned)
        if (copy_start, copy_end) != (scan_start, scan_end):
            raise ValueError("checked copy fact span disagrees with independent source scanner")
        if not (copy_start < source_start < source_end < copy_end):
            raise ValueError("checked source-expression span is outside its copy occurrence")
        if provenance["line"] != fact["copy_occurrence_span"]["start_line"]:
            raise ValueError("checked copy fact source line conflicts with its occurrence span")
        fact_id = typed_copy_fact_identity(fact)
        if fact_id in identities:
            raise ValueError("duplicate checked TypedCopyFactV1")
        identities.add(fact_id)
        facts_by_copy_key.setdefault(copy_span_key, []).append(fact)

    if set(facts_by_copy_key) != set(copies_by_key):
        raise ValueError("checked copy facts do not cover every scanned copy occurrence")
    if not isinstance(sidecar_fact_groups, list):
        raise ValueError("v3 checked_copy_facts is not an array")
    fact_groups_by_id = {}
    for group in sidecar_fact_groups:
        if not isinstance(group, dict) or set(group) != {"copy_id", "facts"}:
            raise ValueError("checked_copy_facts group has missing or unknown fields")
        copy_id = group["copy_id"]
        if not isinstance(copy_id, str) or copy_id in fact_groups_by_id:
            raise ValueError("duplicate or malformed checked_copy_facts lexical ID")
        if not isinstance(group["facts"], list) or any(not isinstance(x, dict) for x in group["facts"]):
            raise ValueError("checked_copy_facts group facts is not an array of objects")
        fact_groups_by_id[copy_id] = group["facts"]
        fact_ids = [typed_copy_fact_identity(fact) for fact in group["facts"]]
        if fact_ids != sorted(fact_ids):
            raise ValueError("checked_copy_facts are not sorted by canonical fact identity")
    source_order = [span["copy_id"] for span in source_copies]
    if [group["copy_id"] for group in sidecar_fact_groups] != source_order:
        raise ValueError("checked_copy_facts groups are not in lexical source order")
    sidecar_by_copy_key: dict[tuple[int, int, int, int], dict] = {}
    source_row_order = []
    for row in source_rows:
        if not isinstance(row, dict):
            raise ValueError("source copy occurrence is not an object")
        if set(row) != {"copy_id", "source_span", "semantic_status", "rust_clone_spans"}:
            raise ValueError("source-copy v3 row has missing or unknown fields")
        span = row.get("source_span")
        if not isinstance(span, dict) or set(span) != {"start", "end", "copy_id"} or any(
                not isinstance(span.get(point), dict) or set(span[point]) != {"line", "column"}
                for point in ("start", "end")):
            raise ValueError("source span has missing or unknown v2 fields")
        key = _nested_span_key(span)
        if key in sidecar_by_copy_key:
            raise ValueError("duplicate sidecar source-copy occurrence")
        facts_for_copy = facts_by_copy_key.get(key)
        if not facts_for_copy:
            raise ValueError("sidecar source copy has no matching checked copy fact")
        copy_id = row["copy_id"]
        expected_copy_id = f"copy@{key[0]}:{key[1]}"
        if copy_id != expected_copy_id or span.get("copy_id") != expected_copy_id:
            raise ValueError("sidecar copy ID is not the frozen lexical occurrence locator")
        sidecar_facts = fact_groups_by_id.get(copy_id)
        if sidecar_facts is None:
            raise ValueError("source copy lacks its checked_copy_facts group")
        expected_facts = sorted(facts_for_copy, key=typed_copy_fact_identity)
        actual_facts = sorted(sidecar_facts, key=typed_copy_fact_identity)
        if actual_facts != expected_facts:
            raise ValueError("sidecar TypedCopyFactV1 list differs from independent checker query")
        if row.get("semantic_status") != "implemented":
            raise ValueError("source copy semantics are not attested as implemented")
        clone_spans = row.get("rust_clone_spans")
        if not isinstance(clone_spans, list) or any(
                not isinstance(clone_span, dict) or set(clone_span) != {"start", "end"} or any(
                    not isinstance(clone_span.get(point), dict) or
                    set(clone_span[point]) != {"line", "column"}
                    for point in ("start", "end")) for clone_span in clone_spans):
            raise ValueError("source-copy Rust span has missing or unknown fields")
        sidecar_span = row.get("source_span", {})
        fact_span = facts_for_copy[0]["copy_occurrence_span"]
        if _nested_span_key(sidecar_span) != _flat_fact_span_key(fact_span):
            raise ValueError("sidecar copy source span differs from checked fact")
        validated_row = dict(row)
        validated_row["checked_copy_facts"] = expected_facts
        validated_row["callable_projections"] = projections
        sidecar_by_copy_key[key] = validated_row
        source_row_order.append(key)
    if set(sidecar_by_copy_key) != set(copies_by_key):
        raise ValueError("sidecar copy rows do not exactly cover checked copy facts")
    if set(fact_groups_by_id) != {row["copy_id"] for row in sidecar_by_copy_key.values()}:
        raise ValueError("checked_copy_facts groups do not exactly cover lexical source-copy rows")
    if source_row_order != sorted(source_row_order):
        raise ValueError("source_copy_occurrences are not in lexical source order")
    return {row["copy_id"]: row for row in sidecar_by_copy_key.values()}


def derive_copy_materializer_links(rust: str, source_by_id: dict[str, dict],
                                   rust_occurrences: list[dict]) -> dict[tuple[int, int, int, int], tuple[str, str]]:
    """Derive explicit-copy clone links from raw Rust markers and per-fact helpers."""
    comments: list[dict] = []
    masked = mask_rust_non_code(rust, comments)
    tokens = rust_code_tokens(masked)
    tooling_begin = re.compile(r"Moss tooling begin\|(function|main|method)\|([^|]+)\|")
    emitted_callable_ids = set()
    raw_emitted_callable_ids = []
    emitted_source_lines = set()
    for comment in comments:
        identity_match = tooling_begin.search(comment["text"])
        if identity_match:
            raw_id = identity_match.group(2)
            raw_emitted_callable_ids.append(raw_id)
            emitted_callable_ids.add(re.sub(r"@\d+(?:#.*)?$", "", raw_id))
        line_match = re.search(r"Moss line (\d+):", comment["text"])
        if line_match:
            emitted_source_lines.add(int(line_match.group(1)))
    normalized_marker_ids: dict[str, set[str]] = {}
    for raw_id in raw_emitted_callable_ids:
        normalized = re.sub(r"@\d+(?:#.*)?$", "", raw_id)
        normalized_marker_ids.setdefault(normalized, set()).add(raw_id)
    if any(len(raw_ids) > 1 for raw_ids in normalized_marker_ids.values()):
        raise ValueError("emitted callable marker normalization is ambiguous")
    for index, (token, _start, _end) in enumerate(tokens[:-1]):
        if token == "fn":
            emitted_callable_ids.add(tokens[index + 1][0])
    facts: dict[str, tuple[str, dict]] = {}
    helper_symbols: dict[str, str] = {}
    for copy_id, source_row in source_by_id.items():
        for fact in source_row["checked_copy_facts"]:
            fact_id = typed_copy_fact_identity(fact)
            if fact_id in facts:
                raise ValueError("same checker fact is linked to multiple lexical copy occurrences")
            digest = fact_id.removeprefix("typed-copy-fact-v1:")
            symbol = "__moss_copy_value_" + digest[:16]
            collision = helper_symbols.get(symbol)
            if collision is not None and collision != fact_id:
                raise ValueError("per-fact Rust helper symbol prefix collision")
            facts[fact_id] = (copy_id, fact)
            helper_symbols[symbol] = fact_id

    marker_pattern = re.compile(r"\s*Moss copy fact call: (typed-copy-fact-v1:[0-9a-f]{64})\s*")
    markers: list[tuple[str, int, int]] = []
    for comment in comments:
        if "Moss copy fact call:" not in comment["text"]:
            continue
        match = marker_pattern.fullmatch(comment["text"])
        if not match or comment["kind"] != "block":
            raise ValueError("malformed copy-fact marker comment")
        markers.append((match.group(1), comment["start"], comment["end"]))
    markers_by_fact: dict[str, list[tuple[int, int]]] = {}
    for fact_id, start, end in markers:
        if fact_id not in facts:
            raise ValueError("raw Rust copy marker names no exact checker fact")
        markers_by_fact.setdefault(fact_id, []).append((start, end))

    specialization_facts: dict[tuple[str, str], set[str]] = {}
    for fact_id, (_copy_id, fact) in facts.items():
        if fact["specialization_identity"]:
            context = re.sub(r"@\d+(?:#.*)?$", "", fact["specialization_context"])
            # A callable may contain multiple lexical copy expressions. Keep
            # the source occurrence in the key so equal specialization
            # contexts at different source spans do not collide.
            specialization_facts.setdefault((_copy_id, context), set()).add(fact_id)

    rust_by_key = {_span_key(span): span for span in rust_occurrences}
    derived: dict[tuple[int, int, int, int], tuple[str, str]] = {}
    expected_clone_spans: dict[str, list[tuple[int, int, int, int]]] = {
        copy_id: [] for copy_id in source_by_id}
    token_values = [token[0] for token in tokens]
    for fact_id, (copy_id, checked_fact) in facts.items():
        symbol = "__moss_copy_value_" + fact_id.removeprefix("typed-copy-fact-v1:")[:16]
        positions = [i for i, token in enumerate(tokens) if token[0] == symbol]
        fact_markers = markers_by_fact.get(fact_id, [])
        if not fact_markers:
            if positions:
                raise ValueError("per-fact helper exists without its raw Rust call marker")
            if checked_fact["specialization_identity"]:
                raise ValueError("emitted specialization fact has no helper marker/call path")
            base_context = checked_fact["specialization_context"] or \
                checked_fact["provenance"]["enclosing_callable_id"]
            normalized_base = re.sub(r"@\d+(?:#.*)?$", "", base_context)
            if normalized_base in emitted_callable_ids:
                raise ValueError("base callable is emitted but its copy fact has no helper marker")
            sibling_contexts = {identity for identity in emitted_callable_ids
                                if identity.startswith(normalized_base + "<")}
            if sibling_contexts:
                for sibling in sibling_contexts:
                    sibling_facts = specialization_facts.get((copy_id, sibling), set())
                    if not sibling_facts or not sibling_facts.issubset(markers_by_fact):
                        raise ValueError("template exemption has a generated specialization without a fact helper")
            elif checked_fact["provenance"]["line"] in emitted_source_lines:
                raise ValueError("copy source line is emitted but no callable/helper mapping proves its status")
            # A base/template fact is non-emitted only when the raw callable
            # inventory shows no base callable and either its specialized
            # descendants are all fact-linked or its source line is absent.
            continue
        definitions = [i for i in positions if i > 0 and tokens[i - 1][0] == "fn"]
        calls = [i for i in positions if i not in definitions and i + 1 < len(tokens)
                 and tokens[i + 1][0] == "("]
        if len(definitions) != len(fact_markers) or \
                len(calls) != len(fact_markers) or \
                len(positions) != 2 * len(fact_markers):
            raise ValueError("fact helper must have one definition and direct call per generated variant, with no aliases")
        seen_variants = set()
        used_calls = set()
        for marker_start, marker_end in fact_markers:
            variant_id, variant_symbol, variant_begin, variant_end, variant_kind = \
                _tooling_function_variant_at(rust, marker_start)
            normalized_context = re.sub(r"@\d+(?:#.*)?$", "", checked_fact["specialization_context"])
            if re.sub(r"@\d+(?:#.*)?$", "", variant_id) != normalized_context:
                raise ValueError("raw copy marker is inside a different checked callable")
            variant_key = (variant_id, variant_symbol)
            if variant_key in seen_variants:
                raise ValueError("checked copy fact repeats in one generated function variant")
            seen_variants.add(variant_key)
            declaration = variant_symbol.rsplit("::", 1)[-1] if variant_kind == "method" else variant_symbol
            declaration_pattern = re.compile(r"\bfn\s+" + re.escape(declaration) + r"\b")
            if len(list(declaration_pattern.finditer(masked[variant_begin:variant_end]))) != 1:
                raise ValueError("tooling function variant symbol is not its exact Rust declaration")
            call_candidates = [i for i in calls
                               if i not in used_calls and tokens[i][1] >= marker_end and
                               not rust[marker_end:tokens[i][1]].strip()]
            if len(call_candidates) != 1:
                raise ValueError("copy-fact marker is not adjacent to one direct helper call")
            call_index = call_candidates[0]
            if tokens[call_index + 1][0] != "(":
                raise ValueError("copy fact marker does not precede a direct helper call")
            used_calls.add(call_index)
            variant_definitions = [i for i in definitions
                                   if variant_begin <= tokens[i][1] < variant_end]
            variant_calls = [i for i in calls
                             if variant_begin <= tokens[i][1] < variant_end]
            if len(variant_definitions) != 1 or variant_calls != [call_index]:
                raise ValueError("generated function variant does not contain exactly one local helper definition and call")
            # Parse the helper signature and balanced body using token boundaries.
            open_paren = variant_definitions[0] + 1
            if tokens[open_paren][0] != "(":
                raise ValueError("copy helper definition has malformed parameter list")
            depth = 0
            close_paren = None
            for i in range(open_paren, len(tokens)):
                if tokens[i][0] == "(":
                    depth += 1
                elif tokens[i][0] == ")":
                    depth -= 1
                    if depth == 0:
                        close_paren = i
                        break
            if close_paren is None:
                raise ValueError("copy helper parameter list is unterminated")
            body_open = next((i for i in range(close_paren + 1, len(tokens))
                              if tokens[i][0] == "{"), None)
            if body_open is None:
                raise ValueError("copy helper has no body")
            brace_depth = 0
            body_close = None
            for i in range(body_open, len(tokens)):
                if tokens[i][0] == "{":
                    brace_depth += 1
                elif tokens[i][0] == "}":
                    brace_depth -= 1
                    if brace_depth == 0:
                        body_close = i
                        break
            if body_close is None:
                raise ValueError("copy helper body is unterminated")
            body_start, body_end = tokens[body_open][1], tokens[body_close][2]
            if any(token.startswith("__moss_value") for token in token_values[body_open:body_close + 1]):
                raise ValueError("per-fact helper depends on shared __moss_value materialization")
            masked_body = masked[body_start:body_end]
            for operator in RUST_DUPLICATION_OPERATORS:
                if operator == "dot_clone":
                    continue
                if rust_operator_spans(masked_body, operator):
                    raise ValueError(f"per-fact helper contains unclassified duplication candidate: {operator}")
            for clone_key, span in rust_by_key.items():
                clone_start, clone_end = _span_offsets(rust, span)
                if body_start <= clone_start and clone_end <= body_end:
                    if clone_key in derived:
                        raise ValueError("one Rust clone is attributed to multiple checker facts")
                    derived[clone_key] = (copy_id, fact_id)
                    expected_clone_spans[copy_id].append(clone_key)

    return derived


def validate_runtime_clone_site_observations(rows: Any, rust: str) -> None:
    inventory_path = Path(__file__).with_name("runtime_clone_site_inventory.json")
    inventory = json.loads(inventory_path.read_text())
    if inventory.get("schema") != "moss-runtime-clone-site-inventory-v1":
        raise ValueError("unsupported runtime clone site inventory")
    expected = {row["site_id"]: row for row in inventory.get("sites", [])}
    if not isinstance(rows, list):
        raise ValueError("runtime clone site observations are not an array")
    observed = {}
    for row in rows:
        fields = {"site_id", "operator_kind", "source_file", "expected_expression",
                  "semantic_type", "provenance_status", "source_sha256", "source_marker",
                  "source_span", "rust_span", "rust_context_span", "rust_context_sha256",
                  "generated_occurrence"}
        if not isinstance(row, dict) or set(row) != fields:
            raise ValueError("runtime clone site row has missing or unknown fields")
        site_id = row["site_id"]
        key = (site_id, row["generated_occurrence"])
        if site_id not in expected or key in observed:
            raise ValueError("unknown or duplicate runtime clone site observation")
        observed[key] = row
    required = set()
    if "struct MossProcessRuntime" in rust:
        required |= {site_id for site_id in expected if site_id.startswith("r7-010-runtime-")}
    if "MossFileIOReadBorrow" in rust:
        required.add("fileio-read-borrow-01")
    chunk_expansions = rust.count("Moss backend: FileIO CHUNK PIPELINE -- eligible")
    if chunk_expansions:
        required.add("file-chunk-01")
    observed_site_ids = {site_id for site_id, _ in observed}
    if observed_site_ids != required:
        raise ValueError(f"runtime clone site coverage mismatch: expected={sorted(required)} observed={sorted(observed_site_ids)}")
    marker_positions = {}
    marker_re = re.compile(r"/\* MOSS_RUNTIME_CLONE_SITE:([^ ]+) \*/")
    for match in marker_re.finditer(rust):
        marker_positions.setdefault(match.group(1), []).append(match.start())
    for site_id in observed_site_ids:
        indices = sorted(index for observed_id, index in observed if observed_id == site_id)
        if indices != list(range(len(indices))) or (site_id != "file-chunk-01" and indices != [0]):
            raise ValueError(f"runtime clone generated occurrence coverage mismatch: {site_id}")
        if site_id == "file-chunk-01" and indices != list(range(chunk_expansions)):
            raise ValueError("FileChunk observations do not cover every eligible generated expansion")
    for (site_id, _), row in observed.items():
        spec = expected[site_id]
        for key in ("source_file", "source_sha256", "source_marker", "semantic_type",
                    "operator_kind", "expected_expression"):
            inventory_key = {"source_marker": "marker"}.get(key, key)
            if row[key] != spec[inventory_key]:
                raise ValueError(f"runtime clone source identity mismatch: {site_id} {key}")
        if row["source_span"] != spec["source_span"] or row["provenance_status"] != "source_site_only_unapproved":
            raise ValueError(f"runtime clone source span/status mismatch: {site_id}")
        source = (Path(__file__).resolve().parents[3] / spec["source_file"]).read_bytes()
        if hashlib.sha256(source).hexdigest() != spec["source_sha256"]:
            raise ValueError(f"runtime clone source blob changed: {site_id}")
        span = spec["source_span"]
        begin, end = span["source_begin"], span["source_end"]
        if source[begin:end] != f"/* {spec['marker']} */".encode():
            raise ValueError(f"runtime clone source marker span changed: {site_id}")
        if spec["type_evidence_file"] != "tests/tooling/check_phase28_r7_010_runtime_clone_evidence.py":
            raise ValueError(f"runtime clone type evidence is not a registered assertion: {site_id}")
        op = row["rust_span"]
        ctx = row["rust_context_span"]
        if not isinstance(op, dict) or set(op) != {"start", "end"} or not isinstance(ctx, dict) or set(ctx) != {"start", "end"}:
            raise ValueError(f"runtime clone Rust span malformed: {site_id}")
        lines = rust.splitlines(keepends=True)
        def offset(point: dict[str, int]) -> int:
            line = point.get("line", 0)
            col = point.get("column", 0)
            if not (1 <= line <= len(lines) and col >= 1):
                raise ValueError(f"runtime clone Rust location invalid: {site_id}")
            return sum(map(len, lines[:line - 1])) + col - 1
        op_begin, op_end = offset(op["start"]), offset(op["end"])
        ctx_begin, ctx_end = offset(ctx["start"]), offset(ctx["end"])
        if not (0 <= ctx_begin <= op_begin < op_end <= ctx_end <= len(rust)):
            raise ValueError(f"runtime clone Rust span bounds invalid: {site_id}")
        operator = rust[op_begin:op_end]
        if row["operator_kind"] == "direct_method":
            valid_operator = operator == ".clone()"
        else:
            valid_operator = operator.startswith("Arc::clone(") and operator.endswith(")")
        context = rust[ctx_begin:ctx_end]
        if not valid_operator or hashlib.sha256(context.encode()).hexdigest() != row["rust_context_sha256"]:
            raise ValueError(f"runtime clone generated operator/context mismatch: {site_id}")
        markers = marker_positions.get(site_id, [])
        site_rows = sorted((entry for key, entry in observed.items() if key[0] == site_id),
                           key=lambda entry: entry["generated_occurrence"])
        if len(markers) != len(site_rows):
            raise ValueError(f"runtime clone marker occurrence count mismatch: {site_id}")
        marker_at = markers[row["generated_occurrence"]]
        if rust.rfind("\n", 0, marker_at) != rust.rfind("\n", 0, op_begin) or marker_at >= op_begin:
            raise ValueError(f"runtime clone marker is relocated from its exact operator: {site_id}")
        context_code = re.sub(r"/\* MOSS_RUNTIME_CLONE_SITE:[^*]+\*/", " ", context)
        if site_id == "file-chunk-01" and not (
                re.search(r"let\s+\w+\s*=\s*std::sync::Arc::clone\(&\w+\);", context_code) and
                re.search(r"branch_publish\(&\w+,\s*move\s*\|\|", context_code)):
            raise ValueError("FileChunk site lacks bound result-slot and branch_publish context")


def audit_clone_provenance(rust_path: Path, source_path: Path, mode: str,
                           expected_copy_calls: int,
                           checked_copy_authority: dict | None = None,
                           root_ingress_authority: dict | None = None) -> dict:
    """Require a bijective artifact map for every Rust clone occurrence."""
    if not rust_path.exists():
        return {"status": "RUST_MISSING", "authorized": 0, "unauthorized": 0, "unclassified": 0, "occurrences": []}
    rust_bytes = rust_path.read_bytes()
    source_bytes = source_path.read_bytes()
    rust = rust_bytes.decode("utf-8")
    source = source_bytes.decode("utf-8")
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
        if manifest.get("format") != "moss-clone-provenance" or manifest.get("version") != 3:
            raise ValueError("unsupported clone provenance format")
        if set(manifest) != {"format", "version", "mode", "rust_sha256", "source_sha256",
                             "source_copy_occurrences", "checked_copy_facts",
                             "root_ingress_domain_captures", "runtime_clone_site_observations",
                             "occurrences"}:
            raise ValueError("clone provenance v3 manifest has missing or unknown fields")
        validate_runtime_clone_site_observations(
            manifest.get("runtime_clone_site_observations"), rust)
        if manifest.get("mode") != mode:
            raise ValueError(f"mode mismatch: expected {mode}")
        if manifest.get("rust_sha256") != hashlib.sha256(rust_bytes).hexdigest():
            raise ValueError("generated Rust digest mismatch")
        if manifest.get("source_sha256") != hashlib.sha256(source_bytes).hexdigest():
            raise ValueError("Moss source digest mismatch")
        rows = manifest.get("occurrences")
        if not isinstance(rows, list):
            raise ValueError("occurrences is not an array")
        root_capture_rows = validate_root_ingress_capture_authority(
            source, source_path, manifest.get("root_ingress_domain_captures"),
            root_ingress_authority, rust, rust_occurrences, rows)
        indexed = {}
        for row in rows:
            if not isinstance(row, dict) or set(row) != {
                    "rust_span", "origin_kind", "source_copy_id", "evidence"}:
                raise ValueError("clone occurrence row has missing or unknown v2 fields")
            span = row.get("rust_span", {})
            if not isinstance(span, dict) or set(span) != {"start", "end"} or any(
                    not isinstance(span.get(point), dict) or set(span[point]) != {"line", "column"}
                    for point in ("start", "end")):
                raise ValueError("Rust clone span has missing or unknown fields")
            key = _span_key(span)
            if key in indexed:
                raise ValueError(f"duplicate Rust occurrence mapping at {key}")
            indexed[key] = row
        rust_keys = {_span_key(x) for x in rust_occurrences}
        if set(indexed) != rust_keys:
            raise ValueError(f"incomplete or extra Rust clone mappings: rust={len(rust_keys)}, mapped={len(indexed)}")
        source_by_id = {}
        source_rows = manifest.get("source_copy_occurrences")
        if not isinstance(source_rows, list):
            raise ValueError("source_copy_occurrences is not an array")
        validated_source_rows = validate_checked_copy_authority(source, source_path, source_copies,
            source_rows, manifest.get("checked_copy_facts"), checked_copy_authority)
        for source_row in validated_source_rows.values():
            copy_id = source_row["copy_id"]
            if not isinstance(source_row.get("rust_clone_spans"), list):
                raise ValueError(f"source copy lacks emitted clone list: {copy_id}")
            source_by_id[copy_id] = source_row
        source_keys = {_span_key(x) for x in source_copies}
        manifest_source_keys = {_span_key(x["source_span"]) for x in source_rows}
        if manifest_source_keys != source_keys or len(source_rows) != len(source_copies):
            raise ValueError("source-copy manifest does not exactly cover Moss copy(...) occurrences")
        derived_copy_links = derive_copy_materializer_links(rust, source_by_id, rust_occurrences)
        explicit_by_id: dict[str, list[tuple[int, int, int, int]]] = {key: [] for key in source_by_id}
        explicit_keys: set[tuple[int, int, int, int]] = set()
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
                if set(evidence) != {"rust_context_span", "rust_context_sha256",
                                     "semantic_type", "ownership_fact", "semantic_identity",
                                     "generated_function_variant_id", "generated_function_symbol",
                                     "generated_function_span", "checked_callable_symbols"}:
                    raise ValueError("explicit-copy evidence has missing or unknown v2 fields")
                rust_key = _span_key(row["rust_span"])
                copy_id = row.get("source_copy_id")
                if copy_id not in source_by_id:
                    raise ValueError("explicit clone points to unknown Moss copy occurrence")
                fact_id = evidence["semantic_identity"]
                matching_facts = [fact for fact in source_by_id[copy_id]["checked_copy_facts"]
                                  if typed_copy_fact_identity(fact) == fact_id]
                if len(matching_facts) != 1:
                    raise ValueError("explicit clone semantic identity does not select one checked fact")
                fact = matching_facts[0]
                if (evidence["semantic_type"] != fact["result_type_key"] or
                        evidence["ownership_fact"] != fact["required_source_access"] or
                        evidence["semantic_identity"] != typed_copy_fact_identity(fact)):
                    raise ValueError("explicit-copy evidence disagrees with independently queried TypedCopyFactV1")
                callable_context = fact["specialization_context"] or \
                    fact["provenance"]["enclosing_callable_id"]
                checked_projection = source_by_id[copy_id]["callable_projections"].get(
                    callable_context)
                if evidence["checked_callable_symbols"] != checked_projection:
                    raise ValueError("sidecar callable symbols differ from independently queried checked Program projection")
                variant_id, variant_symbol, variant_begin, variant_end, variant_kind = \
                    _tooling_function_variant_at(rust, clone_start)
                if not _variant_matches_checked_copy(
                        fact, variant_id, variant_symbol,
                        evidence["checked_callable_symbols"]):
                    raise ValueError("explicit-copy generated function variant does not match checked callable authority")
                declaration = variant_symbol.rsplit("::", 1)[-1] \
                    if variant_kind == "method" else variant_symbol
                masked_rust = mask_rust_non_code(rust)
                declaration_pattern = re.compile(r"\bfn\s+" +
                    re.escape(declaration) + r"\b")
                if len(list(declaration_pattern.finditer(
                        masked_rust[variant_begin:variant_end]))) != 1:
                    raise ValueError("tooling function variant symbol is not its exact Rust declaration")
                recorded_variant_begin, recorded_variant_end = _span_offsets(
                    rust, evidence["generated_function_span"])
                if (evidence["generated_function_symbol"] != variant_symbol or
                        evidence["generated_function_variant_id"] !=
                        _function_variant_identity(rust, variant_id, variant_symbol,
                                                   variant_begin, variant_end) or
                        (recorded_variant_begin, recorded_variant_end) !=
                        (variant_begin, variant_end)):
                    raise ValueError("explicit-copy generated function variant evidence is stale or forged")
                if derived_copy_links.get(rust_key) != (copy_id, fact_id):
                    raise ValueError("explicit-copy sidecar link disagrees with raw Rust helper derivation")
                explicit_by_id[copy_id].append(rust_key)
                explicit_keys.add(rust_key)
            elif origin in {"compiler_runtime_handle", "runtime_support"}:
                if set(evidence) != {"rust_context_span", "rust_context_sha256",
                        "semantic_type", "ownership_fact", "semantic_identity",
                        "runtime_callsite_id", "repository_evidence_path",
                        "repository_evidence_sha256", "repository_evidence_span"}:
                    raise ValueError("runtime evidence has missing or unknown v2 fields")
                if _span_key(row["rust_span"]) in derived_copy_links:
                    raise ValueError("fact-derived clone is mislabeled as runtime support")
                callsite_id = evidence.get("runtime_callsite_id")
                callsite = _runtime_callsites.get(callsite_id)
                if callsite is None:
                    raise ValueError("runtime clone classification is not in independently reviewed callsite registry")
                for key in ("origin_kind", "repository_evidence_path", "repository_evidence_sha256",
                            "repository_evidence_span", "semantic_type", "ownership_fact", "semantic_identity"):
                    actual = row.get("origin_kind") if key == "origin_kind" else evidence.get(key)
                    if actual != callsite.get(key):
                        raise ValueError(f"runtime clone evidence disagrees with reviewed callsite field: {key}")
            elif origin == "root_ingress_domain_capture":
                if set(evidence) != {"rust_context_span", "rust_context_sha256",
                                     "semantic_type", "ownership_fact", "semantic_identity"}:
                    raise ValueError("Root-ingress evidence has missing or unknown fields")
            else:
                if _span_key(row["rust_span"]) in derived_copy_links:
                    raise ValueError("fact-derived clone is mislabeled as unclassified")
                unauthorized += 1
                unclassified += 1
        if explicit_keys != set(derived_copy_links):
            raise ValueError("explicit-copy sidecar rows do not exactly cover raw Rust helper clones")
        for copy_id, source_row in source_by_id.items():
            declared = [_span_key(x) for x in source_row["rust_clone_spans"]]
            if sorted(declared) != sorted(explicit_by_id[copy_id]):
                raise ValueError(f"source-to-Rust copy lowering links mismatch: {copy_id}")
        return {"status": "PASS" if unauthorized == 0 else "UNAUTHORIZED",
                "authorized": len(source_by_id),
                "root_ingress_captures": len(root_capture_rows),
                "unauthorized": unauthorized, "unclassified": unclassified,
                "occurrences": rows}
    except LookupError as error:
        return {"status": "PROVENANCE_MISSING", "authorized": 0,
                "unauthorized": 0, "unclassified": len(rust_occurrences),
                "error": str(error), "occurrences": rust_occurrences}
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
    provider_env = env.copy()
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
    provider_copy_authority = query_copy_authority(
        compiler, provider / "src/provider.moss", provider_env)
    provider_root_ingress_authority = query_root_ingress_capture_authority(
        compiler, provider / "src/provider.moss", provider_env)
    env["MOSS_MODULE_PATH"] = str(provider / "build/debug")
    consumer_copy_authority = query_copy_authority(compiler, source, env)
    consumer_root_ingress_authority = query_root_ingress_capture_authority(
        compiler, source, env)
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
            consumer_audit = audit_clone_provenance(
                generated_rust, source, mode, 0,
                consumer_copy_authority, consumer_root_ingress_authority)
            provider_source = provider / "src/provider.moss"
            provider_audit = audit_clone_provenance(
                provider_rust, provider_source, mode, 0,
                provider_copy_authority, provider_root_ingress_authority)
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

    copy_authority = query_copy_authority(compiler, fpath)
    if copy_authority is not None:
        result["copy_authority_query"] = {
            "status": copy_authority.get("status", "malformed")
            if isinstance(copy_authority, dict) else "malformed"}
    root_ingress_authority = query_root_ingress_capture_authority(compiler, fpath)
    result["root_ingress_capture_authority_query"] = {
        "status": root_ingress_authority.get("status", "malformed")}

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
        clone_audit = audit_clone_provenance(out_rs, fpath, "debug",
            fixture.get("authorized_explicit_copies", 0), copy_authority,
            root_ingress_authority)
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
        clone_audit = audit_clone_provenance(out_opt_rs, fpath, "optimized",
            fixture.get("authorized_explicit_copies", 0), copy_authority,
            root_ingress_authority)
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
