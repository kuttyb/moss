#!/usr/bin/env python3
"""Test the compiler-owned runtime and a bounded two-consumer linkage graph."""

from pathlib import Path
import hashlib
import shutil
import subprocess


ROOT = Path(__file__).resolve().parents[3]
RUNTIME = ROOT / "runtime" / "moss_runtime"
HERE = Path(__file__).resolve().parent
OUT = ROOT / "tmp" / "phase25-moss-runtime-proof"


def run(*args):
    result = subprocess.run([str(a) for a in args], cwd=ROOT, text=True, capture_output=True)
    if result.returncode:
        raise AssertionError(f"{' '.join(map(str, args))}\n{result.stdout}\n{result.stderr}")
    return result.stdout


shutil.rmtree(OUT, ignore_errors=True)
OUT.mkdir(parents=True)
runtime = OUT / "libmoss_runtime.rlib"
provider = OUT / "libprovider.rlib"
consumer_a = OUT / "libconsumer_a.rlib"
consumer_b = OUT / "libconsumer_b.rlib"
executable = OUT / "two_consumers"

run("rustc", "--edition=2021", "--test", RUNTIME / "src/lib.rs", "-o", OUT / "runtime_unit")
run(OUT / "runtime_unit")
run("rustc", "--edition=2021", "--crate-type=rlib", "--crate-name=moss_runtime",
    RUNTIME / "src/lib.rs", "-o", runtime)
run("rustc", "--edition=2021", "--crate-type=rlib", "--crate-name=provider",
    HERE / "provider.rs", "--extern", f"moss_runtime={runtime}", "-L", f"dependency={OUT}",
    "-o", provider)
for name, target in (("consumer_a", consumer_a), ("consumer_b", consumer_b)):
    run("rustc", "--edition=2021", "--crate-type=rlib", f"--crate-name={name}",
        HERE / f"{name}.rs", "--extern", f"moss_runtime={runtime}",
        "--extern", f"provider={provider}", "-L", f"dependency={OUT}", "-o", target)
run("rustc", "--edition=2021", HERE / "main.rs", "--extern", f"moss_runtime={runtime}",
    "--extern", f"provider={provider}", "--extern", f"consumer_a={consumer_a}",
    "--extern", f"consumer_b={consumer_b}", "-L", f"dependency={OUT}", "-o", executable)
assert run(executable) == "shared moss_runtime Map and Set passed\n"

source = (RUNTIME / "src/map.rs").read_text()
assert "pub struct MossMap<K, V>" in source
assert "pub struct MossSet<K>" in source
assert "dyn " not in source
assert "RandomState" not in source
print("moss_runtime unit and two-consumer proof passed;")
print("runtime artifact SHA-256:", hashlib.sha256(runtime.read_bytes()).hexdigest())
