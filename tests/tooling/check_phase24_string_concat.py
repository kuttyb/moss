#!/usr/bin/env python3
"""String concatenation/buffer lowering and Fast Debug regression checks."""
import pathlib
import subprocess
import tempfile
import json

ROOT = pathlib.Path(__file__).resolve().parents[2]
MOSS = ROOT / "moss"
SOURCE = ROOT / "tests" / "phase24_string_concat.moss"
BENCH_PROJECT = (ROOT / "tests" / "tooling" / "fixtures" /
                 "phase24_string_concat_bench")


def run(*args):
    return subprocess.run([str(MOSS), *map(str, args)], cwd=ROOT,
                          text=True, capture_output=True)


checked = run("check", SOURCE, "--json")
assert checked.returncode == 0, checked.stderr
effects = run("effects", "append_part", "--source", SOURCE, "--json")
assert effects.returncode == 0, effects.stderr
effect_payload = json.loads(effects.stdout)["result"]
parameters = effect_payload["ownership_facts"]["parameters"]
assert [(item["name"], item["effect"]) for item in parameters] == [
    ("target", "WRITE"), ("part", "READ")
], parameters
interpreted = run("run", "--interp", SOURCE)
assert interpreted.returncode == 0, interpreted.stderr
assert interpreted.stdout.strip() == "string concat ok", interpreted.stdout

with tempfile.TemporaryDirectory(dir=ROOT / "tmp") as directory:
    generated = pathlib.Path(directory) / "concat.rs"
    compiled = run(SOURCE, "-o", generated)
    assert compiled.returncode == 0, compiled.stderr
    rust = generated.read_text(encoding="utf-8")
    assert 'format!("{}{}"' not in rust
    assert "String::with_capacity" in rust
    assert "__moss_concat_result.push_str" in rust
    assert "__moss_append_1" in rust
    assert ").reserve(__moss_append_1.len().saturating_add(" in rust
    assert 'let __moss_part = "🙂"; __moss_string.push_str(__moss_part)' in rust
    assert '"-".to_string()' not in rust
    assert 'let __moss_append_1 = "-"' in rust
    assert 'let __moss_concat_0 = "["' in rust

    executable = pathlib.Path(directory) / "concat-bin"
    native = subprocess.run(["rustc", "--edition", "2021", "-O", generated,
                             "-o", executable], cwd=ROOT, text=True,
                            capture_output=True)
    assert native.returncode == 0, native.stderr
    executed = subprocess.run([executable], cwd=ROOT, text=True,
                              capture_output=True)
    assert executed.returncode == 0, executed.stderr
    assert executed.stdout.strip() == "string concat ok", executed.stdout

    benchmark = subprocess.run([str(MOSS), "bench", "--json"],
                               cwd=BENCH_PROJECT, text=True,
                               capture_output=True)
    assert benchmark.returncode == 0, benchmark.stderr
    benchmark_rust = (BENCH_PROJECT / "build" / "bench" /
                      "phase24_string_concat_bench_benches.rs")
    benchmark_text = benchmark_rust.read_text(encoding="utf-8")
    assert "finish(builder)" in benchmark_text
    assert "black_box(&builder)" not in benchmark_text

    for body, diagnostic in [
        ('fn main():\n  value = "x"\n  value.append(value)\n',
         "OWNERSHIP_CONFLICTING_ACCESS"),
        ('fn identity(value: String) -> String:\n  return value + ""\n\n'
         'fn main():\n  var value = "x"\n  value.append(identity(value))\n',
         "OWNERSHIP_CONFLICTING_ACCESS"),
        ('fn main():\n  let value = "x"\n  value.append("y")\n',
         "IMMUTABLE_LOCAL_MUTATION"),
        ('fn main():\n  var value = "x"\n  value.reserve("many")\n',
         "TYPE_MISMATCH"),
    ]:
        invalid = pathlib.Path(directory) / "invalid.moss"
        invalid.write_text(body, encoding="utf-8")
        result = run("check", invalid)
        assert result.returncode != 0, body
        assert diagnostic in result.stderr, result.stderr

    for body, wrapped in [
        ('fn identity(value: String) -> String:\n'
         '  return value + ""\n\n'
         'fn main():\n'
         '  var value = "x"\n'
         '  value.append(identity(value))\n', 'identity(value)'),
        ('fn string_length(value: String) -> Int:\n'
         '  return value.length()\n\n'
         'fn main():\n'
         '  var value = "x"\n'
         '  value.reserve(string_length(value))\n', 'string_length(value)'),
    ]:
        invalid = pathlib.Path(directory) / "nested-invalid.moss"
        invalid.write_text(body, encoding="utf-8")
        result = run("check", invalid, "--json")
        assert result.returncode != 0, body
        payload = json.loads(result.stdout)
        assert payload["error"]["code"] == "OWNERSHIP_CONFLICTING_ACCESS", payload
        entities = payload["error"]["cause"]["entities"]
        assert any(item.get("expression") == wrapped for item in entities), entities

print("Phase 24 String concatenation checks passed")
