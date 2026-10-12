#!/usr/bin/env python3
"""String concatenation/buffer lowering and Fast Debug regression checks."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
MOSS = ROOT / "moss"
SOURCE = ROOT / "tests" / "phase24_string_concat.moss"


def run(*args):
    return subprocess.run([str(MOSS), *map(str, args)], cwd=ROOT,
                          text=True, capture_output=True)


checked = run("check", SOURCE, "--json")
assert checked.returncode == 0, checked.stderr
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
    assert "__moss_string.push_str(__moss_part.as_str())" in rust

    executable = pathlib.Path(directory) / "concat-bin"
    native = subprocess.run(["rustc", "--edition", "2021", "-O", generated,
                             "-o", executable], cwd=ROOT, text=True,
                            capture_output=True)
    assert native.returncode == 0, native.stderr
    executed = subprocess.run([executable], cwd=ROOT, text=True,
                              capture_output=True)
    assert executed.returncode == 0, executed.stderr
    assert executed.stdout.strip() == "string concat ok", executed.stdout

    for body, diagnostic in [
        ('fn main():\n  value = "x"\n  value.append(value)\n',
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

print("Phase 24 String concatenation checks passed")
