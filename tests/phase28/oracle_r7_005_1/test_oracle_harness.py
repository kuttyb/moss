#!/usr/bin/env python3
"""Adversarial self-tests for fail-closed oracle validation."""
import hashlib
import copy
import importlib.util
import json
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
        self.rust = "fn helper() { let _copy = value.clone(); }\n"
        self.source = "value = copy(item)\n"
        self.rust_path.write_text(self.rust)
        self.source_path.write_text(self.source)
        self.rust_occ = harness.clone_occurrences(self.rust)[0]
        self.source_occ = harness.moss_copy_call_spans(self.source)[0]

    def tearDown(self):
        self.temp.cleanup()

    def manifest(self, origin="explicit_copy"):
        copy_id = self.source_occ["copy_id"]
        return {"format": "moss-clone-provenance", "version": 2, "mode": "debug",
                "rust_sha256": hashlib.sha256(self.rust.encode()).hexdigest(),
                "source_sha256": hashlib.sha256(self.source.encode()).hexdigest(),
                "source_copy_occurrences": [{"copy_id": copy_id,
                    "source_span": self.source_occ, "semantic_status": "implemented",
                    "rust_clone_spans": [self.rust_occ] if origin == "explicit_copy" else []}],
                "occurrences": [{"rust_span": self.rust_occ, "origin_kind": origin,
                    "source_copy_id": copy_id if origin == "explicit_copy" else None,
                    "evidence": {"rust_context_span": self.rust_occ,
                        "rust_context_sha256": hashlib.sha256(
                            self.rust[self._offset(self.rust_occ, "start"):self._offset(self.rust_occ, "end")].encode()).hexdigest(),
                        "semantic_type": "String", "ownership_fact": "READ", "semantic_identity": "helper:value"}}]}

    @staticmethod
    def _offset(span, point):
        pos = span[point]
        return sum(len(line) for line in "fn helper() { let _copy = value.clone(); }\n".splitlines(keepends=True)[:pos["line"] - 1]) + pos["column"] - 1

    def write_manifest(self, manifest):
        Path(str(self.rust_path) + ".mossclone.json").write_text(json.dumps(manifest))

    def test_missing_provenance_is_rejected(self):
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1)
        self.assertEqual(audit["status"], "PROVENANCE_MISSING")

    def test_missing_sidecar_fails_closed_without_clone_or_copy_calls(self):
        self.rust_path.write_text("fn main() {}\n")
        self.source_path.write_text("fn main(): pass\n")
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 0)
        self.assertEqual(audit["status"], "PROVENANCE_MISSING")

    def test_runtime_label_without_source_evidence_is_rejected(self):
        self.write_manifest(self.manifest("runtime_support"))
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1)
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
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1)
        self.assertEqual(audit["status"], "INVALID_PROVENANCE")

    def test_incorrect_source_span_is_rejected(self):
        manifest = self.manifest()
        manifest["source_copy_occurrences"][0]["source_span"] = self.rust_occ
        self.write_manifest(manifest)
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1)
        self.assertEqual(audit["status"], "INVALID_PROVENANCE")

    def test_stale_manifest_digest_is_rejected(self):
        manifest = self.manifest()
        manifest["rust_sha256"] = "0" * 64
        self.write_manifest(manifest)
        audit = harness.audit_clone_provenance(self.rust_path, self.source_path, "debug", 1)
        self.assertEqual(audit["status"], "INVALID_PROVENANCE")


if __name__ == "__main__":
    unittest.main()
