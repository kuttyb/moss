"""Exercise moss.encoding through current source-backed interfaces."""

import json
import os
from pathlib import Path
import subprocess
import tempfile

PACKAGE = Path(__file__).resolve().parents[1]
ROOT = Path(__file__).resolve().parents[3]
MOSS = Path(os.environ.get("MOSS", ROOT / "moss")).resolve()
MARGO = ROOT / "margo"
BYTES = ROOT / "sdk" / "foundations" / "bytes"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)
ENV = os.environ.copy()
ENV.update(MOSS=str(MOSS), MARGO_HOME=str(SCRATCH / "margo-encoding-source-free"),
           TMPDIR=str(SCRATCH))

subprocess.run([str(MARGO), "build", "--json"], cwd=PACKAGE, env=ENV,
               check=True, capture_output=True, text=True)
with tempfile.TemporaryDirectory(prefix="encoding-source-free-", dir=SCRATCH) as temporary:
    work = Path(temporary)
    app = work / "app"
    (app / "src").mkdir(parents=True)
    for interface in (PACKAGE / "build/debug/encoding.mossi",
                      BYTES / "build/debug/bytes.mossi"):
        assert "native_abi 9" in interface.read_text(encoding="utf-8"), interface
    (app / "Moss.toml").write_text(
        '[package]\nname = "encoding_source_free_smoke"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n', encoding="utf-8")
    (app / "src" / "main.moss").write_text('''module app
import encoding

fn same_ints(left: Vector[Int], right: Vector[Int]) -> Bool:
  if (left |> count) != (right |> count):
    return false
  var i = 0
  while i < (left |> count):
    if left[i] != right[i]:
      return false
    i = i + 1
  return true

fn decode_error_fingerprint(values: Vector[Int]) -> Vector[Int]:
  try:
    decoded = encoding.decode_utf8(values)
    return [0, decoded |> count]
  recover encoding.Utf8Error.InvalidByte(index, value):
    return [1, index, value]
  recover encoding.Utf8Error.UnexpectedContinuation(index, value):
    return [2, index, value]
  recover encoding.Utf8Error.InvalidLeadingByte(index, value):
    return [3, index, value]
  recover encoding.Utf8Error.TruncatedSequence(index, expected_bytes, available_bytes):
    return [4, index, expected_bytes, available_bytes]
  recover encoding.Utf8Error.InvalidContinuation(index, value):
    return [5, index, value]
  recover encoding.Utf8Error.OverlongEncoding(index, scalar):
    return [6, index, scalar]
  recover encoding.Utf8Error.SurrogateScalar(index, scalar):
    return [7, index, scalar]
  recover encoding.Utf8Error.InvalidScalar(index, scalar):
    return [8, index, scalar]

fn encode_error_fingerprint(values: Vector[Int]) -> Vector[Int]:
  try:
    encoded = encoding.encode_utf8(values)
    return [0, encoded |> count]
  recover encoding.Utf8Error.InvalidByte(index, value):
    return [1, index, value]
  recover encoding.Utf8Error.UnexpectedContinuation(index, value):
    return [2, index, value]
  recover encoding.Utf8Error.InvalidLeadingByte(index, value):
    return [3, index, value]
  recover encoding.Utf8Error.TruncatedSequence(index, expected_bytes, available_bytes):
    return [4, index, expected_bytes, available_bytes]
  recover encoding.Utf8Error.InvalidContinuation(index, value):
    return [5, index, value]
  recover encoding.Utf8Error.OverlongEncoding(index, scalar):
    return [6, index, scalar]
  recover encoding.Utf8Error.SurrogateScalar(index, scalar):
    return [7, index, scalar]
  recover encoding.Utf8Error.InvalidScalar(index, scalar):
    return [8, index, scalar]

fn main():
  try:
    encoded = encoding.encode_utf8([0, 127, 128578])
    assertEqual(encoded |> count, 6)
    decoded = encoding.decode_utf8(encoded)
    assertEqual(decoded[2], 128578)
    assert(same_ints(decode_error_fingerprint([-1]), [1, 0, -1]))
    assert(same_ints(decode_error_fingerprint([65, 256]), [1, 1, 256]))
    assert(same_ints(decode_error_fingerprint([65, 128]), [2, 1, 128]))
    assert(same_ints(decode_error_fingerprint([65, 255]), [3, 1, 255]))
    assert(same_ints(decode_error_fingerprint([65, 226, 130]), [4, 1, 3, 2]))
    assert(same_ints(decode_error_fingerprint([65, 226, 40, 161]), [5, 2, 40]))
    assert(same_ints(decode_error_fingerprint([65, 192, 128]), [6, 1, 0]))
    assert(same_ints(decode_error_fingerprint([65, 237, 160, 128]), [7, 1, 55296]))
    assert(same_ints(decode_error_fingerprint([65, 244, 144, 128, 128]), [8, 1, 1114112]))
    assert(same_ints(encode_error_fingerprint([65, -1]), [8, 1, -1]))
    assert(same_ints(encode_error_fingerprint([65, 1114112]), [8, 1, 1114112]))
    assert(same_ints(encode_error_fingerprint([65, 55296]), [7, 1, 55296]))
    echo "UTF-8 error contracts ok"
  recover error:
    echo "unexpected propagated UTF-8 error"
''', encoding="utf-8")
    source_free_env = ENV | {
        "MOSS_MODULE_PATH": os.pathsep.join((
            str(PACKAGE / "build" / "debug"),
            str(BYTES / "build" / "debug"),
        ))
    }
    checked = subprocess.run([str(MOSS), "check", "src/main.moss", "--json"],
                             cwd=app, env=source_free_env, check=True,
                             capture_output=True, text=True)
    assert json.loads(checked.stdout)["ok"] is True, checked.stdout
    subprocess.run([str(MOSS), "build", "--json"], cwd=app,
                   env=source_free_env, check=True, capture_output=True, text=True)
    result = subprocess.run([str(app / "build" / "debug" / "encoding_source_free_smoke")],
                            cwd=app, env=source_free_env, check=True,
                            capture_output=True, text=True)
    assert result.stdout == "UTF-8 error contracts ok\n", result.stdout
print("Current ABI-v9 moss.encoding provider interface: exact error contracts passed")
