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
        ('fn main():\n  x = "hi".join([1])\n', 'String join expects vector[string]'),
        ('fn main():\n  x = "hi".length(1)\n', 'expects no argument'),
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
print('Phase 15.15 String API Fast Debug checks passed')
