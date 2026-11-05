#!/usr/bin/env python3
"""Check imported-package copy fact lookup uses the canonical checked Program."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
if len(sys.argv) != 3:
    raise SystemExit("usage: check_canonical_copy_context.py COMPILER WORK_DIR")
COMPILER = Path(sys.argv[1]).resolve()
WORK = Path(sys.argv[2]).resolve()
if ROOT / "tmp" not in WORK.parents:
    raise SystemExit(f"work directory must be under {ROOT / 'tmp'}")
shutil.rmtree(WORK, ignore_errors=True)
source_root = WORK / "regex"
shutil.copytree(ROOT / "packages/regex", source_root)
source = source_root / "src/main.moss"
text = source.read_text(encoding="utf-8")

def replace(old: str, new: str, expected: int) -> None:
    global text
    actual = text.count(old)
    if actual == expected:
        text = text.replace(old, new)
    elif text.count(new) != expected:
        raise AssertionError(
            f"expected {expected} occurrences of either {old!r} or {new!r}; "
            f"found {actual} and {text.count(new)}")


# Temporary, source-level checked-copy repairs supplied by the L7 package
# compatibility work. They let this consumer gate reach package codegen while
# leaving the tracked fixture and frozen package source unchanged.
replace("push_str(chars_list, raw_chars[r_idx])",
        "push_str(chars_list, copy(raw_chars[r_idx]))", 1)
replace("push_str(classes_list, raw_classes[r_idx])",
        "push_str(classes_list, copy(raw_classes[r_idx]))", 1)
replace("add_tok(post_kinds, post_chars, post_classes, post_negs, tk, tc, tclass, tneg)",
        "add_tok(post_kinds, post_chars, post_classes, post_negs, tk, copy(tc), copy(tclass), tneg)", 2)
replace("add_tok(op_kinds, op_chars, op_classes, op_negs, tk, tc, tclass, tneg)",
        "add_tok(op_kinds, op_chars, op_classes, op_negs, tk, copy(tc), copy(tclass), tneg)", 2)
replace("push_str(st_chars, pc)", "push_str(st_chars, copy(pc))", 1)
replace("push_str(st_classes, pcl)", "push_str(st_classes, copy(pcl))", 1)
replace("\n        if char_matches(re.chars[st_id], chars_vec[pos]):",
        "\n        let state_char = copy(re.chars[st_id])\n"
        "        if char_matches(state_char, chars_vec[pos]):", 1)
replace("\n            let in_cls = char_in_class(re.classes[st_id], chars_vec[pos])",
        "\n            let class_chars = copy(re.classes[st_id])\n"
        "            let in_cls = char_in_class(class_chars, chars_vec[pos])", 1)
replace("\n            if char_matches(re.chars[st_id], chars_vec[pos]):",
        "\n            let state_char = copy(re.chars[st_id])\n"
        "            if char_matches(state_char, chars_vec[pos]):", 1)
replace("\n                let in_cls = char_in_class(re.classes[st_id], chars_vec[pos])",
        "\n                let class_chars = copy(re.classes[st_id])\n"
        "                let in_cls = char_in_class(class_chars, chars_vec[pos])", 1)
source.write_text(text, encoding="utf-8")

check = subprocess.run([str(COMPILER), "check", str(source), "--json"],
                       cwd=ROOT, text=True, capture_output=True)
if check.returncode:
    raise AssertionError(f"temporary regex source check failed:\n{check.stdout}{check.stderr}")
if not json.loads(check.stdout).get("ok"):
    raise AssertionError(f"temporary regex source was rejected: {check.stdout}")

for hook, expected in (
    ("MOSS_TEST_CONSUMER_DROP_COPY_FACT", "required checked copy fact unavailable"),
    ("MOSS_TEST_CONSUMER_PERTURB_COPY_FACT", "required checked copy fact contradicts requirement"),
):
    shutil.rmtree(source_root / "build", ignore_errors=True)
    env = os.environ.copy()
    env["MOSS"] = str(COMPILER)
    env[hook] = "1"
    result = subprocess.run([str(ROOT / "margo"), "build", "--json"],
                            cwd=source_root, env=env, text=True,
                            capture_output=True)
    message = result.stdout + result.stderr
    if result.returncode == 0 or expected not in message:
        raise AssertionError(
            f"canonical copy corruption {hook} did not fail at exact fact lookup:\n"
            f"exit={result.returncode}\n{message}")
    if "explicit copy lacks checked EST context" in message:
        raise AssertionError(f"{hook} failed at the projected Program, not canonical facts")

print("canonical imported-package copy fact drop/perturb checks fail closed")
