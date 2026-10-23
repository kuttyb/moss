"""Native consumer acceptance for the FileIO-backed moss.fs package.

Margo's package-test harness does not provide the Phase 20 FileIO host shim, so
the durable acceptance is a real root application compiled against the package.
The generated fixture stays under repository tmp/ and is never a user path.
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile

PACKAGE = Path(__file__).resolve().parents[1]
ROOT = Path(__file__).resolve().parents[3]
MARGO = ROOT / "margo"
MOSS = Path(os.environ.get("MOSS", ROOT / "moss")).resolve()
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)
ENV = os.environ.copy()
ENV.update(MOSS=str(MOSS), MARGO_HOME=str(SCRATCH / "margo-host-fs"), TMPDIR=str(SCRATCH))

# The provider itself must lower and produce an ABI-v9 interface.
subprocess.run([str(MARGO), "build", "--json"], cwd=PACKAGE, env=ENV,
               check=True, capture_output=True, text=True)
provider_build = PACKAGE / "build" / "debug"
assert "native_abi 9" in (provider_build / "fs.mossi").read_text(encoding="utf-8")

with tempfile.TemporaryDirectory(prefix="host-fs-", dir=SCRATCH) as temporary:
    work = Path(temporary)
    fixture = work / "input.bin"
    output_fixture = work / "output.bin"
    fixture.write_bytes(b"Moss host services\n")
    input_path = fixture.relative_to(ROOT).as_posix()
    output_path = output_fixture.relative_to(ROOT).as_posix()
    app = work / "app"
    (app / "src").mkdir(parents=True)
    (app / "Moss.toml").write_text(
        '[package]\nname = "host_fs_consumer"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n\n'
        '[dependencies]\n"moss.fs" = { path = "../../../sdk/fs" }\n',
        encoding="utf-8")
    (app / "src" / "main.moss").write_text(f'''module app
import fs

fn main():
  try:
    first = fs.read_prefix("{input_path}", 0)
    whole = fs.read_all("{input_path}")
    limited = fs.read_limited("{input_path}", 64)
    tail = fs.read_prefix("{input_path}", 5)
    fs.copy_prefix("{input_path}", "{output_path}")
    copied = fs.read_prefix("{output_path}", 0)
    assert((first |> count) > 0)
    assert((whole |> count) == (first |> count))
    assert((limited |> count) == (first |> count))
    assert((tail |> count) > 0)
    assert((copied |> count) == (first |> count))
    echo first[0], tail[0], copied[0]
  recover:
    echo "unexpected FileIO error"
  try:
    ignored = fs.read_prefix("{input_path}", -1)
    assert(false)
  recover fs.FsError.InvalidOffset:
    echo "negative offset rejected"
  recover:
    echo "wrong negative-offset error"
  try:
    ignored = fs.read_prefix("{work.relative_to(ROOT).as_posix()}/no-such-file.bin", 0)
    assert(false)
  recover FileError.NotFound:
    echo "missing file reported"
  recover:
    echo "wrong missing-file error"
  try:
    ignored = fs.read_limited("{input_path}", 4)
    assert(false)
  recover fs.FsError.LimitExceeded:
    echo "read limit rejected"
  recover:
    echo "wrong read-limit error"
  try:
    ignored = fs.read_limited("{input_path}", -1)
    assert(false)
  recover fs.FsError.InvalidLimit:
    echo "negative read limit rejected"
  recover:
    echo "wrong negative-limit error"
''', encoding="utf-8")
    (app / "tests").mkdir()
    (app / "tests" / "smoke.moss").write_text(f'''import fs

test "FileIO is native-only":
  data = fs.read_prefix("{input_path}", 0)
  assert((data |> count) > 0)
''', encoding="utf-8")
    for profile in ("debug", "release"):
        args = [str(MARGO)]
        if profile == "release":
            args.append("--release")
        args.extend(("build", "--json"))
        subprocess.run(args, cwd=app, env=ENV, check=True)
        executable = app / "build" / profile / "host_fs_consumer"
        result = subprocess.run([str(executable)], cwd=ROOT, env=ENV,
                                check=True, capture_output=True, text=True)
        assert result.stdout == ("77 104 77\nnegative offset rejected\nmissing file reported\n"
                                 "read limit rejected\nnegative read limit rejected\n"), (profile, result.stdout)

    # Fast Debug intentionally has no FileIO backend in Moss v0.1. Verify the
    # package reports the capability boundary rather than claiming parity.
    interp = subprocess.run([str(MARGO), "test", "--interp"], cwd=app, env=ENV,
                            capture_output=True, text=True)
    assert interp.returncode != 0
    assert "FileIO" in (interp.stdout + interp.stderr) or "FAST_DEBUG" in (interp.stdout + interp.stderr), (interp.stdout, interp.stderr)

print("moss.fs native debug/release consumer and Fast Debug boundary passed")
