"""Verify checked Map.get Write demand reaches the stored value in all engines."""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / "tmp/team-b/map-get-write-through"
OUT.mkdir(parents=True, exist_ok=True)
MOSS = Path(sys.argv[1]).resolve()
SOURCE = OUT / "map_get_write.moss"
SOURCE.write_text(
    "type Payload:\n"
    "  tag: String\n"
    "fn main():\n"
    "  var m = Map()\n"
    "  m[\"live\"] = Payload(tag: \"active\")\n"
    "  p = m.get(\"live\")\n"
    "  p.tag = \"updated\"\n"
    "  echo m.get(\"live\").tag\n")


def run(args, env=None, expect=0):
    proc = subprocess.run(list(map(str, args)), cwd=ROOT, env=env,
                          text=True, capture_output=True, check=False,
                          timeout=90)
    if proc.returncode != expect:
        raise RuntimeError(f"{args!r} returned {proc.returncode}\n"
                           f"{proc.stdout}\n{proc.stderr}")
    return proc.stdout.strip(), proc.stderr


assert run([MOSS, "check", "--json", SOURCE])[0]
assert run([MOSS, "run", "--interp", SOURCE])[0] == "updated"
for mode, moss_flags, rust_flags in (
        ("debug", [], []), ("optimized", ["-O"], ["-O"])):
    rust = OUT / f"map-get-{mode}.rs"
    binary = OUT / f"map-get-{mode}"
    run([MOSS, *moss_flags, SOURCE, "-o", rust])
    generated = rust.read_text()
    assert ".get_mut(" in generated, "checked Map.get Write did not use get_mut"
    run(["rustc", *rust_flags, rust, "-o", binary])
    assert run([binary])[0] == "updated"

for hook in (
        "MOSS_TEST_CONSUMER_DROP_OWNERSHIP_FACT",
        "MOSS_TEST_CONSUMER_PERTURB_OWNERSHIP_FACT",
        "MOSS_TEST_CONSUMER_DELETE_OWNERSHIP_RECORD",
        "MOSS_TEST_CONSUMER_DUPLICATE_OWNERSHIP_FACT",
        "MOSS_TEST_CONSUMER_DUPLICATE_OWNERSHIP_REQUIREMENT"):
    env = dict(os.environ)
    env[hook] = "1"
    for command in ([MOSS, "run", "--interp", SOURCE],
                    [MOSS, SOURCE, "-o", OUT / f"corrupt-{hook}.rs"]):
        stdout, stderr = run(command, env=env, expect=1)
        assert "MOSS_INTERNAL_INVARIANT" in stdout + stderr, (hook, stdout, stderr)

print("Map.get checked Write-through, Native Debug/Optimized/Fast Debug parity, and five corruption gates PASS")
