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
MOSS = ROOT / "moss"
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
    fixture = SCRATCH / "phase24-host-fs-input.bin"
    fixture.write_bytes(b"Moss host services\n")
    app = work / "app"
    (app / "src").mkdir(parents=True)
    (app / "Moss.toml").write_text(
        '[package]\nname = "host_fs_consumer"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n\n'
        '[dependencies]\n"moss.fs" = { path = "../../../sdk/fs" }\n',
        encoding="utf-8")
    (app / "src" / "main.moss").write_text('''module app
import fs

fn main():
  try:
    first = fs.read_prefix("tmp/phase24-host-fs-input.bin", 0)
    whole = fs.read_all("tmp/phase24-host-fs-input.bin")
    tail = fs.read_prefix("tmp/phase24-host-fs-input.bin", 5)
    fs.copy_prefix("tmp/phase24-host-fs-input.bin", "tmp/phase24-host-fs-output.bin")
    copied = fs.read_prefix("tmp/phase24-host-fs-output.bin", 0)
    assert((first |> count) > 0)
    assert((whole |> count) == (first |> count))
    assert((tail |> count) > 0)
    assert((copied |> count) == (first |> count))
    echo first[0], tail[0], copied[0]
  recover:
    echo "unexpected FileIO error"
  try:
    ignored = fs.read_prefix("tmp/phase24-host-fs-input.bin", -1)
    assert(false)
  recover fs.FsError.InvalidOffset:
    echo "negative offset rejected"
  recover:
    echo "wrong negative-offset error"
  try:
    ignored = fs.read_prefix("tmp/phase24-host-fs-no-such-file.bin", 0)
    assert(false)
  recover FileError.NotFound:
    echo "missing file reported"
  recover:
    echo "wrong missing-file error"
''', encoding="utf-8")
    (app / "tests").mkdir()
    (app / "tests" / "smoke.moss").write_text('''import fs

test "FileIO is native-only":
  data = fs.read_prefix("tmp/phase24-host-fs-input.bin", 0)
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
        assert result.stdout == "77 104 77\nnegative offset rejected\nmissing file reported\n", (profile, result.stdout)

    # Fast Debug intentionally has no FileIO backend in Moss v0.1. Verify the
    # package reports the capability boundary rather than claiming parity.
    interp = subprocess.run([str(MARGO), "test", "--interp"], cwd=app, env=ENV,
                            capture_output=True, text=True)
    assert interp.returncode != 0
    assert "FileIO" in (interp.stdout + interp.stderr) or "FAST_DEBUG" in (interp.stdout + interp.stderr), (interp.stdout, interp.stderr)

print("moss.fs native debug/release consumer and Fast Debug boundary passed")
