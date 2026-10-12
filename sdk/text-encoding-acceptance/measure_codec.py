#!/usr/bin/env python3
"""Measure release UTF-8 operations against idiomatic and i64 Rust.

Each process performs 5,000 operations on 256 emoji scalars (1,024 bytes).
Five warmups precede 31 samples. Raw process time is divided by operations;
startup is not subtracted. Content assertions prevent workload drift.
"""

import json
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / "tmp" / "phase24-codec-bench"
REPETITIONS = 5_000
ENV = os.environ.copy()
ENV.update(MOSS=str(ROOT / "moss"), TMPDIR=str(ROOT / "tmp"),
           MARGO_HOME=str(ROOT / "tmp" / "margo-codec-bench"))


def moss_project(name: str, body: str) -> Path:
    folder = WORK / name
    (folder / "src").mkdir(parents=True)
    (folder / "Moss.toml").write_text(
        f'[package]\nname = "{name}"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n\n[dependencies]\n'
        f'"moss.encoding" = {{ path = "{ROOT / "sdk" / "encoding"}" }}\n',
        encoding="utf-8")
    (folder / "src" / "main.moss").write_text(body, encoding="utf-8")
    subprocess.run([str(ROOT / "margo"), "run", "--release"], cwd=folder,
                   env=ENV, check=True, stdout=subprocess.DEVNULL)
    return folder / "build" / "release" / name


def measure(command: list[str], expected: str) -> dict[str, int]:
    for _ in range(5):
        result = subprocess.run(command, text=True, capture_output=True, check=True)
        assert result.stdout == expected
    samples = []
    for _ in range(31):
        start = time.perf_counter_ns()
        result = subprocess.run(command, text=True, capture_output=True, check=True)
        elapsed = time.perf_counter_ns() - start
        assert result.stdout == expected
        samples.append((elapsed + REPETITIONS - 1) // REPETITIONS)
    samples.sort()
    return {"median_ns": samples[15], "p25_ns": samples[7], "p75_ns": samples[23]}


if WORK.exists():
    shutil.rmtree(WORK)
WORK.mkdir(parents=True)

encode_source = f'''module app
import encoding

fn main():
  try:
    var scalars = Vector[Int]()
    var i = 0
    while i < 256:
      scalars.push(128578)
      i = i + 1
    var total = 0
    var run = 0
    while run < {REPETITIONS}:
      encoded = encoding.encode_utf8(scalars)
      assertEqual(encoded |> count, 1024)
      assertEqual(encoded[0], 240)
      assertEqual(encoded[1023], 130)
      total = total + (encoded |> count) + encoded[0] + encoded[1023]
      run = run + 1
    echo total
  recover error:
    echo -1
'''
decode_source = f'''module app
import encoding

fn main():
  try:
    var values = Vector[Int]()
    var i = 0
    while i < 256:
      values.push(240)
      values.push(159)
      values.push(153)
      values.push(130)
      i = i + 1
    var total = 0
    var run = 0
    while run < {REPETITIONS}:
      decoded = encoding.decode_utf8(values)
      assertEqual(decoded |> count, 256)
      assertEqual(decoded[0], 128578)
      assertEqual(decoded[255], 128578)
      total = total + (decoded |> count) + decoded[0] + decoded[255]
      run = run + 1
    echo total
  recover error:
    echo -1
'''
validate_source = f'''module app
import encoding

fn main():
  var values = Vector[Int]()
  var i = 0
  while i < 256:
    values.push(240)
    values.push(159)
    values.push(153)
    values.push(130)
    i = i + 1
  assertEqual(values |> count, 1024)
  assertEqual(values[0], 240)
  assertEqual(values[1023], 130)
  var total = 0
  var run = 0
  while run < {REPETITIONS}:
    if encoding.validate_utf8(values):
      total = total + 1
    run = run + 1
  echo total
'''
encode = moss_project("moss_utf8_encode_bench", encode_source)
decode = moss_project("moss_utf8_decode_bench", decode_source)
validate = moss_project("moss_utf8_validate_bench", validate_source)

rust_source = WORK / "rust_codec_bench.rs"
rust_binary = WORK / "rust_codec_bench"
rust_source.write_text(f'''use std::hint::black_box;

fn validate_i64(values: &[i64]) -> bool {{
    let mut i = 0_usize;
    while i < values.len() {{
        let first = values[i];
        if !(0..=255).contains(&first) {{ return false; }}
        if first <= 127 {{ i += 1; continue; }}
        let (width, minimum) = if (192..=223).contains(&first) {{ (2, 128) }}
            else if (224..=239).contains(&first) {{ (3, 2048) }}
            else if (240..=247).contains(&first) {{ (4, 65536) }}
            else {{ return false; }};
        if values.len() - i < width {{ return false; }}
        let second = values[i + 1];
        if !(128..=191).contains(&second) {{ return false; }}
        let scalar = if width == 2 {{
            (first - 192) * 64 + second - 128
        }} else {{
            let third = values[i + 2];
            if !(128..=191).contains(&third) {{ return false; }}
            if width == 3 {{
                (first - 224) * 4096 + (second - 128) * 64 + third - 128
            }} else {{
                let fourth = values[i + 3];
                if !(128..=191).contains(&fourth) {{ return false; }}
                (first - 240) * 262144 + (second - 128) * 4096
                    + (third - 128) * 64 + fourth - 128
            }}
        }};
        if scalar < minimum || (55296..=57343).contains(&scalar) || scalar > 1114111 {{
            return false;
        }}
        i += width;
    }}
    true
}}

fn encode_i64(scalars: &[i64]) -> Vec<i64> {{
    let mut result = Vec::new();
    for &scalar in scalars {{
        assert!((0..=1114111).contains(&scalar) && !(55296..=57343).contains(&scalar));
        if scalar <= 127 {{ result.push(scalar); }}
        else if scalar <= 2047 {{
            result.push(192 + scalar / 64); result.push(128 + scalar % 64);
        }} else if scalar <= 65535 {{
            result.push(224 + scalar / 4096); result.push(128 + (scalar / 64) % 64);
            result.push(128 + scalar % 64);
        }} else {{
            result.push(240 + scalar / 262144); result.push(128 + (scalar / 4096) % 64);
            result.push(128 + (scalar / 64) % 64); result.push(128 + scalar % 64);
        }}
    }}
    result
}}

fn decode_i64(values: &[i64]) -> Vec<i64> {{
    let mut result = Vec::new();
    let mut i = 0;
    while i < values.len() {{
        let first = values[i];
        assert!((0..=255).contains(&first));
        if first <= 127 {{ result.push(first); i += 1; }}
        else if first <= 223 {{
            assert!(first >= 192 && values.len() - i >= 2);
            let second = values[i + 1];
            assert!((128..=191).contains(&second));
            let scalar = (first - 192) * 64 + second - 128;
            assert!(scalar >= 128);
            result.push(scalar); i += 2;
        }} else if first <= 239 {{
            assert!(values.len() - i >= 3);
            let second = values[i + 1]; let third = values[i + 2];
            assert!((128..=191).contains(&second) && (128..=191).contains(&third));
            let scalar = (first - 224) * 4096 + (second - 128) * 64 + third - 128;
            assert!(scalar >= 2048 && !(55296..=57343).contains(&scalar));
            result.push(scalar); i += 3;
        }} else {{
            assert!(first <= 247 && values.len() - i >= 4);
            let second = values[i + 1]; let third = values[i + 2]; let fourth = values[i + 3];
            assert!((128..=191).contains(&second) && (128..=191).contains(&third)
                && (128..=191).contains(&fourth));
            let scalar = (first - 240) * 262144 + (second - 128) * 4096
                + (third - 128) * 64 + fourth - 128;
            assert!((65536..=1114111).contains(&scalar));
            result.push(scalar); i += 4;
        }}
    }}
    result
}}

fn checksum_i64(values: &[i64]) -> i64 {{
    values.len() as i64 + values[0] + values[values.len() - 1]
}}

fn main() {{
    let mode = std::env::args().nth(1).unwrap();
    let scalars = vec![0x1F642_i64; 256];
    let bytes = "🙂".repeat(256).into_bytes();
    let bytes_i64: Vec<i64> = bytes.iter().map(|&value| value as i64).collect();
    let mut total = 0_i64;
    for _ in 0..{REPETITIONS} {{
        total += match mode.as_str() {{
            "std_encode" => {{
                let mut encoded = Vec::<u8>::new();
                for &scalar in black_box(&scalars) {{
                    let mut buffer = [0_u8; 4];
                    let text = char::from_u32(scalar as u32).unwrap().encode_utf8(&mut buffer);
                    encoded.extend_from_slice(text.as_bytes());
                }}
                let encoded = black_box(encoded);
                encoded.len() as i64 + encoded[0] as i64 + encoded[1023] as i64
            }},
            "std_decode" => {{
                let decoded: Vec<u32> = std::str::from_utf8(black_box(&bytes)).unwrap()
                    .chars().map(|value| value as u32).collect();
                let decoded = black_box(decoded);
                decoded.len() as i64 + decoded[0] as i64 + decoded[255] as i64
            }},
            "std_validate" => black_box(std::str::from_utf8(black_box(&bytes)).is_ok()) as i64,
            "i64_encode" => checksum_i64(black_box(&encode_i64(black_box(&scalars)))),
            "i64_decode" => checksum_i64(black_box(&decode_i64(black_box(&bytes_i64)))),
            "i64_validate" => black_box(validate_i64(black_box(&bytes_i64))) as i64,
            _ => panic!("unknown mode"),
        }};
    }}
    println!("{{total}}");
}}
''', encoding="utf-8")
subprocess.run(["rustc", "--edition=2021", "-D", "warnings", "-O",
                "-C", "debuginfo=1", str(rust_source), "-o", str(rust_binary)],
               check=True)

encode_total = f"{REPETITIONS * 1394}\n"
decode_total = f"{REPETITIONS * 257412}\n"
valid_total = f"{REPETITIONS}\n"
results = {
    "methodology": {"warmups": 5, "samples": 31, "operations_per_process": REPETITIONS,
                    "startup_subtracted": False, "scalar_count": 256,
                    "encoded_byte_count": 1024, "content_assertions": True},
    "moss_encode": measure([str(encode)], encode_total),
    "moss_decode": measure([str(decode)], decode_total),
    "moss_validate": measure([str(validate)], valid_total),
    "rust_std_encode": measure([str(rust_binary), "std_encode"], encode_total),
    "rust_std_decode": measure([str(rust_binary), "std_decode"], decode_total),
    "rust_std_validate": measure([str(rust_binary), "std_validate"], valid_total),
    "rust_i64_encode": measure([str(rust_binary), "i64_encode"], encode_total),
    "rust_i64_decode": measure([str(rust_binary), "i64_decode"], decode_total),
    "rust_i64_validate": measure([str(rust_binary), "i64_validate"], valid_total),
}
print(json.dumps(results, sort_keys=True))
