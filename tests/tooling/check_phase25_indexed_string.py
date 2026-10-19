"""Phase 25 G4 indexed String record (staged for the joint ABI-v10 cut).

1. Differential property test: the indexed-v1 runtime agrees with the plain
   runtime on length/char_at/slice/find after random Unicode appends, and its
   offset metadata satisfies the documented invariants.
2. Staged builds (MOSS_STAGED_STRING_REPR=indexed-v1) of a String-heavy
   program agree with plain native builds and Fast Debug.
3. The two representations never mix: a staged consumer rejects a plain
   provider interface, and staged multi-crate builds fail closed.
"""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else "./moss").resolve()
MARGO = ROOT / "margo"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)
STAGED = {**os.environ, "MOSS_STAGED_STRING_REPR": "indexed-v1"}
PLAIN = {k: v for k, v in os.environ.items() if k != "MOSS_STAGED_STRING_REPR"}


def run(cmd, cwd=ROOT, env=None, timeout=900):
    return subprocess.run(cmd, cwd=cwd, env=env or PLAIN, capture_output=True,
                          text=True, timeout=timeout)


def runtime_sections():
    header = (ROOT / "src" / "char_string_ordering_runtime.hpp").read_text()
    sections = re.findall(r'R"RUST\((.*?)\)RUST"', header, re.S)
    assert len(sections) == 3, "expected ordering, plain, and indexed runtime sections"
    return sections


PROPERTY_MAIN = r"""
struct Rng(u64);
impl Rng {
    fn next(&mut self) -> u64 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        self.0
    }
    fn below(&mut self, n: u64) -> u64 { self.next() % n }
}

const ALPHABET: [char; 10] = ['a', 'Z', '0', ' ', '"', 'é', 'ß', '世', '界', '😀'];

fn random_part(rng: &mut Rng) -> String {
    let len = rng.below(150) as usize;
    let ascii_only = rng.below(3) == 0;
    (0..len).map(|_| {
        let limit = if ascii_only { 5 } else { ALPHABET.len() as u64 };
        ALPHABET[rng.below(limit) as usize]
    }).collect()
}

fn check_invariants(s: &indexed::MossString) {
    let text: &str = s;
    let count = text.chars().count();
    assert_eq!(indexed::__moss_string_length(s) as usize, count);
    let mut expected = Vec::new();
    if !text.is_ascii() {
        for (cp, (byte, _)) in text.char_indices().enumerate() {
            if cp > 0 && cp % 64 == 0 { expected.push(byte as u32); }
        }
    }
    assert_eq!(s.offsets_for_test(), &expected[..]);
}

fn main() {
    let mut rng = Rng(0x9e3779b97f4a7c15);
    let mut cases = 0usize;
    for _round in 0..200 {
        let mut plain_text = String::new();
        let mut staged = if rng.below(2) == 0 {
            indexed::MossString::new()
        } else {
            let seed = random_part(&mut rng);
            plain_text.push_str(&seed);
            indexed::MossString::from(seed)
        };
        for _ in 0..rng.below(12) {
            let part = random_part(&mut rng);
            plain_text.push_str(&part);
            staged.push_str(&part);
        }
        check_invariants(&staged);
        assert_eq!(&*staged, plain_text.as_str());
        let n = plain::__moss_string_length(&plain_text);
        assert_eq!(indexed::__moss_string_length(&staged), n);
        for _ in 0..64 {
            if n > 0 {
                let i = rng.below(n as u64) as i64;
                assert_eq!(indexed::__moss_string_char_at(&staged, i),
                           plain::__moss_string_char_at(&plain_text, i));
            }
            let start = rng.below(n as u64 + 3) as i64 - 1;
            let length = rng.below(n as u64 + 3) as i64 - 1;
            let a = indexed::__moss_string_slice(&staged, start, length);
            let b = plain::__moss_string_slice(&plain_text, start, length);
            assert_eq!(&*a, b.as_str(), "slice({}, {}) of {:?}", start, length, plain_text);
            check_invariants(&a);
            let pattern: String = if n > 0 && rng.below(2) == 0 {
                let from = rng.below(n as u64) as i64;
                plain::__moss_string_slice(&plain_text, from, rng.below(4) as i64)
            } else {
                random_part(&mut rng).chars().take(2).collect()
            };
            assert_eq!(indexed::__moss_string_find(&staged, &pattern),
                       plain::__moss_string_find(&plain_text, &pattern));
            cases += 1;
        }
    }
    let empty = indexed::MossString::new();
    assert_eq!(indexed::__moss_string_length(&empty), 0);
    assert_eq!(indexed::__moss_string_find(&empty, ""), 0);
    println!("{}", cases);
}
"""


def test_runtime_property():
    print("indexed-v1 runtime vs plain runtime (random Unicode appends)")
    ordering, plain, indexed = runtime_sections()
    indexed = indexed.replace(
        "    // Bytes of index metadata",
        "    pub fn offsets_for_test(&self) -> &[u32] { &self.offsets }\n\n    // Bytes of index metadata")
    source = ("#![allow(dead_code)]\n" + ordering +
              "mod plain {\n#![allow(dead_code)]\nuse super::*;\n" + plain + "}\n" +
              "mod indexed {\n#![allow(dead_code)]\nuse super::*;\n" + indexed + "}\n" +
              PROPERTY_MAIN)
    with tempfile.TemporaryDirectory(prefix="p25-indexed-", dir=SCRATCH) as td:
        path = Path(td) / "property.rs"
        path.write_text(source)
        binary = Path(td) / "property"
        build = run(["rustc", "--edition=2021", "-O", str(path), "-o", str(binary)])
        assert build.returncode == 0, build.stderr[-4000:]
        result = run([str(binary)])
        assert result.returncode == 0, result.stderr
        print(f"  PASS: {result.stdout.strip()} randomized char_at/slice/find comparisons")


PROGRAM = """
fn count_structure(text: String) -> Int:
  var n = 0
  var i = 0
  while i < text.length():
    c = text.char_at(i)
    if c == '"' or c == '{' or c == '}' or c == ',':
      n = n + 1
    i = i + 1
  return n

fn main():
  var doc = ""
  var k = 0
  while k < 300:
    doc.append("{\\"名前\\":\\"値😀\\",\\"é\\":[")
    doc.append(k.to_string())
    doc.append("]},")
    k = k + 1
  echo doc.length()
  echo count_structure(doc)
  echo doc.char_at(5000)
  echo doc.char_at(doc.length() - 2)
  echo doc.slice(4990, 20)
  echo doc.find("😀\\",\\"é\\":[299")
  let tail = doc.slice(doc.length() - 9, 9)
  echo tail
  echo tail.length()
  var parts = "a,é,😀".split(",")
  echo parts[2].length()
  echo "-".join(parts)
  echo doc.slice(64, 3) == "値😀\\""
"""


def native(source, td, env, tag, release):
    rust = Path(td) / f"{tag}.rs"
    binary = Path(td) / tag
    gen = run([str(MOSS), str(source), "-O" if release else "--debug", "-o", str(rust)], env=env)
    assert gen.returncode == 0, gen.stderr
    flags = ["-O"] if release else []
    runtime_flags = []
    if "moss_runtime::" in rust.read_text():
        runtime = Path(td) / "libmoss_runtime.rlib"
        if not runtime.is_file():
            runtime_build = run([
                "rustc", "--edition=2021", "--crate-name", "moss_runtime",
                "--crate-type", "rlib", str(ROOT / "runtime/moss_runtime/src/lib.rs"),
                "-o", str(runtime),
            ], env=env)
            assert runtime_build.returncode == 0, runtime_build.stderr[-4000:]
        runtime_flags = ["--extern", f"moss_runtime={runtime}",
                         "-L", f"dependency={td}"]
    build = run(["rustc", "--edition=2021", *flags, "--crate-name", "case",
                 str(rust), *runtime_flags, "-o", str(binary)], env=env)
    assert build.returncode == 0, build.stderr[-4000:]
    result = run([str(binary)], env=env)
    assert result.returncode == 0, result.stderr
    return result.stdout, rust.read_text()


def test_staged_program():
    print("staged indexed-v1 native builds vs plain native and Fast Debug")
    with tempfile.TemporaryDirectory(prefix="p25-indexed-", dir=SCRATCH) as td:
        source = Path(td) / "doc.moss"
        source.write_text(PROGRAM)
        interp = run([str(MOSS), "run", "--interp", str(source)])
        assert interp.returncode == 0, interp.stderr
        outputs = {"fast-debug": interp.stdout}
        for release in (False, True):
            plain_out, plain_rust = native(source, td, PLAIN, f"plain{int(release)}", release)
            staged_out, staged_rust = native(source, td, STAGED, f"staged{int(release)}", release)
            assert "pub type MossStr = String;" in plain_rust
            if os.environ.get("MOSS_STAGED_ABI") == "v10":
                assert "pub use moss_runtime::string::{MossStr, MossString," in staged_rust
                assert "pub type MossStr = MossString;" in (
                    ROOT / "runtime/moss_runtime/src/string.rs").read_text()
            else:
                assert "pub type MossStr = MossString;" in staged_rust
            outputs[f"plain-{release}"] = plain_out
            outputs[f"staged-{release}"] = staged_out
        assert len(set(outputs.values())) == 1, outputs
    print("  PASS: identical output in Fast Debug and plain/staged debug and release")


def test_representations_never_mix():
    print("plain and staged artifacts never link")
    with tempfile.TemporaryDirectory(prefix="p25-indexed-", dir=SCRATCH) as td:
        proj = Path(td)
        (proj / "Moss.toml").write_text('[package]\nname = "mix"\nversion = "0.1.0"\n')
        src = proj / "src"
        src.mkdir()
        (src / "provider.moss").write_text(
            "module provider\nexport fn shout(s: String) -> String:\n  return s + \"!\"\n")
        (src / "main.moss").write_text(
            "module app\nimport provider\nfn main():\n  echo provider.shout(\"é\")\n")
        plain = run([str(MOSS), "build", "--json"], cwd=proj)
        assert plain.returncode == 0, plain.stdout + plain.stderr
        interface = (proj / "build" / "debug" / "provider.mossi").read_text()
        assert "string_representation" not in interface
        if os.environ.get("MOSS_STAGED_ABI") == "v10":
            plain_artifacts = Path(td) / "plain_artifacts"
            plain_artifacts.mkdir()
            for name in ("provider.mossi", "libprovider.rlib"):
                shutil.copy2(proj / "build" / "debug" / name,
                             plain_artifacts / name)
            staged = run([str(MARGO), "run"], cwd=proj, env=STAGED)
            assert staged.returncode == 0 and staged.stdout == "é!\n", (
                staged.stdout + staged.stderr)
            consumer = Path(td) / "mixed_consumer"
            (consumer / "src").mkdir(parents=True)
            (consumer / "Moss.toml").write_text(
                '[package]\nname = "mixed_consumer"\nversion = "0.1.0"\n')
            shutil.copy2(src / "main.moss", consumer / "src" / "main.moss")
            mixed_env = {**STAGED, "MOSS_MODULE_PATH": str(plain_artifacts)}
            mixed = run([str(MOSS), "check", "src/main.moss", "--json"],
                        cwd=consumer, env=mixed_env)
            assert mixed.returncode != 0, mixed.stdout
            assert json.loads(mixed.stdout)["error"]["code"] == (
                "STRING_REPRESENTATION_MISMATCH"), mixed.stdout
            print("  PASS: staged multi-crate execution and plain/indexed mismatch rejection")
            return
        staged = run([str(MOSS), "build", "--json"], cwd=proj, env=STAGED)
        assert staged.returncode != 0
        assert "STRING_REPRESENTATION_STAGED_SINGLE_CRATE" in staged.stdout, staged.stdout
        (src / "provider.moss").unlink()
        mixed = run([str(MOSS), "check", str(src / "main.moss"), "--json"], cwd=proj, env=STAGED)
        assert mixed.returncode != 0
        assert "STRING_REPRESENTATION_MISMATCH" in mixed.stdout + mixed.stderr, mixed.stdout
    print("  PASS: staged multi-crate builds and staged-vs-plain interfaces fail closed")


def main():
    print("=== Phase 25 G4 indexed String record ===")
    test_runtime_property()
    test_staged_program()
    test_representations_never_mix()
    print("=== Phase 25 indexed String checks passed ===")


if __name__ == "__main__":
    main()
