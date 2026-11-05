#!/usr/bin/env python3
"""Adversarial self-tests for fail-closed oracle validation."""
import hashlib
import copy
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import subprocess

ROOT = Path(__file__).resolve().parents[3]
(ROOT / "tmp").mkdir(exist_ok=True)
HARNESS_PATH = Path(__file__).with_name("oracle_harness.py")
spec = importlib.util.spec_from_file_location("oracle_harness", HARNESS_PATH)
harness = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = harness
spec.loader.exec_module(harness)


class RejectionAndPanicTests(unittest.TestCase):
    def test_unrelated_crash_is_not_index_panic(self):
        self.assertFalse(harness.expected_panic_matches(1, "", "division by zero", ["index out of bounds"])[0])

    def test_mismatched_panic_diagnostic_is_rejected(self):
        self.assertFalse(harness.expected_panic_matches(1, "", "interpreter internal error", ["vector index out of bounds"])[0])

    def test_timeout_is_not_panic(self):
        self.assertFalse(harness.expected_panic_matches(-1, "", "TIMEOUT", ["index out of bounds"])[0])

    def test_run_process_preserves_timeout_classification(self):
        with mock.patch.object(harness.subprocess, "run", side_effect=subprocess.TimeoutExpired(["x"], 1)):
            code, _out, err, _elapsed = harness.run_process(["x"], timeout=1)
        self.assertEqual((code, err), (-1, "TIMEOUT"))

    def test_structured_diagnostic_requires_code_message_and_relevant_location(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "tmp") as temp:
            source = Path(temp) / "source.moss"
            source.write_text("fn copy(value):\n  return value\n")
            contract = {"code": "MOSS_COMPILE_ERROR", "severity": "error",
                        "semantic_category": "reserved_name",
                        "message_regex": r"\bcopy\b.*reserved",
                        "source_line_regex": r"fn\s+copy\b"}
            base = {"ok": False, "error": {"code": "MOSS_COMPILE_ERROR",
                    "severity": "error", "message": "copy is a reserved keyword",
                    "source_file": str(source.resolve()), "line": 1, "column": 1}}
            self.assertTrue(harness.validate_rejection_diagnostic(base, contract, source)[0])
            wrong_code = json.loads(json.dumps(base))
            wrong_code["error"]["code"] = "MOSS_IO_ERROR"
            self.assertFalse(harness.validate_rejection_diagnostic(wrong_code, contract, source)[0])
            unrelated = json.loads(json.dumps(base))
            unrelated["error"]["message"] = "cannot infer return type"
            self.assertFalse(harness.validate_rejection_diagnostic(unrelated, contract, source)[0])
            wrong_line = json.loads(json.dumps(base))
            wrong_line["error"]["line"] = 2
            self.assertFalse(harness.validate_rejection_diagnostic(wrong_line, contract, source)[0])

    def test_unbound_open_result_does_not_authorize_clone_alias_rejection(self):
        expectation = next(x for x in harness._expectation_doc["fixtures"]
                           if x["fixture_id"] == "ORACLE-10B-CLONE-ALIAS-REJECTED")
        source = harness.FIXTURES_DIR / expectation["fixture_file"]
        payload = {"ok": False, "error": {"code": "TYPE_AMBIGUOUS_OPEN_RESULT",
            "severity": "error", "message": "unbound open result type for variable 'r'",
            "source_file": str(source.resolve()), "line": 6, "column": 3}}
        mapping = harness._diagnostic_doc["mappings"][expectation["fixture_id"]]
        self.assertFalse(harness.validate_rejection_diagnostic(payload, mapping, source)[0])

    def test_clone_alias_mapping_requires_specific_diagnostic_and_statement(self):
        expectation = next(x for x in harness._expectation_doc["fixtures"]
                           if x["fixture_id"] == "ORACLE-10B-CLONE-ALIAS-REJECTED")
        source = harness.FIXTURES_DIR / expectation["fixture_file"]
        mapping = harness._diagnostic_doc["mappings"][expectation["fixture_id"]]
        payload = {"ok": False, "error": {"code": "CLONE_ALIAS_UNSUPPORTED",
            "severity": "error", "message": "source-level .clone() is unsupported; use copy(...) ",
            "source_file": str(source.resolve()), "line": 7, "column": 1}}
        self.assertTrue(harness.validate_rejection_diagnostic(payload, mapping, source)[0])
        payload["error"]["message"] = "unknown result type"
        self.assertFalse(harness.validate_rejection_diagnostic(payload, mapping, source)[0])
        payload["error"]["message"] = "source-level .clone() is unsupported; use copy(...)"
        payload["error"]["line"] = 1
        self.assertFalse(harness.validate_rejection_diagnostic(payload, mapping, source)[0])

    def test_generic_compile_error_does_not_authorize_reserved_copy_rejection(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "tmp") as temp:
            source = Path(temp) / "reserved.moss"
            source.write_text("fn copy(value):\n  return value\n")
            payload = {"ok": False, "error": {"code": "MOSS_COMPILE_ERROR",
                "severity": "error", "message": "copy is a reserved keyword",
                "source_file": str(source.resolve()), "line": 1, "column": 1}}
            mapping = harness._diagnostic_doc["mappings"]["ORACLE-08-USER-COPY-RESOLUTION"]
            self.assertFalse(harness.validate_rejection_diagnostic(payload, mapping, source)[0])

    def test_unrelated_generic_errors_do_not_satisfy_oracle_44_or_52(self):
        for fixture_id, line, column, approved_message in (
            ("ORACLE-44-HANDLER-PRIMITIVE-MUTATE-REJECT", 4, 5,
             "cannot WRITE incoming message payload 'n'; message payloads are immutable snapshots"),
            ("ORACLE-52-MUTATE-DURING-ITERATION-REJECTED", 8, 5,
             "cannot structurally mutate collection 'values' during an active READ traversal"),
        ):
            with self.subTest(fixture=fixture_id):
                expectation = next(x for x in harness._expectation_doc["fixtures"] if x["fixture_id"] == fixture_id)
                source = harness.FIXTURES_DIR / expectation["fixture_file"]
                mapping = harness._diagnostic_doc["mappings"][fixture_id]
                payload = {"ok": False, "error": {"code": "MOSS_COMPILE_ERROR",
                    "severity": "error", "message": approved_message,
                    "source_file": str(source.resolve()), "line": line, "column": column}}
                self.assertTrue(harness.validate_rejection_diagnostic(payload, mapping, source)[0])
                payload["error"]["message"] = "unrelated type mismatch in another expression"
                self.assertFalse(harness.validate_rejection_diagnostic(payload, mapping, source)[0])


class OracleDataValidationTests(unittest.TestCase):
    def test_nested_copy_calls_get_independent_spans_and_ids(self):
        spans = harness.moss_copy_call_spans("result = copy(copy(x))\n")
        self.assertEqual(len(spans), 2)
        self.assertEqual(len({span["copy_id"] for span in spans}), 2)
        self.assertNotEqual(spans[0]["start"], spans[1]["start"])
        self.assertLessEqual(spans[0]["start"]["column"], spans[1]["start"]["column"])
        self.assertGreaterEqual(spans[0]["end"]["column"], spans[1]["end"]["column"])

    def test_mutation_assertions_must_match_every_engine_stdout(self):
        fixture = next(x for x in harness._expectation_doc["fixtures"]
                       if x["fixture_id"] == "ORACLE-07-CATCHABLE-COLL-ERRORS")
        harness.validate_mutation_assertions(fixture)
        broken = copy.deepcopy(fixture)
        broken["expected"]["observable_mutations"][0]["stdout_line"] = 9
        with self.assertRaisesRegex(ValueError, "contradicts fast_debug stdout"):
            harness.validate_mutation_assertions(broken)

    def test_explicit_copy_fixture_mutates_copy_and_preserves_source_expectation(self):
        row = next(x for x in harness._expectation_doc["fixtures"]
                   if x["fixture_id"] == "ORACLE-10A-EXPLICIT-COPY-ONLY")
        source = (harness.FIXTURES_DIR / row["fixture_file"]).read_text()
        self.assertIn("var copied = copy(v[0])", source)
        self.assertIn('copied.tag = "copy"', source)
        self.assertEqual(row["expected"]["native_debug"]["stdout"], "copy\noriginal")
        harness.validate_mutation_assertions(row)


class CloneProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / "tmp")
        self.root = Path(self.temp.name)
        self.rust_path = self.root / "unit.rs"
        self.source_path = self.root / "unit.moss"
        self.source = "value = copy(item)\n"
        self.source_path.write_text(self.source)
        self.source_occ = harness.moss_copy_call_spans(self.source)[0]
        self.fact_row = self.fact()
        self.build_rust_for_facts([(self.fact_row, self.source_occ["copy_id"])])
        self.rust_occ = harness.clone_occurrences(self.rust)[0]

    def tearDown(self):
        self.temp.cleanup()

    def manifest(self, origin="explicit_copy"):
        manifest = self.manifest_for_facts([(self.fact_row, self.source_occ["copy_id"])])
        if origin != "explicit_copy":
            manifest["occurrences"][0]["origin_kind"] = origin
            manifest["occurrences"][0]["evidence"].update({
                "semantic_type": "String", "ownership_fact": "READ",
                "semantic_identity": "helper:value"})
        return manifest

    def build_rust_for_facts(self, fact_links):
        chunks = []
        for index, (fact, _copy_id) in enumerate(fact_links):
            fact_id = harness.typed_copy_fact_identity(fact)
            prefix = fact_id.removeprefix("typed-copy-fact-v1:")[:16]
            symbol = "__moss_copy_value_" + prefix
            chunks.append(f"fn {symbol}(value: Record) -> Record {{\n    let copied = value.clone();\n    copied\n}}")
        calls = []
        callers = []
        for index, (fact, _copy_id) in enumerate(fact_links):
            fact_id = harness.typed_copy_fact_identity(fact)
            prefix = fact_id.removeprefix("typed-copy-fact-v1:")[:16]
            symbol = "__moss_copy_value_" + prefix
            callable_id = fact["specialization_context"] or "main"
            callers.append(
                f"// Moss tooling begin|function|{callable_id}@1|callsite{index}|callsite{index}\n"
                f"fn callsite{index}() {{ let _copy{index} = "
                f"(/* Moss copy fact call: {fact_id} */ {symbol}(value{index})); }}\n"
                f"// Moss tooling end|{callable_id}@1")
            calls.append(f"    callsite{index}();")
        self.rust = "\n".join(chunks + callers) + "\nfn main() {\n" + "\n".join(calls) + "\n}\n"
        self.rust_path.write_text(self.rust)
        all_spans = harness.clone_occurrences(self.rust)
        spans_by_fact = {}
        for (fact, copy_id), span in zip(fact_links, all_spans):
            spans_by_fact[harness.typed_copy_fact_identity(fact)] = span
        return spans_by_fact

    def manifest_for_facts(self, fact_links):
        clone_spans_by_fact = self.build_rust_for_facts(fact_links)
        copies = harness.moss_copy_call_spans(self.source)
        facts_by_copy = {span["copy_id"]: [] for span in copies}
        clone_keys_by_copy = {span["copy_id"]: [] for span in copies}
        occurrences = []
        context = {"start": {"line": 1, "column": 1},
                   "end": harness._position(self.rust, len(self.rust.rstrip("\n")))}
        context_start, context_end = harness._span_offsets(self.rust, context)
        context_hash = hashlib.sha256(self.rust[context_start:context_end].encode()).hexdigest()
        for fact, copy_id in fact_links:
            fact_id = harness.typed_copy_fact_identity(fact)
            facts_by_copy[copy_id].append(fact)
            clone_span = clone_spans_by_fact[fact_id]
            clone_keys_by_copy[copy_id].append(clone_span)
            occurrences.append({"rust_span": clone_span, "origin_kind": "explicit_copy",
                "source_copy_id": copy_id,
                "evidence": {"rust_context_span": context,
                    "rust_context_sha256": context_hash,
                    "semantic_type": fact["result_type_key"],
                    "ownership_fact": fact["required_source_access"],
                    "semantic_identity": fact_id}})
        source_rows = [{"copy_id": span["copy_id"], "source_span": span,
            "semantic_status": "implemented",
            "rust_clone_spans": clone_keys_by_copy[span["copy_id"]]}
            for span in copies]
        return {"format": "moss-clone-provenance", "version": 3, "mode": "debug",
            "rust_sha256": hashlib.sha256(self.rust.encode()).hexdigest(),
            "source_sha256": hashlib.sha256(self.source.encode()).hexdigest(),
            "source_copy_occurrences": source_rows,
            "checked_copy_facts": [{"copy_id": span["copy_id"],
                "facts": sorted(facts_by_copy[span["copy_id"]],
                                key=harness.typed_copy_fact_identity)} for span in copies],
            "occurrences": occurrences}

    def fact(self):
        return self.fact_for_source(self.source_path, self.source, self.source_occ,
                                    "main", 11)

    def fact_for_source(self, source_path, source, copy_span, specialization, expr_id):
        copy_start, copy_end = harness._source_byte_span_offsets(source, copy_span)
        source_start = copy_start + len("copy(")
        source_end = copy_end - 1
        def flat_span(start, end):
            start_pos = harness._source_byte_position(source, start)
            end_pos = harness._source_byte_position(source, end)
            return {"start_line": start_pos["line"], "start_column": start_pos["column"],
                    "end_line": end_pos["line"], "end_column": end_pos["column"],
                    "source_begin": len(source[:start].encode()),
                    "source_end": len(source[:end].encode())}
        path = str(source_path.resolve())
        return {"source_file": path,
            "source_sha256": "sha256:" + hashlib.sha256(source.encode()).hexdigest(),
            "est_arena_index": expr_id, "copy_expression_id": expr_id,
            "source_expression_id": expr_id + 1,
            "copy_occurrence_span": flat_span(copy_start, copy_end),
            "source_occurrence_span": flat_span(source_start, source_end),
            "statement_occurrence_id": expr_id + 2, "specialization_identity": specialization,
            "specialization_context": specialization, "est_fact_context": "unit:" + specialization,
            "source_type_key": "type:string", "result_type_key": "type:string",
            "required_source_access": "READ", "creates_independent_owner": True,
            "availability": "Present",
            "provenance": {"source_file": path,
                "line": copy_span["start"]["line"],
                "defining_module_id": "module:unit",
                "enclosing_callable_id": specialization or "fn:duplicate"}}

    def authority(self, facts=None):
        return {"status": "available", "facts": facts or [self.fact_row]}

    def _offset(self, span, point):
        return harness._span_offsets(self.rust, {
            "start": span[point], "end": span[point]})[0]

    def write_manifest(self, manifest):
        Path(str(self.rust_path) + ".mossclone.json").write_text(json.dumps(manifest))

    def test_missing_provenance_is_rejected(self):
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1,
                                               self.authority())
        self.assertEqual(audit["status"], "PROVENANCE_MISSING")

    def test_clone_scanner_uses_shared_tokenizer_for_whitespace_forms(self):
        rust = 'value . clone ( )\n"text . clone ( )"\n/* . clone ( ) */\n'
        self.assertEqual(len(harness.clone_occurrences(rust)), 1)

    def test_missing_sidecar_fails_closed_without_clone_or_copy_calls(self):
        self.rust_path.write_text("fn main() {}\n")
        self.source_path.write_text("fn main(): pass\n")
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 0)
        self.assertEqual(audit["status"], "PROVENANCE_MISSING")

    def test_runtime_label_without_source_evidence_is_rejected(self):
        self.write_manifest(self.manifest("runtime_support"))
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1,
                                               self.authority())
        self.assertEqual(audit["status"], "INVALID_PROVENANCE")

    def test_forged_runtime_source_claim_is_rejected(self):
        manifest = self.manifest("runtime_support")
        forged = self.root / "runtime_evidence.txt"
        forged.write_text("// clone String unrelated comment\n")
        relative = forged.relative_to(ROOT).as_posix()
        evidence = manifest["occurrences"][0]["evidence"]
        evidence.update({"repository_evidence_path": relative,
            "repository_evidence_sha256": hashlib.sha256(forged.read_bytes()).hexdigest(),
            "repository_evidence_span": {"start": {"line": 1, "column": 1},
                                         "end": {"line": 1, "column": len(forged.read_text().rstrip()) + 1}},
            "runtime_callsite_id": "forged-reviewed-callsite"})
        self.write_manifest(manifest)
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1,
                                               self.authority())
        self.assertEqual(audit["status"], "INVALID_PROVENANCE")

    def test_incorrect_source_span_is_rejected(self):
        manifest = self.manifest()
        manifest["source_copy_occurrences"][0]["source_span"] = self.rust_occ
        self.write_manifest(manifest)
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1,
                                               self.authority())
        self.assertEqual(audit["status"], "INVALID_PROVENANCE")

    def test_stale_manifest_digest_is_rejected(self):
        manifest = self.manifest()
        manifest["rust_sha256"] = "0" * 64
        self.write_manifest(manifest)
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1,
                                               self.authority())
        self.assertEqual(audit["status"], "INVALID_PROVENANCE")

    def test_forged_nonempty_evidence_no_longer_passes(self):
        manifest = self.manifest()
        evidence = manifest["occurrences"][0]["evidence"]
        evidence.update({"semantic_type": "FileIO", "ownership_fact": "CONSUME_NOT_READ",
                         "semantic_identity": "forged:unrelated-binding"})
        self.write_manifest(manifest)
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1,
                                               self.authority())
        self.assertEqual(audit["status"], "INVALID_PROVENANCE")

    def test_missing_checker_authority_is_blocked(self):
        self.write_manifest(self.manifest())
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1)
        self.assertEqual(audit["status"], "PROVENANCE_MISSING")

    def test_perturbed_checked_fact_is_rejected(self):
        manifest = self.manifest()
        manifest["checked_copy_facts"][0]["facts"][0]["required_source_access"] = "CONSUME"
        self.write_manifest(manifest)
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1,
                                               self.authority())
        self.assertEqual(audit["status"], "INVALID_PROVENANCE")

    def test_duplicate_checker_fact_is_rejected(self):
        manifest = self.manifest()
        fact = self.fact()
        self.write_manifest(manifest)
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1,
            {"status": "available", "facts": [fact, fact]})
        self.assertEqual(audit["status"], "INVALID_PROVENANCE")

    def test_two_specialization_facts_share_one_lexical_copy_occurrence(self):
        source_path = ROOT / "tests/phase28/team_c_r7_010/copy_multi_specialization.moss"
        source = source_path.read_bytes().decode("utf-8")
        copy_span = harness.moss_copy_call_spans(source)[0]
        first = self.fact_for_source(source_path, source, copy_span,
                                     "fn:duplicate<First>", 101)
        second = self.fact_for_source(source_path, source, copy_span,
                                      "fn:duplicate<Second>", 202)
        template = self.fact_for_source(source_path, source, copy_span, "", 1)
        self.source_path = source_path
        self.source = source
        self.source_occ = copy_span
        manifest = self.manifest_for_facts([
            (second, copy_span["copy_id"]), (first, copy_span["copy_id"])])
        manifest["checked_copy_facts"][0]["facts"].append(template)
        manifest["checked_copy_facts"][0]["facts"].sort(key=harness.typed_copy_fact_identity)
        self.write_manifest(manifest)
        audit = harness.audit_clone_provenance(self.rust_path, source_path, "debug", 1,
                                               self.authority([template, first, second]))
        self.assertEqual(audit["status"], "PASS", audit.get("error"))
        self.assertEqual(audit["authorized"], 1)

        result = harness.validate_checked_copy_authority(
            source, source_path, [copy_span],
            [{"copy_id": copy_span["copy_id"], "source_span": copy_span,
              "semantic_status": "implemented", "rust_clone_spans": []}],
            [{"copy_id": copy_span["copy_id"],
              "facts": sorted([second, first, template], key=harness.typed_copy_fact_identity)}],
            {"status": "available", "facts": [template, first, second]})
        self.assertEqual(set(result), {copy_span["copy_id"]})
        self.assertEqual(len(result[copy_span["copy_id"]]["checked_copy_facts"]), 3)

    def test_swapped_valid_source_copy_links_are_rejected(self):
        source_path = self.root / "two_copies.moss"
        source = "left = copy(item)\nright = copy(other)\n"
        source_path.write_text(source)
        source_spans = harness.moss_copy_call_spans(source)
        first = self.fact_for_source(source_path, source, source_spans[0], "main", 301)
        second = self.fact_for_source(source_path, source, source_spans[1], "main", 401)
        self.source_path = source_path
        self.source = source
        manifest = self.manifest_for_facts([
            (first, source_spans[0]["copy_id"]),
            (second, source_spans[1]["copy_id"])])
        manifest["occurrences"][0]["source_copy_id"], \
            manifest["occurrences"][1]["source_copy_id"] = \
            manifest["occurrences"][1]["source_copy_id"], \
            manifest["occurrences"][0]["source_copy_id"]
        self.write_manifest(manifest)
        audit = harness.audit_clone_provenance(self.rust_path, source_path, "debug", 2,
                                               self.authority([first, second]))
        self.assertEqual(audit["status"], "INVALID_PROVENANCE")

    def test_extra_per_fact_helper_caller_is_rejected(self):
        manifest = self.manifest()
        fact_id = harness.typed_copy_fact_identity(self.fact_row)
        helper = "__moss_copy_value_" + fact_id.removeprefix("typed-copy-fact-v1:")[:16]
        self.rust += f"fn extra() {{ let _ = {helper}(other); }}\n"
        self.rust_path.write_text(self.rust)
        manifest["rust_sha256"] = hashlib.sha256(self.rust.encode()).hexdigest()
        self.write_manifest(manifest)
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1,
                                               self.authority())
        self.assertEqual(audit["status"], "INVALID_PROVENANCE")

    def test_forged_raw_rust_fact_marker_is_rejected(self):
        manifest = self.manifest()
        fact_id = harness.typed_copy_fact_identity(self.fact_row)
        forged_id = "typed-copy-fact-v1:" + "0" * 64
        self.rust = self.rust.replace(f"Moss copy fact call: {fact_id}",
                                      f"Moss copy fact call: {forged_id}")
        self.rust_path.write_text(self.rust)
        manifest["rust_sha256"] = hashlib.sha256(self.rust.encode()).hexdigest()
        self.write_manifest(manifest)
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1,
                                               self.authority())
        self.assertEqual(audit["status"], "INVALID_PROVENANCE")

    def test_emitted_base_callable_without_copy_helper_is_rejected(self):
        manifest = self.manifest()
        self.rust = "// Moss tooling begin|function|main@1|main|main\nfn main() {}\n"
        self.rust_path.write_text(self.rust)
        manifest["source_copy_occurrences"][0]["rust_clone_spans"] = []
        manifest["occurrences"] = []
        manifest["rust_sha256"] = hashlib.sha256(self.rust.encode()).hexdigest()
        self.write_manifest(manifest)
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1,
                                               self.authority())
        self.assertEqual(audit["status"], "INVALID_PROVENANCE")

    def test_ambiguous_emitted_callable_normalization_is_rejected(self):
        manifest = self.manifest()
        self.rust += "// Moss tooling begin|function|main@2|main2|main2\n"
        self.rust_path.write_text(self.rust)
        manifest["rust_sha256"] = hashlib.sha256(self.rust.encode()).hexdigest()
        self.write_manifest(manifest)
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1,
                                               self.authority())
        self.assertEqual(audit["status"], "INVALID_PROVENANCE")

    def test_unknown_copy_authority_top_level_field_is_rejected(self):
        self.write_manifest(self.manifest())
        authority = self.authority()
        authority["claimed_by_sidecar"] = True
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1,
                                               authority)
        self.assertEqual(audit["status"], "INVALID_PROVENANCE")

    def test_exact_source_digest_and_byte_spans_preserve_crlf_and_utf8(self):
        raw = "prefix = \"é\"; value = copy(item)\r\n".encode("utf-8")
        self.source_path.write_bytes(raw)
        self.source = raw.decode("utf-8")
        self.source_occ = harness.moss_copy_call_spans(self.source)[0]
        self.fact_row = self.fact()
        manifest = self.manifest()
        self.write_manifest(manifest)
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1,
                                               self.authority())
        self.assertEqual(audit["status"], "PASS", audit.get("error"))

    def test_query_uses_first_source_copy_location_and_returns_checker_fact_set(self):
        authority = {"status": "available", "facts": [self.fact()]}
        with mock.patch.object(harness, "run_process", return_value=(0,
                json.dumps({"ok": True, "result": {"copy_authority": authority}}), "", 0)) as run:
            actual = harness.query_copy_authority(Path("./moss"), self.source_path)
        self.assertEqual(actual, authority)
        self.assertEqual(run.call_count, 7)
        args = run.call_args_list[0].args[0]
        self.assertEqual(args[1:3], ["ownership", "at:1:9"])

    def test_query_target_uses_byte_column_after_utf8_on_same_line(self):
        raw = "prefix = \"é\"; value = copy(item)\n".encode("utf-8")
        self.source_path.write_bytes(raw)
        self.source = raw.decode("utf-8")
        copies = harness.moss_copy_call_spans(self.source)
        expected = copies[0]["start"]["column"]
        authority = {"status": "available", "facts": []}
        with mock.patch.object(harness, "run_process", return_value=(0,
                json.dumps({"ok": True, "result": {"copy_authority": authority}}), "", 0)) as run:
            harness.query_copy_authority(Path("./moss"), self.source_path)
        self.assertEqual(run.call_args_list[0].args[0][2], f"at:1:{expected}")
        self.assertGreater(expected, self.source.index("copy") + 1)

    def test_query_retries_exact_location_by_kind_and_requires_authority_agreement(self):
        raw = 'fn main():\n  value = sink("é", copy(item))\n'
        self.source_path.write_text(raw)
        self.source = raw
        self.source_occ = harness.moss_copy_call_spans(raw)[0]
        target = harness.moss_copy_call_spans(raw)[0]["start"]
        authority = {"status": "available", "facts": [self.fact()]}
        responses = iter([
            (1, "", "ambiguous location", 0),
            (1, "", "not a binding", 0),
            (0, json.dumps({"ok": True, "result": {"copy_authority": authority}}), "", 0),
            (0, json.dumps({"ok": True, "result": {"copy_authority": authority}}), "", 0),
            (1, "", "not a function", 0),
            (1, "", "not a method", 0),
            (1, "", "not a handler", 0),
        ])
        with mock.patch.object(harness, "run_process", side_effect=lambda *_: next(responses)) as run:
            actual = harness.query_copy_authority(Path("./moss"), self.source_path)
        self.assertEqual(actual, authority)
        args = [call.args[0] for call in run.call_args_list]
        self.assertEqual([x[2] for x in args], [f"at:{target['line']}:{target['column']}"] * 7)
        self.assertEqual([x[x.index("--kind") + 1] for x in args[1:]],
                         ["binding", "call", "statement", "function", "method", "handler"])

    def test_query_conflicting_successful_retries_fail_closed(self):
        first = {"status": "available", "facts": [self.fact()]}
        second_fact = self.fact()
        second_fact["specialization_identity"] = "different"
        second = {"status": "available", "facts": [second_fact]}
        responses = iter([
            (1, "", "ambiguous", 0), (1, "", "missing", 0),
            (0, json.dumps({"ok": True, "result": {"copy_authority": first}}), "", 0),
            (0, json.dumps({"ok": True, "result": {"copy_authority": second}}), "", 0),
            (1, "", "missing", 0), (1, "", "missing", 0), (1, "", "missing", 0),
        ])
        with mock.patch.object(harness, "run_process", side_effect=lambda *_: next(responses)):
            actual = harness.query_copy_authority(Path("./moss"), self.source_path)
        self.assertEqual(actual["status"], "conflicting")

    @unittest.skipUnless(os.environ.get("MOSS_PHASE28_QUERY_COMPILER"),
                         "live checker query compiler not configured")
    def test_live_checker_query_rows_match_independent_source_scanner(self):
        compiler = Path(os.environ["MOSS_PHASE28_QUERY_COMPILER"]).resolve()
        fixture = ROOT / "tests/phase28/oracle_r7_005_1/fixtures/oracle_10a_explicit_copy_only.moss"
        utf8_source = self.root / "copy_same_line_utf8.moss"
        utf8_source.write_text('''type Record:
  label: String

fn sink(label: String, value: Record):
  echo label
  echo value.label

fn main():
  var record = Record(label: "source")
  sink("é", copy(record))
''')
        multi_specialization = ROOT / "tests/phase28/team_c_r7_010/copy_multi_specialization.moss"
        for source_path in (fixture, utf8_source, multi_specialization):
            source = source_path.read_bytes().decode("utf-8")
            code, stdout, stderr, _ = harness.run_process(
                [str(compiler), "check", "--json", str(source_path)])
            self.assertEqual(code, 0, stdout + stderr)
            authority = harness.query_copy_authority(compiler, source_path)
            self.assertEqual(authority.get("status"), "available", authority)
            if source_path == multi_specialization:
                self.assertGreaterEqual(len(authority["facts"]), 2)
                self.assertEqual(len({harness._flat_fact_span_key(f["copy_occurrence_span"])
                                      for f in authority["facts"]}), 1)
                specialized = {f["specialization_identity"] for f in authority["facts"]
                               if f["specialization_identity"]}
                self.assertEqual(len(specialized), 2)
            copies = harness.moss_copy_call_spans(source)
            grouped = {span["copy_id"]: [] for span in copies}
            for fact in authority["facts"]:
                key = harness._flat_fact_span_key(fact["copy_occurrence_span"])
                matched = [span for span in copies
                           if harness._nested_span_key(span) == key]
                self.assertEqual(len(matched), 1)
                grouped[matched[0]["copy_id"]].append(fact)
            rows = [{"copy_id": span["copy_id"], "source_span": span,
                     "semantic_status": "implemented", "rust_clone_spans": []}
                    for span in copies]
            fact_groups = [{"copy_id": span["copy_id"],
                            "facts": sorted(grouped[span["copy_id"]],
                                key=harness.typed_copy_fact_identity)}
                           for span in copies]
            result = harness.validate_checked_copy_authority(
                source, source_path, copies, rows, fact_groups, authority)
            self.assertEqual(len(result), len(copies))


if __name__ == "__main__":
    unittest.main()
