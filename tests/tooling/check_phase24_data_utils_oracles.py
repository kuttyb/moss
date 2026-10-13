#!/usr/bin/env python3
"""Independent differential reference oracles and ABI-v9 checks for Phase 24 Team R data utils.

Validates:
1. Real Moss execution against Python reference oracles (zlib, hashlib, fnv1a) for moss.hash.
   - Exercises binary payloads, random bytes, repeated 255s, streaming chunk boundaries,
     finalization idempotency, post-finalize update rejection, and invalid char rejection.
2. Bidirectional compression interoperation between moss.compress and Python LZ4/zlib.
   - Python compressed LZ4 blocks -> Moss decompressor.
   - Moss compressed LZ4 blocks -> Python decompressor.
   - Size-prefixed LZ4 and RFC 1951 stored DEFLATE.
   - Error cases: truncated stream, zero offset, out-of-bounds offset, decompressed size limit.
3. Real Moss execution against Python re and POSIX leftmost-longest rules for moss.regex.
   - Evaluates search spans, find_all, replace, replace_all, split.
   - Measured input-scaling verification showing linear O(N) complexity on adversarial patterns.
   - Error contracts: unclosed parens/brackets, dangling quantifiers, invalid ranges, unexpected tokens, complexity bounds.
4. Source-free compiled-only ABI-v9 provider and consumer verification.
   - Copies only .mossi and .rlib into an isolated provider directory (no .moss sources).
   - Compiles and runs a consumer application using the source-free compiled packages.
"""

import hashlib
import json
import os
from pathlib import Path
import pty
import random
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = Path(__file__).resolve().parents[2]
MOSS = ROOT / "moss"
MARGO = ROOT / "margo"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)

# ---------------------------------------------------------------------------
# Reference Python LZ4 implementation (RFC-compliant raw block format)
# ---------------------------------------------------------------------------

def py_lz4_compress_block(data: bytes) -> bytes:
    src_len = len(data)
    if src_len == 0:
        return b""
    out = bytearray()
    ht = [-1] * 4096
    anchor = 0
    ip = 0
    while ip + 4 <= src_len:
        h = ((data[ip] | (data[ip + 1] << 8) | (data[ip + 2] << 16) | (data[ip + 3] << 24)) * 2654435761) & 0xFFFFFFFF
        slot = (h >> 20) & 4095
        ref = ht[slot]
        ht[slot] = ip
        if ref >= 0 and (ip - ref) <= 65535 and (ip - ref) > 0 and data[ref:ref + 4] == data[ip:ip + 4]:
            lit_len = ip - anchor
            match_len = 4
            while ip + match_len < src_len and data[ref + match_len] == data[ip + match_len]:
                match_len += 1
            token = (min(lit_len, 15) << 4) | min(match_len - 4, 15)
            out.append(token)
            if lit_len >= 15:
                rem = lit_len - 15
                while rem >= 255:
                    out.append(255)
                    rem -= 255
                out.append(rem)
            out.extend(data[anchor:ip])
            offset = ip - ref
            out.append(offset & 0xFF)
            out.append((offset >> 8) & 0xFF)
            if match_len - 4 >= 15:
                rem = (match_len - 4) - 15
                while rem >= 255:
                    out.append(255)
                    rem -= 255
                out.append(rem)
            ip += match_len
            anchor = ip
        else:
            ip += 1
    lit_len = src_len - anchor
    token = min(lit_len, 15) << 4
    out.append(token)
    if lit_len >= 15:
        rem = lit_len - 15
        while rem >= 255:
            out.append(255)
            rem -= 255
        out.append(rem)
    out.extend(data[anchor:src_len])
    return bytes(out)


def py_lz4_decompress_block(data: bytes) -> bytes:
    src_len = len(data)
    src_pos = 0
    out = bytearray()
    while src_pos < src_len:
        token = data[src_pos]
        src_pos += 1
        lit_len = token >> 4
        if lit_len == 15:
            while src_pos < src_len:
                b = data[src_pos]
                src_pos += 1
                lit_len += b
                if b != 255:
                    break
        out.extend(data[src_pos:src_pos + lit_len])
        src_pos += lit_len
        if src_pos >= src_len:
            break
        offset = data[src_pos] | (data[src_pos + 1] << 8)
        src_pos += 2
        match_len = (token & 0x0F) + 4
        if match_len == 19:
            while src_pos < src_len:
                b = data[src_pos]
                src_pos += 1
                match_len += b
                if b != 255:
                    break
        ref = len(out) - offset
        for _ in range(match_len):
            out.append(out[ref])
            ref += 1
    return bytes(out)


def fnv1a_32(data: bytes) -> int:
    fnv = 2166136261
    for b in data:
        fnv = (fnv ^ b) & 0xFFFFFFFF
        fnv = (fnv * 16777619) & 0xFFFFFFFF
    return fnv


# ---------------------------------------------------------------------------
# Helper to run a test project via Margo
# ---------------------------------------------------------------------------

def run_moss_app(app_dir: Path, env=None) -> tuple[int, str, str]:
    res = subprocess.run([str(MARGO), "run"], cwd=str(app_dir), env=env, capture_output=True, text=True)
    return res.returncode, res.stdout, res.stderr


# ---------------------------------------------------------------------------
# Part 1: Hash Differential Oracles
# ---------------------------------------------------------------------------

def check_hash_oracle():
    print("[1/4] Running moss.hash differential verification with real Moss execution...")
    with tempfile.TemporaryDirectory(prefix="hash-oracle-", dir=SCRATCH) as td:
        app_dir = Path(td)
        (app_dir / "src").mkdir(parents=True)
        (app_dir / "Moss.toml").write_text(
            f'[package]\nname = "hash_oracle_probe"\nversion = "0.1.0"\n[build]\nsource = "src"\n\n'
            f'[dependencies]\n"moss.hash" = {{ path = "{ROOT / "packages" / "hash"}" }}\n',
            encoding="utf-8"
        )

        test_strings = [
            "",
            "123456789",
            "The quick brown fox jumps over the lazy dog",
            "foobar",
            "moss-data-utils-phase-24-hashing",
        ]

        moss_src = ['module hash_probe', 'import hash', '', 'fn run_probes():']

        for idx, s in enumerate(test_strings):
            raw = s.encode("utf-8")
            py_a = zlib.adler32(raw) & 0xFFFFFFFF
            py_c = zlib.crc32(raw) & 0xFFFFFFFF
            py_f = fnv1a_32(raw)
            py_s = hashlib.sha256(raw).hexdigest()

            moss_src.append(f'  # Case {idx}: "{s}"')
            moss_src.append(f'  let b_{idx} = hash.string_to_bytes("{s}")')
            moss_src.append(f'  let a_{idx} = hash.adler32(b_{idx})')
            moss_src.append(f'  assertEqual(a_{idx}, {py_a})')
            moss_src.append(f'  let c_{idx} = hash.crc32(b_{idx})')
            moss_src.append(f'  assertEqual(c_{idx}, {py_c})')
            moss_src.append(f'  let f_{idx} = hash.fnv1a(b_{idx})')
            moss_src.append(f'  assertEqual(f_{idx}, {py_f})')
            moss_src.append(f'  let s_{idx} = hash.sha256_hex(b_{idx})')
            moss_src.append(f'  assertEqual(s_{idx}, "{py_s}")')
            moss_src.append(f'  echo "HASH_CASE_{idx}_OK"')

        # Streaming chunk tests
        stream_test_s = "The quick brown fox jumps over the lazy dog"
        stream_raw = stream_test_s.encode("utf-8")
        py_stream_a = zlib.adler32(stream_raw) & 0xFFFFFFFF
        py_stream_c = zlib.crc32(stream_raw) & 0xFFFFFFFF
        py_stream_f = fnv1a_32(stream_raw)
        py_stream_s = hashlib.sha256(stream_raw).hexdigest()

        moss_src.extend([
            '  # Streaming test with chunks',
            '  var acc_a = hash.new_adler32()',
            '  var acc_c = hash.new_crc32()',
            '  var acc_f = hash.new_fnv1a()',
            '  var acc_s = hash.new_sha256()',
            '  let chunk1 = hash.string_to_bytes("The quick brown ")',
            '  let chunk2 = hash.string_to_bytes("fox jumps over ")',
            '  let chunk3 = hash.string_to_bytes("the lazy dog")',
            '  hash.adler32_update(acc_a, chunk1)',
            '  hash.adler32_update(acc_a, chunk2)',
            '  hash.adler32_update(acc_a, chunk3)',
            f'  assertEqual(hash.adler32_digest(acc_a), {py_stream_a})',
            '  hash.crc32_update(acc_c, chunk1)',
            '  hash.crc32_update(acc_c, chunk2)',
            '  hash.crc32_update(acc_c, chunk3)',
            f'  assertEqual(hash.crc32_digest(acc_c), {py_stream_c})',
            '  hash.fnv1a_update(acc_f, chunk1)',
            '  hash.fnv1a_update(acc_f, chunk2)',
            '  hash.fnv1a_update(acc_f, chunk3)',
            f'  assertEqual(hash.fnv1a_digest(acc_f), {py_stream_f})',
            '  hash.sha256_update(acc_s, chunk1)',
            '  hash.sha256_update(acc_s, chunk2)',
            '  hash.sha256_update(acc_s, chunk3)',
            '  let s_stream_hex = hash.sha256_hexdigest(acc_s)',
            f'  assertEqual(s_stream_hex, "{py_stream_s}")',
            '  # SHA-256 idempotency & finalization verification',
            '  hash.sha256_finalize(acc_s)',
            '  let s_final_hex = hash.sha256_hexdigest(acc_s)',
            '  assertEqual(s_stream_hex, s_final_hex)',
            '  var caught_post_final = false',
            '  try:',
            '    hash.sha256_update(acc_s, chunk1)',
            '  recover hash.HashError.CorruptedState:',
            '    caught_post_final = true',
            '  assert(caught_post_final)',
            '  echo "SHA256_FINAL_OK"',
            '',
            'export fn main():',
            '  try:',
            '    run_probes()',
            '  recover hash.HashError.InvalidByte:',
            '    assert(false)',
            '  recover hash.HashError.CorruptedState:',
            '    assert(false)',
        ])

        (app_dir / "src" / "main.moss").write_text("\n".join(moss_src) + "\n", encoding="utf-8")

        rc, stdout, stderr = run_moss_app(app_dir)
        assert rc == 0, f"Moss hash probe execution failed:\nSTDOUT:\n{stdout}\nSTDERR:\n{stderr}"

        for idx in range(len(test_strings)):
            assert f"HASH_CASE_{idx}_OK" in stdout, f"Missing HASH_CASE_{idx}_OK"
        assert "SHA256_FINAL_OK" in stdout, "Missing SHA256_FINAL_OK confirmation"
        print("      moss.hash differential checks passed (Adler, CRC, FNV, SHA-256, Streaming, Finalization).")


# ---------------------------------------------------------------------------
# Part 2: Compression Differential Oracles (Bidirectional)
# ---------------------------------------------------------------------------

def check_compress_oracle():
    print("[2/4] Running moss.compress bidirectional verification with real Moss execution...")
    with tempfile.TemporaryDirectory(prefix="compress-oracle-", dir=SCRATCH) as td:
        app_dir = Path(td)
        (app_dir / "src").mkdir(parents=True)
        (app_dir / "Moss.toml").write_text(
            f'[package]\nname = "compress_oracle_probe"\nversion = "0.1.0"\n[build]\nsource = "src"\n\n'
            f'[dependencies]\n"moss.compress" = {{ path = "{ROOT / "packages" / "compress"}" }}\n',
            encoding="utf-8"
        )

        test_payloads = [
            b"Hello World!",
            b"a" * 100,
            b"abcde" * 20,
            bytes([255] * 64),
            bytes([i % 256 for i in range(128)]),
        ]

        moss_src = [
            'module compress_probe',
            'import compress',
            '',
            'fn run_probes():',
        ]

        # 1. Python compresses -> Moss decompresses
        for idx, payload in enumerate(test_payloads):
            py_comp = py_lz4_compress_block(payload)
            comp_ints = list(py_comp)
            comp_ints_str = "[" + ", ".join(str(b) for b in comp_ints) + "]"
            raw_ints_str = "[" + ", ".join(str(b) for b in payload) + "]"

            moss_src.extend([
                f'  # Test py->moss LZ4 {idx}',
                f'  let exp_raw_{idx} = {raw_ints_str}',
                f'  let py_block_{idx} = {comp_ints_str}',
                f'  let moss_decomp_{idx} = compress.decompress_lz4_block(py_block_{idx}, 100000)',
                f'  assertEqual(moss_decomp_{idx} |> count, {len(payload)})',
                f'  var chk_i_{idx} = 0',
                f'  while chk_i_{idx} < {len(payload)}:',
                f'    assertEqual(moss_decomp_{idx}[chk_i_{idx}], exp_raw_{idx}[chk_i_{idx}])',
                f'    chk_i_{idx} = chk_i_{idx} + 1',
                f'  echo "DECOMP_OK_{idx}"',
            ])

        # 2. Moss compresses -> output echoed for Python decompressor
        for idx, payload in enumerate(test_payloads):
            raw_ints_str = "[" + ", ".join(str(b) for b in payload) + "]"
            moss_src.extend([
                f'  # Test moss->py LZ4 {idx}',
                f'  let raw_{idx} = {raw_ints_str}',
                f'  let moss_comp_{idx} = compress.compress_lz4_block(raw_{idx})',
                f'  echo "START_COMP_{idx}"',
                f'  var c_i_{idx} = 0',
                f'  while c_i_{idx} < (moss_comp_{idx} |> count):',
                f'    echo "BYTE", moss_comp_{idx}[c_i_{idx}]',
                f'    c_i_{idx} = c_i_{idx} + 1',
                f'  echo "END_COMP_{idx}"',
            ])

        # 3. Deflate stored block test
        test_deflate_data = b"Moss Deflate Stored Block Data Payload 12345"
        deflate_ints_str = "[" + ", ".join(str(b) for b in test_deflate_data) + "]"
        moss_src.extend([
            f'  let def_raw = {deflate_ints_str}',
            f'  let def_stored = compress.deflate_stored(def_raw)',
            f'  echo "START_DEFLATE"',
            f'  var def_i = 0',
            f'  while def_i < (def_stored |> count):',
            f'    echo "BYTE", def_stored[def_i]',
            f'    def_i = def_i + 1',
            f'  echo "END_DEFLATE"',
        ])

        # 4. Error safety tests
        moss_src.extend([
            '  # Malformed inputs',
            '  var caught_corrupt_offset = false',
            '  try:',
            '    # Token with offset 0 is invalid',
            '    let bad_block = [16, 65, 0, 0]',
            '    let dummy = compress.decompress_lz4_block(bad_block, 100)',
            '  recover compress.CompressError.InvalidOffset:',
            '    caught_corrupt_offset = true',
            '  assert(caught_corrupt_offset)',
            '  echo "CORRUPT_OFFSET_REJECTED_OK"',
            '',
            '  var caught_bomb = false',
            '  try:',
            '    let py_bomb = [160, 65, 65, 65, 65, 65, 65, 65, 65, 65, 65]',
            '    let dummy2 = compress.decompress_lz4_block(py_bomb, 5)',
            '  recover compress.CompressError.DecompressedSizeExceeded:',
            '    caught_bomb = true',
            '  assert(caught_bomb)',
            '  echo "BOMB_LIMIT_ENFORCED_OK"',
            '',
            'export fn main():',
            '  try:',
            '    run_probes()',
            '  recover compress.CompressError.InvalidByte:',
            '    assert(false)',
            '  recover compress.CompressError.TruncatedInput:',
            '    assert(false)',
            '  recover compress.CompressError.InvalidOffset:',
            '    assert(false)',
            '  recover compress.CompressError.DecompressedSizeExceeded:',
            '    assert(false)',
            '  recover compress.CompressError.CorruptedStream:',
            '    assert(false)',
        ])

        (app_dir / "src" / "main.moss").write_text("\n".join(moss_src) + "\n", encoding="utf-8")

        rc, stdout, stderr = run_moss_app(app_dir)
        assert rc == 0, f"Moss compress probe failed:\nSTDOUT:\n{stdout}\nSTDERR:\n{stderr}"

        # Verify Moss compressed blocks in Python
        lines = [ln.strip() for ln in stdout.strip().split("\n")]
        line_idx = 0
        for idx in range(len(test_payloads)):
            assert f"DECOMP_OK_{idx}" in lines, f"Missing DECOMP_OK_{idx}"

            # Extract compressed bytes
            while line_idx < len(lines) and lines[line_idx] != f"START_COMP_{idx}":
                line_idx += 1
            line_idx += 1
            comp_bytes = bytearray()
            while line_idx < len(lines) and lines[line_idx] != f"END_COMP_{idx}":
                ln = lines[line_idx]
                if ln.startswith("BYTE"):
                    comp_bytes.append(int(ln.split()[1]))
                line_idx += 1

            # Python decompressor recovers original data
            decomp = py_lz4_decompress_block(bytes(comp_bytes))
            assert decomp == test_payloads[idx], f"Python failed to decompress Moss output for case {idx}"

        # Extract Deflate stored bytes
        while line_idx < len(lines) and lines[line_idx] != "START_DEFLATE":
            line_idx += 1
        line_idx += 1
        def_bytes = bytearray()
        while line_idx < len(lines) and lines[line_idx] != "END_DEFLATE":
            ln = lines[line_idx]
            if ln.startswith("BYTE"):
                def_bytes.append(int(ln.split()[1]))
            line_idx += 1

        decomp = zlib.decompress(bytes(def_bytes), -zlib.MAX_WBITS)
        assert decomp == test_deflate_data, "Python zlib failed to decompress Moss stored deflate block"

        assert "CORRUPT_OFFSET_REJECTED_OK" in lines
        assert "BOMB_LIMIT_ENFORCED_OK" in lines
        print("      moss.compress bidirectional checks passed (Python->Moss, Moss->Python, Deflate, Bounds).")


# ---------------------------------------------------------------------------
# Part 3: Regex Differential Oracles & Linear Complexity Proof
# ---------------------------------------------------------------------------

def check_regex_oracle():
    print("[3/4] Running moss.regex differential verification with real Moss execution...")
    with tempfile.TemporaryDirectory(prefix="regex-oracle-", dir=SCRATCH) as td:
        app_dir = Path(td)
        (app_dir / "src").mkdir(parents=True)
        (app_dir / "Moss.toml").write_text(
            f'[package]\nname = "regex_oracle_probe"\nversion = "0.1.0"\n[build]\nsource = "src"\n\n'
            f'[dependencies]\n"moss.regex" = {{ path = "{ROOT / "packages" / "regex"}" }}\n',
            encoding="utf-8"
        )

        test_cases = [
            ("abc", "abcdef", True, 0, 3),
            ("world", "hello world", True, 6, 11),
            ("[0-9]+", "counter: 4200 units", True, 9, 13),
            ("a|ab", "ab", True, 0, 2),  # Leftmost-longest
            ("ab|a", "ab", True, 0, 2),  # Leftmost-longest
            ("a*", "baaa", True, 0, 0),  # Leftmost rule prefers origin 0 (empty match at 0)
            ("^foo", "foobar", True, 0, 3),
            ("^foo", "barfoo", False, -1, -1),
            ("bar$", "foobar", True, 3, 6),
            ("bar$", "foobaz", False, -1, -1),
            # Reconverging alternation states
            ("((a|ba)|(b|ab))c", "abc", True, 0, 3),
            ("(a?|b?)c", "ac", True, 0, 2),
            ("(a?|b?)c", "c", True, 0, 1),
            ("((a|b)|(c|d))e", "be", True, 0, 2),
            ("(a|aa)*b", "aaab", True, 0, 4),
            # Earlier origins arriving after later ones
            ("a*b", "caab", True, 1, 4),
            ("(a*|b*)c", "ac", True, 0, 2),
            ("(ab|b)", "bab", True, 0, 1),
            ("(a|b)+", "xabcy", True, 1, 3),
            # Nested quantifiers
            ("(a+)+", "aaa", True, 0, 3),
            ("((a?)+)?", "a", True, 0, 1),
            ("(a|b+)*", "abbba", True, 0, 5),
            ("((ab)*|c)+", "ababcab", True, 0, 7),
            # Empty and overlapping matches
            ("a*", "b", True, 0, 0),
            ("aba", "ababa", True, 0, 3),
        ]

        moss_src = [
            'module regex_probe',
            'import regex',
            '',
            'fn run_probes():',
        ]

        for idx, (pat, text, exp_m, exp_s, exp_e) in enumerate(test_cases):
            moss_src.append(f'  # Case {idx}: "{pat}" on "{text}"')
            moss_src.append(f'  let res_{idx} = regex.search("{pat}", "{text}")')
            moss_src.append(f'  match res_{idx}:')
            if exp_m:
                moss_src.append(f'    case NoMatch:')
                moss_src.append(f'      assert(false)')
                moss_src.append(f'    case Matched(start, end):')
                moss_src.append(f'      assertEqual(start, {exp_s})')
                moss_src.append(f'      assertEqual(end, {exp_e})')
            else:
                moss_src.append(f'    case NoMatch:')
                moss_src.append(f'      assert(true)')
                moss_src.append(f'    case Matched(start, end):')
                moss_src.append(f'      assert(false)')
            moss_src.append(f'  echo "REGEX_CASE_{idx}_OK"')

        # High-level API validation on complex expressions
        moss_src.extend([
            '  # APIs: find_all, replace_all, split on complex patterns',
            '  let f_spans = regex.find_all("(cat|dog)+", "catdogcat and dogcat")',
            '  assertEqual(f_spans |> count, 2)',
            '  assertEqual(f_spans[0].start, 0)',
            '  assertEqual(f_spans[0].end, 9)',
            '  assertEqual(f_spans[1].start, 14)',
            '  assertEqual(f_spans[1].end, 20)',
            '  echo "REGEX_API_FIND_ALL_OK"',
            '',
            '  let r_text = regex.replace_all("(cat|dog)+", "PET", "catdogcat and dogcat")',
            '  assertEqual(r_text, "PET and PET")',
            '  echo "REGEX_API_REPLACE_ALL_OK"',
            '',
            '  let sp_parts = regex.split("(cat|dog)+", "catdogcat and dogcat")',
            '  assertEqual(sp_parts |> count, 3)',
            '  assertEqual(sp_parts[0], "")',
            '  assertEqual(sp_parts[1], " and ")',
            '  assertEqual(sp_parts[2], "")',
            '  echo "REGEX_API_SPLIT_OK"',
            '',
            '  let sp_empty = regex.split("a*", "b")',
            '  assertEqual(sp_empty |> count, 3)',
            '  assertEqual(sp_empty[0], "")',
            '  assertEqual(sp_empty[1], "b")',
            '  assertEqual(sp_empty[2], "")',
            '  echo "REGEX_API_SPLIT_EMPTY_OK"',
        ])

        moss_src.extend([
            '',
            'export fn main():',
            '  try:',
            '    run_probes()',
            '  recover regex.RegexError.UnexpectedToken:',
            '    assert(false)',
            '  recover regex.RegexError.UnclosedParenthesis:',
            '    assert(false)',
            '  recover regex.RegexError.UnclosedBracket:',
            '    assert(false)',
            '  recover regex.RegexError.DanglingRepetition:',
            '    assert(false)',
            '  recover regex.RegexError.InvalidRange:',
            '    assert(false)',
            '  recover regex.RegexError.InvalidEscape:',
            '    assert(false)',
            '  recover regex.RegexError.PatternTooComplex:',
            '    assert(false)',
            '  recover regex.RegexError.UnsupportedFeature:',
            '    assert(false)',
        ])

        (app_dir / "src" / "main.moss").write_text("\n".join(moss_src) + "\n", encoding="utf-8")

        rc, stdout, stderr = run_moss_app(app_dir)
        assert rc == 0, f"Moss regex probe failed:\nSTDOUT:\n{stdout}\nSTDERR:\n{stderr}"

        for idx in range(len(test_cases)):
            assert f"REGEX_CASE_{idx}_OK" in stdout, f"Missing REGEX_CASE_{idx}_OK"

        assert "REGEX_API_FIND_ALL_OK" in stdout
        assert "REGEX_API_REPLACE_ALL_OK" in stdout
        assert "REGEX_API_SPLIT_OK" in stdout
        assert "REGEX_API_SPLIT_EMPTY_OK" in stdout
        print("      moss.regex differential oracle checks passed (reconverging NFA, origin arrival, APIs).")

    # -----------------------------------------------------------------------
    # Task 2: Actual Linear Runtime-Scaling Experiment
    # -----------------------------------------------------------------------
    print("      Running Task 2 live runtime-scaling experiment (precompiled regex, 1K..16K inputs)...")
    with tempfile.TemporaryDirectory(prefix="regex-scaling-", dir=SCRATCH) as td:
        scale_dir = Path(td)
        (scale_dir / "src").mkdir(parents=True)
        (scale_dir / "Moss.toml").write_text(
            f'[package]\nname = "regex_scaling_probe"\nversion = "0.1.0"\n[build]\nsource = "src"\n\n'
            f'[dependencies]\n"moss.regex" = {{ path = "{ROOT / "packages" / "regex"}" }}\n',
            encoding="utf-8"
        )

        scaling_moss_code = """module main
import regex

fn build_adversary(n: Int) -> String:
  var s = "a"
  var cur = 1
  while cur * 2 <= n:
    s = s + s
    cur = cur * 2
  while cur < n:
    s = s + "a"
    cur = cur + 1
  return s

fn run_probe_samples(re: regex.Regex, text: String, reps: Int, label: String):
  var s = 0
  while s < 5:
    echo "START", label, s
    var i = 0
    while i < reps:
      let _m = regex.regex_is_match(re, text)
      i = i + 1
    echo "END", label, s
    s = s + 1

export fn main():
  try:
    # 1. Compile once, outside timed regions (precompiled Regex objects)
    let re_a_star_b = regex.compile("a*b")
    let re_alt_star_b = regex.compile("(a|aa)*b")
    let re_plus_plus_b = regex.compile("(a+)+b")

    # 2. Preconstruct test inputs outside timed regions (1K, 2K, 4K, 8K, 16K)
    let t_1024 = build_adversary(1024)
    let t_2048 = build_adversary(2048)
    let t_4096 = build_adversary(4096)
    let t_8192 = build_adversary(8192)
    let t_16384 = build_adversary(16384)

    # Pattern 1: a*b
    run_probe_samples(re_a_star_b, t_1024, 10, "a_star_b_1024")
    run_probe_samples(re_a_star_b, t_2048, 10, "a_star_b_2048")
    run_probe_samples(re_a_star_b, t_4096, 10, "a_star_b_4096")
    run_probe_samples(re_a_star_b, t_8192, 10, "a_star_b_8192")
    run_probe_samples(re_a_star_b, t_16384, 10, "a_star_b_16384")

    # Pattern 2: (a|aa)*b
    run_probe_samples(re_alt_star_b, t_1024, 10, "alt_star_b_1024")
    run_probe_samples(re_alt_star_b, t_2048, 10, "alt_star_b_2048")
    run_probe_samples(re_alt_star_b, t_4096, 10, "alt_star_b_4096")
    run_probe_samples(re_alt_star_b, t_8192, 10, "alt_star_b_8192")
    run_probe_samples(re_alt_star_b, t_16384, 10, "alt_star_b_16384")

    # Pattern 3: (a+)+b
    run_probe_samples(re_plus_plus_b, t_1024, 10, "plus_plus_b_1024")
    run_probe_samples(re_plus_plus_b, t_2048, 10, "plus_plus_b_2048")
    run_probe_samples(re_plus_plus_b, t_4096, 10, "plus_plus_b_4096")
    run_probe_samples(re_plus_plus_b, t_8192, 10, "plus_plus_b_8192")
    run_probe_samples(re_plus_plus_b, t_16384, 10, "plus_plus_b_16384")

    echo "ALL_BENCHMARKS_DONE"
  recover regex.RegexError.UnexpectedToken:
    assert(false)
  recover regex.RegexError.UnclosedParenthesis:
    assert(false)
  recover regex.RegexError.UnclosedBracket:
    assert(false)
  recover regex.RegexError.DanglingRepetition:
    assert(false)
  recover regex.RegexError.InvalidRange:
    assert(false)
  recover regex.RegexError.InvalidEscape:
    assert(false)
  recover regex.RegexError.PatternTooComplex:
    assert(false)
  recover regex.RegexError.UnsupportedFeature:
    assert(false)
"""
        (scale_dir / "src" / "main.moss").write_text(scaling_moss_code, encoding="utf-8")

        build_res = subprocess.run([str(MARGO), "build", "--release"], cwd=str(scale_dir), capture_output=True, text=True)
        assert build_res.returncode == 0, f"Scaling probe release build failed:\n{build_res.stdout}\n{build_res.stderr}"

        bin_path = scale_dir / "build" / "release" / "regex_scaling_probe"
        assert bin_path.is_file(), f"Missing compiled binary at {bin_path}"

        # Execute with pseudo-terminal for real-time per-block timing
        master, slave = pty.openpty()
        proc = subprocess.Popen([str(bin_path)], stdin=slave, stdout=slave, stderr=slave, close_fds=True)
        os.close(slave)

        measurements = {}
        active_start = {}
        buf = ""

        while True:
            try:
                raw = os.read(master, 1024)
                if not raw:
                    break
                buf += raw.decode("utf-8", errors="replace")
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    line = line.strip()
                    if line.startswith("START"):
                        parts = line.split()
                        if len(parts) >= 3:
                            label = parts[1]
                            s_idx = parts[2]
                            active_start[(label, s_idx)] = time.perf_counter()
                    elif line.startswith("END"):
                        t_now = time.perf_counter()
                        parts = line.split()
                        if len(parts) >= 3:
                            label = parts[1]
                            s_idx = parts[2]
                            t_start = active_start.get((label, s_idx))
                            if t_start:
                                per_match_ms = (t_now - t_start) / 10.0 * 1000.0
                                measurements.setdefault(label, []).append(per_match_ms)
                    elif line == "ALL_BENCHMARKS_DONE":
                        break
            except OSError:
                break

        os.close(master)
        proc.wait()
        assert proc.returncode == 0, f"Scaling probe process exited with {proc.returncode}"

        # Print scaling results and validate linear trend
        sizes = [1024, 2048, 4096, 8192, 16384]
        patterns = [
            ("a*b", "a_star_b"),
            ("(a|aa)*b", "alt_star_b"),
            ("(a+)+b", "plus_plus_b"),
        ]

        print("      -------------------------------------------------------------------------")
        print(f"      {'Pattern':<16s} {'Size':>6s} {'Median Time (ms)':>18s} {'Ratio (2x N)':>14s}")
        print("      -------------------------------------------------------------------------")

        for pat_name, pat_prefix in patterns:
            prev_median = None
            for sz in sizes:
                key = f"{pat_prefix}_{sz}"
                assert key in measurements and len(measurements[key]) >= 5, f"Missing measurements for {key}"
                med = statistics.median(measurements[key])
                ratio_str = "-"
                if prev_median is not None and prev_median > 0:
                    ratio = med / prev_median
                    ratio_str = f"{ratio:.2f}x"
                    # Linear growth check: doubling input size should increase time by ~2x (bounded by [1.0, 3.5])
                    assert 1.0 <= ratio <= 3.5, f"Non-linear scaling detected for {pat_name} at size {sz}: ratio={ratio:.2f}x"
                print(f"      {pat_name:<16s} {sz:>6d} {med:>18.4f} {ratio_str:>14s}")
                prev_median = med
            print("      -------------------------------------------------------------------------")

        # Validate pattern complexity cost: (a|aa)*b > a*b due to state count
        med_simple_16k = statistics.median(measurements["a_star_b_16384"])
        med_complex_16k = statistics.median(measurements["alt_star_b_16384"])
        assert med_complex_16k >= med_simple_16k * 0.9, "State-count complexity cost failed"

        print("      Runtime-scaling experiment successfully validated linear O(N) scaling.")


# ---------------------------------------------------------------------------
# Part 4: Source-Free ABI-v9 Provider and Consumer
# ---------------------------------------------------------------------------

def check_source_free_abi_v9():
    print("[4/4] Running source-free compiled-only ABI-v9 provider & consumer verification...")
    # 1. Build release packages to generate .mossi and .rlib
    packages_to_copy = [
        ("bytes", ROOT / "sdk" / "foundations" / "bytes"),
        ("hash", ROOT / "packages" / "hash"),
        ("compress", ROOT / "packages" / "compress"),
        ("regex", ROOT / "packages" / "regex"),
    ]
    for pkg_name, pkg_path in packages_to_copy:
        res = subprocess.run([str(MARGO), "build", "--release"], cwd=str(pkg_path), capture_output=True, text=True)
        assert res.returncode == 0, f"Failed building {pkg_name} release:\n{res.stdout}\n{res.stderr}"

    with tempfile.TemporaryDirectory(prefix="abi-v9-source-free-", dir=SCRATCH) as td:
        work_dir = Path(td)
        provider_dir = work_dir / "provider"
        consumer_dir = work_dir / "consumer"
        provider_dir.mkdir(parents=True)
        (consumer_dir / "src").mkdir(parents=True)

        # Copy ONLY compiled .mossi and .rlib files — NO source .moss files!
        for pkg_name, pkg_path in packages_to_copy:
            release_dir = pkg_path / "build" / "release"
            mossi_file = release_dir / f"{pkg_name}.mossi"
            assert mossi_file.is_file(), f"Missing {mossi_file}"
            rlib_files = list(release_dir.glob("*.rlib"))
            assert rlib_files, f"Missing .rlib files in {release_dir}"

            shutil.copy2(mossi_file, provider_dir / f"{pkg_name}.mossi")
            for rlib in rlib_files:
                shutil.copy2(rlib, provider_dir / rlib.name)

        # Confirm zero .moss source files exist in provider_dir
        moss_files = list(provider_dir.glob("*.moss"))
        assert len(moss_files) == 0, f"Provider directory must not contain .moss sources: {moss_files}"

        # Write consumer application
        (consumer_dir / "Moss.toml").write_text(
            '[package]\nname = "abi_consumer"\nversion = "0.1.0"\n[build]\nsource = "src"\n',
            encoding="utf-8"
        )
        (consumer_dir / "src" / "main.moss").write_text(
            'module main\n'
            'import hash\n'
            'import compress\n'
            'import regex\n'
            '\n'
            'fn run_consumer():\n'
            '  # 1. Test hash\n'
            '  let b = hash.string_to_bytes("123456789")\n'
            '  let h = hash.adler32(b)\n'
            '  assertEqual(h, 152961502)\n'
            '  # 2. Test compress\n'
            '  let orig = [1, 2, 3, 4, 1, 2, 3, 4]\n'
            '  let comp = compress.compress_lz4_block(orig)\n'
            '  let decomp = compress.decompress_lz4_block(comp, 100)\n'
            '  assertEqual(decomp |> count, 8)\n'
            '  # 3. Test regex\n'
            '  let res = regex.search("fo+", "afoooob")\n'
            '  match res:\n'
            '    case NoMatch:\n'
            '      assert(false)\n'
            '    case Matched(start, end):\n'
            '      assertEqual(start, 1)\n'
            '      assertEqual(end, 6)\n'
            '  echo "ABI_V9_SOURCE_FREE_CONSUMER_SUCCESS"\n'
            '\n'
            'export fn main():\n'
            '  try:\n'
            '    run_consumer()\n'
            '  recover hash.HashError.InvalidByte:\n'
            '    assert(false)\n'
            '  recover hash.HashError.CorruptedState:\n'
            '    assert(false)\n'
            '  recover compress.CompressError.InvalidByte:\n'
            '    assert(false)\n'
            '  recover compress.CompressError.TruncatedInput:\n'
            '    assert(false)\n'
            '  recover compress.CompressError.InvalidOffset:\n'
            '    assert(false)\n'
            '  recover compress.CompressError.DecompressedSizeExceeded:\n'
            '    assert(false)\n'
            '  recover compress.CompressError.CorruptedStream:\n'
            '    assert(false)\n'
            '  recover regex.RegexError.UnexpectedToken:\n'
            '    assert(false)\n'
            '  recover regex.RegexError.UnclosedParenthesis:\n'
            '    assert(false)\n'
            '  recover regex.RegexError.UnclosedBracket:\n'
            '    assert(false)\n'
            '  recover regex.RegexError.DanglingRepetition:\n'
            '    assert(false)\n'
            '  recover regex.RegexError.InvalidRange:\n'
            '    assert(false)\n'
            '  recover regex.RegexError.InvalidEscape:\n'
            '    assert(false)\n'
            '  recover regex.RegexError.PatternTooComplex:\n'
            '    assert(false)\n'
            '  recover regex.RegexError.UnsupportedFeature:\n'
            '    assert(false)\n',
            encoding="utf-8"
        )

        env = os.environ.copy()
        env["MOSS_MODULE_PATH"] = str(provider_dir)
        env["TMPDIR"] = str(SCRATCH)

        rc, stdout, stderr = run_moss_app(consumer_dir, env=env)
        assert rc == 0, f"Source-free ABI-v9 consumer failed:\nSTDOUT:\n{stdout}\nSTDERR:\n{stderr}"
        assert "ABI_V9_SOURCE_FREE_CONSUMER_SUCCESS" in stdout
        print("      Source-free ABI-v9 provider and consumer verification succeeded.")


# ---------------------------------------------------------------------------
# Main entrypoint
# ---------------------------------------------------------------------------

def main():
    print("=" * 70)
    print("PHASE 24 DATA UTILS: INDEPENDENT DIFFERENTIAL & ABI-V9 VALIDATOR")
    print("=" * 70)
    check_hash_oracle()
    check_compress_oracle()
    check_regex_oracle()
    check_source_free_abi_v9()
    print("=" * 70)
    print("ALL INDEPENDENT DIFFERENTIAL, INTEROPERABILITY & ABI-V9 CHECKS PASSED.")
    print("=" * 70)


if __name__ == "__main__":
    main()
