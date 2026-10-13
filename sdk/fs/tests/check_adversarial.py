"""Permanent adversarial path/FileIO boundary regressions from the Team H Architect."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]
MARGO = ROOT / "margo"
SCRATCH = ROOT / "tmp"
SCRATCH.mkdir(exist_ok=True)
ENV = os.environ | {
    "TMPDIR": str(SCRATCH),
    "MARGO_HOME": str(SCRATCH / "margo-host-fs-adversarial"),
}

with tempfile.TemporaryDirectory(prefix="host-fs-adversarial-", dir=SCRATCH) as temporary:
    work = Path(temporary)
    app = work / "app"
    fixture_dir = work / "fixtures"
    (app / "src").mkdir(parents=True)
    fixture_dir.mkdir()
    (app / "Moss.toml").write_text(
        '[package]\nname = "team_h_final_adversarial"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n\n'
        '[dependencies]\n"moss.path" = { path = "../../../sdk/path" }\n'
        '"moss.fs" = { path = "../../../sdk/fs" }\n',
        encoding="utf-8")

    empty = fixture_dir / "empty.bin"
    exact = fixture_dir / "exact.bin"
    over = fixture_dir / "over.bin"
    stale = fixture_dir / "stale.bin"
    empty.write_bytes(b"")
    exact.write_bytes(bytes(i % 251 for i in range(4096)))
    over.write_bytes(bytes(i % 251 for i in range(4097)))
    stale.write_bytes(b"Z" * 5000)
    empty_path = empty.relative_to(ROOT).as_posix()
    exact_path = exact.relative_to(ROOT).as_posix()
    over_path = over.relative_to(ROOT).as_posix()
    stale_path = stale.relative_to(ROOT).as_posix()

    (app / "src" / "main.moss").write_text(f'''module app
import path
import fs

fn main():
  assertEqual(path.normalize("///"), "/")
  assertEqual(path.normalize("../../a/../b"), "../../b")
  assertEqual(path.normalize("/a/../../../b"), "/b")
  assertEqual(path.join("α//β", "../γ"), "α/γ")
  assertEqual(path.basename("/α//β/"), "β")
  assertEqual(path.dirname("/α//β/"), "/α")
  assertEqual(path.extension(".env"), "")
  assertEqual(path.extension("archive."), "")
  assertEqual(path.stem("archive."), "archive.")
  assert(path.has_prefix("/a/bc/d", "/a"))
  assert(not path.has_prefix("/a/bc/d", "/a/b"))
  assert(path.has_suffix("/a/bc/d", "bc/d"))
  assert(not path.has_suffix("/a/bc/d", "c/d"))
  try:
    empty = fs.read_all("{empty_path}")
    exact = fs.read_all("{exact_path}")
    over = fs.read_all("{over_path}")
    limited = fs.read_limited("{over_path}", 4097)
    tail = fs.read_prefix("{over_path}", 4095)
    beyond = fs.read_prefix("{over_path}", 5000)
    fs.copy_prefix("{over_path}", "{stale_path}")
    stale = fs.read_all("{stale_path}")
    assertEqual(empty |> count, 0)
    assertEqual(exact |> count, 4096)
    assertEqual(over |> count, 4097)
    assertEqual(limited |> count, 4097)
    assertEqual(tail |> count, 2)
    assertEqual(beyond |> count, 0)
    assertEqual(stale |> count, 5000)
    assertEqual(stale[0], over[0])
    assertEqual(stale[4095], over[4095])
    assertEqual(stale[4096], 90)
    echo "edge cases passed"
  recover:
    echo "unexpected file error"
  try:
    bad = fs.read_prefix("tmp", 0)
    assert(false)
  recover FileError.NotRegularFile:
    echo "directory rejected"
  recover:
    echo "wrong directory error"
  try:
    too_large = fs.read_limited("{over_path}", 4096)
    assert(false)
  recover fs.FsError.LimitExceeded:
    echo "limit rejected"
  recover:
    echo "wrong limit error"
''', encoding="utf-8")

    for profile in ("debug", "release"):
        args = [str(MARGO)]
        if profile == "release":
            args.append("--release")
        args.extend(["build", "--json"])
        subprocess.run(args, cwd=app, env=ENV, check=True,
                       capture_output=True, text=True)
        result = subprocess.run(
            [str(app / "build" / profile / "team_h_final_adversarial")],
            cwd=ROOT, env=ENV, check=True, capture_output=True, text=True)
        assert result.stdout == "edge cases passed\ndirectory rejected\nlimit rejected\n", (
            profile, result.stdout, result.stderr)

print("Team H path/FileIO boundary regressions passed in native debug and release")
