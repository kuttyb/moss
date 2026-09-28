#!/usr/bin/env python3
"""Map.delete ownership and Fast Debug checks without rustc."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
MOSS = ROOT / 'moss'

def run(*args):
    return subprocess.run([str(MOSS), *map(str,args)],cwd=ROOT,text=True,capture_output=True)

with tempfile.TemporaryDirectory(dir=ROOT/'tmp') as directory:
    path=pathlib.Path(directory)/'map.moss'
    path.write_text('''fn main():
  values = Map()
  values["red"] = "old"
  var found = false
  removed = values.delete("red", "fallback", found)
  assertEqual(removed, "old")
  assert(found)
  missing = values.delete("red", "fallback", found)
  assertEqual(missing, "fallback")
  assert(not found)
  assertEqual(values.keys() |> count, 0)
''')
    result=run('run','--interp',path)
    assert result.returncode==0,result
    path.write_text('''fn take(m: Map[String, String], fallback: String, found: Bool) -> String:
  return m.delete("x", fallback, found)
fn main():
  values = Map()
  values["x"] = "old"
  var found = false
  result = take(values, "new", found)
  assertEqual(result, "old")
  assert(found)
''')
    result=run('run','--interp',path)
    assert result.returncode==0,result
    path.write_text('''fn main():
  values = Map()
  values["x"] = "old"
  var found = false
  fallback = "new"
  result = values.delete("x", fallback, found)
  echo fallback
''')
    result=run('check',path)
    assert result.returncode != 0 and 'OWNERSHIP_USE_AFTER_CONSUME' in result.stderr,result
    for suffix,expected in [
      ('  x = values.delete(1, "fallback", found)\n','key type mismatch'),
      ('  x = values.delete("red", 1, found)\n','fallback type mismatch'),
      ('  x = values.delete("red", "fallback", 1)\n','flag must be Bool'),
      ('  x = values.delete("red", "fallback", false)\n','writable Bool location'),
      ('  let immutable = false\n  x = values.delete("red", "fallback", immutable)\n','immutable local'),
      ('  x = values.delete("red", "fallback", found, 1)\n','expects key'),
    ]:
      path.write_text('fn main():\n  values = Map()\n  values["red"] = "old"\n  var found = false\n'+suffix)
      result=run('check',path)
      assert result.returncode != 0,(suffix,result)
      if expected: assert expected in result.stderr,(suffix,result.stderr)
print('Phase 15.15 Map delete Fast Debug checks passed')
