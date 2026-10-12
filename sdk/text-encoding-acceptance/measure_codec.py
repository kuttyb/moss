#!/usr/bin/env python3
"""Measure release UTF-8 encode/decode with amortized process startup.

Each timed process performs 5,000 conversions of a 256-scalar emoji workload.
Five process warmups precede 31 samples. Reported nanoseconds are raw elapsed
process time divided by conversions; no startup subtraction or parity claim is
made. The Rust reference uses std strict UTF-8 operations on the same workload.
"""

import json
import os
from pathlib import Path
import shutil
import subprocess
import time

PROJECT = Path(__file__).resolve().parent
ROOT = PROJECT.parents[1]
MARGO = ROOT / "margo"
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
    subprocess.run([str(MARGO), "run", "--release"], cwd=folder, env=ENV,
                   check=True, stdout=subprocess.DEVNULL)
    return folder / "build" / "release" / name


def measure(command: list[str], expected: str) -> dict[str, int]:
    for _ in range(5):
        completed = subprocess.run(command, text=True, capture_output=True, check=True)
        assert completed.stdout == expected
    samples = []
    for _ in range(31):
        start = time.perf_counter_ns()
        completed = subprocess.run(command, text=True, capture_output=True, check=True)
        elapsed = time.perf_counter_ns() - start
        assert completed.stdout == expected
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
      total = total + (encoded |> count)
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
      total = total + (decoded |> count)
      run = run + 1
    echo total
  recover error:
    echo -1
'''
encode = moss_project("moss_utf8_encode_bench", encode_source)
decode = moss_project("moss_utf8_decode_bench", decode_source)

rust_source = WORK / "rust_codec_bench.rs"
rust_binary = WORK / "rust_codec_bench"
rust_source.write_text(f'''use std::hint::black_box;

fn main() {{
    let mode = std::env::args().nth(1).unwrap();
    let repeats = {REPETITIONS};
    if mode == "encode" {{
        let scalars = vec![0x1F642_u32; 256];
        let mut total = 0_usize;
        for _ in 0..repeats {{
            let mut encoded = Vec::<u8>::new();
            for &scalar in black_box(&scalars) {{
                let mut buffer = [0_u8; 4];
                let text = char::from_u32(scalar).unwrap().encode_utf8(&mut buffer);
                encoded.extend_from_slice(text.as_bytes());
            }}
            total += black_box(encoded.len());
        }}
        println!("{{total}}");
    }} else {{
        let values = "🙂".repeat(256).into_bytes();
        let mut total = 0_usize;
        for _ in 0..repeats {{
            total += black_box(std::str::from_utf8(black_box(&values)).unwrap().chars().count());
        }}
        println!("{{total}}");
    }}
}}
''', encoding="utf-8")
subprocess.run(["rustc", "--edition=2021", "-D", "warnings", "-O",
                "-C", "debuginfo=1", str(rust_source), "-o", str(rust_binary)],
               check=True)

results = {
    "methodology": {"warmups": 5, "samples": 31, "conversions_per_process": REPETITIONS,
                    "startup_subtracted": False, "scalar_count": 256},
    "moss_encode": measure([str(encode)], f"{REPETITIONS * 1024}\n"),
    "moss_decode": measure([str(decode)], f"{REPETITIONS * 256}\n"),
    "rust_std_encode": measure([str(rust_binary), "encode"], f"{REPETITIONS * 1024}\n"),
    "rust_std_decode": measure([str(rust_binary), "decode"], f"{REPETITIONS * 256}\n"),
}
print(json.dumps(results, sort_keys=True))
