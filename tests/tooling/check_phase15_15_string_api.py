#!/usr/bin/env python3
"""Focused String API checks without a Rust toolchain."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
MOSS = ROOT / 'moss'


def run(*args):
    return subprocess.run([str(MOSS), *map(str, args)], cwd=ROOT,
                          text=True, capture_output=True)


positive = ROOT / 'tests/phase15_15_string_map.moss'
checked = run('check', positive)
assert checked.returncode == 0, checked.stderr
interpreted = run('run', '--interp', positive)
assert interpreted.returncode == 0 and interpreted.stdout.strip() == 'string/map ok', interpreted

with tempfile.TemporaryDirectory(dir=ROOT / 'tmp') as directory:
    base = pathlib.Path(directory)
    for source, expected in [
        ('fn main():\n  x = "hi".char_at("0")\n', 'String char_at expects int'),
        ('fn main():\n  x = "hi".split(1)\n', 'String split expects string'),
        ('fn main():\n  x = "hi".find(1)\n', 'String find expects string'),
        ('fn main():\n  x = "hi".slice("0", 1)\n', 'String slice expects int'),
        ('fn main():\n  x = "hi".slice(0, "1")\n', 'String slice expects int'),
        ('fn main():\n  x = "hi".ascii_lower(1)\n', 'expects 0 arguments'),
        ('fn main():\n  x = "hi".join([1])\n', 'String join expects vector[string]'),
        ('fn main():\n  x = "hi".length(1)\n', 'expects 0 arguments'),
    ]:
        path = base / 'invalid.moss'
        path.write_text(source)
        result = run('check', path)
        assert result.returncode != 0 and expected in result.stderr, result
    for source, expected in [
        ('fn main():\n  x = "hi".char_at(-1)\n  echo x\n', 'out of bounds'),
        ('fn main():\n  x = "hi".split("")\n  echo x\n', 'nonempty separator'),
    ]:
        path = base / 'invalid.moss'
        path.write_text(source)
        result = run('run', '--interp', path)
        assert result.returncode != 0 and expected in result.stderr, result
    generated = base / 'string_api.rs'
    compiled = run(positive, '-o', generated)
    assert compiled.returncode == 0, compiled.stderr
    rust = generated.read_text(encoding='utf-8')
    assert '.find(__moss_pattern.as_str())' in rust, 'find did not use native str::find'
    assert '.is_ascii()' in rust, 'slice omitted its code-point-safe ASCII byte fast path'
    assert '.char_indices()' in rust, 'slice did not traverse UTF-8 boundaries directly'
    assert '.to_ascii_lowercase()' in rust and '.to_ascii_uppercase()' in rust
    assert 'format!("{}{}", text, "".to_string())' not in rust
print('Phase 15.15 String API Fast Debug checks passed')
