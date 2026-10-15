"""Phase 25 G4 benchmark: Unicode JSON cursor traversal and String metadata.

Measures `while i < s.length(): c = s.char_at(i)` over runtime-built Unicode
JSON text in native release builds, for the plain representation (MossStr =
String) and the staged indexed-v1 record (MossStr = MossString).

Method (docs/phase25/TEAM_S_UNICODE_STRING_BENCHMARK.md):
  * release flags identical to `margo --release`: --edition=2021 -O -C debuginfo=1;
  * per size, a build-only baseline binary and a build+traverse binary from
    the same source; traversal time = median(traverse) - median(build);
  * the document is assembled at run time by append in a loop and the scan
    result is printed, so neither step can be folded away;
  * 5 runs per binary, median reported, plus the doubling ratio per 10x size;
  * plain runs are skipped once a size exceeds the time budget (quadratic).

Usage: python3 tests/benchmarks/phase25_unicode_traversal.py [moss] [--json out.json]
"""

import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
args = [a for a in sys.argv[1:] if not a.startswith("--")]
MOSS = Path(args[0] if args else ROOT / "moss").resolve()
JSON_OUT = sys.argv[sys.argv.index("--json") + 1] if "--json" in sys.argv else None
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)
RUNS = 5
# Each fragment is 25 code points / 40 UTF-8 bytes of mixed ASCII, Latin, CJK, emoji.
FRAGMENT = '{"名前":"値😀","é":[1,2],"k":"ü"},'
FRAGMENT_CODEPOINTS = len(FRAGMENT)
SIZES = [1_000, 10_000, 100_000, 1_000_000]
PLAIN_BUDGET_SECONDS = 60.0

SOURCE = """
fn build(blocks: Int) -> String:
  var doc = ""
  var k = 0
  while k < blocks:
    doc.append("{\\"名前\\":\\"値😀\\",\\"é\\":[1,2],\\"k\\":\\"ü\\"},")
    k = k + 1
  return doc

fn scan(doc: String) -> Int:
  var structural = 0
  var i = 0
  while i < doc.length():
    c = doc.char_at(i)
    if c == '{' or c == '}' or c == '"' or c == ',' or c == ':':
      structural = structural + 1
    i = i + 1
  return structural

fn main():
  let doc = build(BLOCKS)
  echo doc.length()
  SCAN
"""


def env_for(mode):
    env = {k: v for k, v in os.environ.items() if k != "MOSS_STAGED_STRING_REPR"}
    if mode == "indexed-v1":
        env["MOSS_STAGED_STRING_REPR"] = "indexed-v1"
    return env


def compile_binary(td, mode, blocks, traverse):
    tag = f"{mode}-{blocks}-{'scan' if traverse else 'build'}"
    source = Path(td) / f"{tag}.moss"
    source.write_text(SOURCE.replace("BLOCKS", str(blocks))
                      .replace("SCAN", "echo scan(doc)" if traverse else "echo 0"))
    rust = Path(td) / f"{tag}.rs"
    binary = Path(td) / tag
    env = env_for(mode)
    gen = subprocess.run([str(MOSS), str(source), "-O", "-o", str(rust)], env=env,
                         capture_output=True, text=True)
    assert gen.returncode == 0, gen.stderr
    build = subprocess.run(["rustc", "--edition=2021", "-O", "-C", "debuginfo=1",
                            "--crate-name", "bench", str(rust), "-o", str(binary)],
                           env=env, capture_output=True, text=True)
    assert build.returncode == 0, build.stderr[-3000:]
    return binary


def timed(binary, timeout):
    samples, output = [], None
    for _ in range(RUNS):
        start = time.perf_counter()
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=timeout)
        samples.append(time.perf_counter() - start)
        assert result.returncode == 0, result.stderr
        assert output in (None, result.stdout)
        output = result.stdout
    return statistics.median(samples), output


METADATA_MAIN = r"""
fn main() {
    let cases: [(&str, &str); 4] = [("ascii", "abcdefghijklmnopqrstuvwxy,"),
        ("json-mixed", "{\"名前\":\"値😀\",\"é\":[1,2],\"k\":\"ü\"},"),
        ("cjk", "名前値漢字"), ("emoji", "😀🙂🌍")];
    println!("struct_bytes {} {}", std::mem::size_of::<String>(), std::mem::size_of::<MossString>());
    for (name, unit) in cases.iter() {
        for &codepoints in [1_000usize, 1_000_000].iter() {
            let unit_cp = unit.chars().count();
            let mut s = MossString::new();
            while s.codepoint_len() + unit_cp <= codepoints { s.push_str(unit); }
            println!("{} {} {} {}", name, s.codepoint_len(), s.len(), s.metadata_bytes());
        }
    }
}
"""


def metadata_costs(td):
    header = (ROOT / "src" / "char_string_ordering_runtime.hpp").read_text()
    ordering, _plain, indexed = re.findall(r'R"RUST\((.*?)\)RUST"', header, re.S)
    path = Path(td) / "metadata.rs"
    path.write_text("#![allow(dead_code)]\n" + ordering + indexed + METADATA_MAIN)
    binary = Path(td) / "metadata"
    build = subprocess.run(["rustc", "--edition=2021", "-O", str(path), "-o", str(binary)],
                           capture_output=True, text=True)
    assert build.returncode == 0, build.stderr[-3000:]
    lines = subprocess.run([str(binary)], capture_output=True, text=True).stdout.split("\n")
    _, string_bytes, moss_bytes = lines[0].split()
    rows = []
    for line in lines[1:]:
        if line.strip():
            name, cps, payload, meta = line.split()
            rows.append({"text": name, "codepoints": int(cps), "utf8_bytes": int(payload),
                         "metadata_bytes": int(meta),
                         "metadata_percent_of_payload": round(100.0 * int(meta) / int(payload), 3)})
    return {"rust_struct_bytes": {"String": int(string_bytes), "MossString": int(moss_bytes)},
            "cases": rows}


def main():
    rustc = subprocess.run(["rustc", "--version"], capture_output=True, text=True).stdout.strip()
    sha = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    report = {"compiler_sha": sha, "rustc": rustc, "runs_per_binary": RUNS,
              "fragment": FRAGMENT, "fragment_codepoints": FRAGMENT_CODEPOINTS, "results": []}
    with tempfile.TemporaryDirectory(prefix="p25-bench-", dir=SCRATCH) as td:
        report["metadata"] = metadata_costs(td)
        for mode in ("plain", "indexed-v1"):
            skip = False
            for size in SIZES:
                blocks = size // FRAGMENT_CODEPOINTS
                row = {"mode": mode, "codepoints": blocks * FRAGMENT_CODEPOINTS}
                if skip:
                    row["skipped"] = f"previous size exceeded {PLAIN_BUDGET_SECONDS}s budget"
                    report["results"].append(row)
                    print(json.dumps(row))
                    continue
                build_bin = compile_binary(td, mode, blocks, False)
                scan_bin = compile_binary(td, mode, blocks, True)
                build_s, _ = timed(build_bin, 600)
                scan_s, out = timed(scan_bin, 3600)
                row.update({"build_median_s": round(build_s, 6),
                            "build_and_scan_median_s": round(scan_s, 6),
                            "scan_s": round(max(scan_s - build_s, 0.0), 6),
                            "output": out.split()})
                report["results"].append(row)
                print(json.dumps(row))
                if mode == "plain" and scan_s > PLAIN_BUDGET_SECONDS / 10:
                    skip = True
    for mode in ("plain", "indexed-v1"):
        rows = [r for r in report["results"] if r["mode"] == mode and "scan_s" in r]
        for small, large in zip(rows, rows[1:]):
            if small["scan_s"] > 0:
                large["ratio_vs_previous_size"] = round(large["scan_s"] / small["scan_s"], 2)
    outputs = {}
    for r in report["results"]:
        if "output" in r:
            outputs.setdefault(r["codepoints"], set()).add(tuple(r["output"]))
    assert all(len(v) == 1 for v in outputs.values()), f"modes disagree: {outputs}"
    print(json.dumps(report["metadata"], indent=1))
    if JSON_OUT:
        Path(JSON_OUT).write_text(json.dumps(report, indent=1) + "\n")


if __name__ == "__main__":
    main()
