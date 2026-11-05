#!/usr/bin/env python3
"""Unit tests for the independent R7-010 Rust duplication candidate scanner."""

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = Path(__file__).with_name("check_phase28_r7_010_clone_inventory.py")
spec = importlib.util.spec_from_file_location("clone_inventory", SCRIPT)
inventory = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = inventory
spec.loader.exec_module(inventory)


class RustCandidateScannerTests(unittest.TestCase):
    def test_comments_strings_raw_strings_and_chars_are_not_code(self):
        source = r'''// value.clone() .cloned()
/* outer .clone() end */
fn f() {
  let text = ".clone() .copied()";
  let live = value.clone();
}
'''
        masked = inventory.mask_non_code(source)
        self.assertEqual(len(inventory.operator_spans(masked, "dot_clone")), 1)
        self.assertEqual(len(inventory.operator_spans(masked, "dot_cloned")), 0)
        self.assertEqual(len(inventory.operator_spans(masked, "dot_copied")), 0)

    def test_raw_strings_and_char_literals_are_not_code(self):
        source = r'''fn f() {
  let raw = br##".clone() .to_vec()"##;
  let live = value.clone();
}
'''
        masked = inventory.mask_non_code(source)
        self.assertEqual(len(inventory.operator_spans(masked, "dot_clone")), 1)
        self.assertEqual(len(inventory.operator_spans(masked, "dot_to_vec")), 0)

    def test_char_literal_is_not_code(self):
        source = r'''fn f() {
  let c = 'x';
  let live = value.clone();
}
'''
        masked = inventory.mask_non_code(source)
        self.assertEqual(len(inventory.operator_spans(masked, "dot_clone")), 1)

    def test_alternate_duplication_and_move_candidates_are_detected(self):
        source = '''
fn f() {
  x.clone_from(&y);
  iter.cloned();
  iter.copied();
  s.to_owned();
  s.to_string();
  bytes.to_vec();
  Clone::clone(&x);
  Arc::clone(&x);
  std::mem::replace(&mut x, y);
  std::mem::take(&mut x);
  std::ptr::read(&x);
  String::from(s);
  Vec::from(xs);
}
'''
        masked = inventory.mask_non_code(source)
        counts = {key: len(inventory.operator_spans(masked, key))
                  for key in inventory.OPERATORS}
        self.assertEqual(counts, {
            "dot_clone": 0,
            "dot_clone_from": 1,
            "dot_cloned": 1,
            "dot_copied": 1,
            "dot_to_owned": 1,
            "dot_to_string": 1,
            "dot_to_vec": 1,
            "qualified_clone": 2,
            "mem_replace": 1,
            "mem_take": 1,
            "ptr_read": 1,
            "string_or_vec_from": 2,
        })

    def test_whitespace_separated_clone_tokens_are_detected(self):
        source = "value . clone ( )\nvalue/* comment */.\nclone(/* x */)\n"
        masked = inventory.mask_non_code(source)
        spans = inventory.operator_spans(masked, "dot_clone")
        self.assertEqual(len(spans), 2)

    def test_moss_clone_derive_is_reported_as_capability_not_invocation(self):
        fact_id = "typed-copy-fact-v1:" + "a" * 64
        source = '''// Moss tooling begin|type|type:Record@2|Record|
#[derive(Clone, Debug)]
struct Record { value: String }
// Moss tooling end|type:Record@2
#[derive(Clone)]
struct RuntimeState { value: usize }
fn __moss_copy_value_aaaaaaaaaaaaaaaa(value: &Record) -> Record { (*value).clone() }
fn explicit_copy(value: &Record) -> Record {
  { /* Moss copy fact call: FACT_ID */ __moss_copy_value_aaaaaaaaaaaaaaaa(value) }
}
'''
        source = source.replace("FACT_ID", fact_id)
        with tempfile.TemporaryDirectory(dir=ROOT / "tmp") as tmp:
            root = Path(tmp)
            path = root / "fixture.rs"
            path.write_text(source)
            capabilities = inventory.inspect_moss_clone_derives(path, root)
            uses = inventory.inspect_moss_clone_derive_uses(path, root, capabilities)
        self.assertEqual(len(capabilities), 1)
        self.assertEqual(capabilities[0]["rust_type"], "Record")
        self.assertEqual(capabilities[0]["moss_region_symbol"], "Record")
        self.assertEqual(capabilities[0]["capability"], "derived Clone implementation")
        self.assertFalse(capabilities[0]["is_invocation"])
        self.assertEqual(len(uses), 1)
        self.assertEqual(uses[0]["rust_type"], "Record")
        self.assertEqual(uses[0]["fact_marker_locator"], fact_id)
        self.assertTrue(uses[0]["helper_prefix_matches_marker"])
        self.assertEqual(uses[0]["authority_status"], "not independently verified")
        masked = inventory.mask_non_code(source)
        self.assertEqual(len(inventory.operator_spans(masked, "dot_clone")), 1)


if __name__ == "__main__":
    unittest.main()
