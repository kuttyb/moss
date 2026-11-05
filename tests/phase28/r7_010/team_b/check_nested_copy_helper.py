#!/usr/bin/env python3
"""Verify exact explicit-copy facts own their nested Rust materializers."""
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
MOSS = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()
OUT = Path(sys.argv[2] if len(sys.argv) > 2 else ROOT / "tmp/team-b/nested-copy-helper").resolve()
OUT.mkdir(parents=True, exist_ok=True)
source = OUT / "nested_copy_helper.moss"
source.write_text("type Metadata:\n  label: String\ntype Record:\n  metadata: Metadata\ndomain Lookup:\n  record: Record = Record(metadata: Metadata(label: \"hello\"))\n  fn Get() -> Record:\n    reply copy(record)\nfn main():\n  lookup = Lookup()\n  value = message lookup.Get()\n  echo value.metadata.label\n")

def run(args, env=None):
    return subprocess.run(list(map(str, args)), cwd=ROOT, env=env,
                          capture_output=True, text=True, check=False, timeout=90)

check = run([MOSS, "check", "--json", source])
assert check.returncode == 0, (check.stdout, check.stderr)
fast = run([MOSS, "run", "--interp", source])
assert fast.returncode == 0 and fast.stdout.strip() == "hello", (fast.stdout, fast.stderr)

for mode, flags in (("debug", []), ("optimized", ["-O"])):
    rust = OUT / f"nested-copy-{mode}.rs"
    binary = OUT / f"nested-copy-{mode}"
    emitted = run([MOSS, *flags, source, "-o", rust])
    assert emitted.returncode == 0, (emitted.stdout, emitted.stderr)
    generated = rust.read_text()
    marker = re.search(
        r"/\* Moss copy fact call: (typed-copy-fact-v1:[0-9a-f]{64}) \*/\s*"
        r"(__moss_copy_value_([0-9a-f]{16}))\(", generated)
    assert marker, "exact fact marker is not adjacent to a per-fact helper call"
    helper = marker.group(2)
    assert marker.group(1).split(":", 1)[1].startswith(marker.group(3))
    assert generated.count(f"fn {helper}(") == 1
    assert generated.count(f"{helper}(") == 2, "per-fact helper has an extra caller"
    assert "__moss_field_metadata()" in generated
    assert "__moss_field_label().clone()" in generated
    assert "MossAccess_Metadata__moss_value()" not in generated
    assert "MossAccess_Record__moss_value()" not in generated
    compiled = run(["rustc", *flags, rust, "-o", binary])
    assert compiled.returncode == 0, (compiled.stdout, compiled.stderr)
    native = run([binary])
    assert native.returncode == 0 and native.stdout.strip() == "hello", (native.stdout, native.stderr)

for hook in ("MOSS_TEST_CONSUMER_DROP_COPY_FACT", "MOSS_TEST_CONSUMER_PERTURB_COPY_FACT", "MOSS_TEST_CONSUMER_DELETE_COPY_REQUIREMENT", "MOSS_TEST_CONSUMER_DUPLICATE_COPY_FACT", "MOSS_TEST_CONSUMER_DUPLICATE_COPY_REQUIREMENT"):
    env = dict(os.environ)
    env[hook] = "1"
    failed = run([MOSS, source, "-o", OUT / f"corrupt-{hook}.rs"], env)
    assert failed.returncode != 0 and "MOSS_INTERNAL_INVARIANT" in (failed.stdout + failed.stderr), (hook, failed.stdout, failed.stderr)

print("per-fact nested explicit-copy materializer, Debug/Optimized/Fast Debug parity, and five authority corruptions PASS")
