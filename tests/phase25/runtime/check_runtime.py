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

# 1. Runtime unit tests
run("rustc", "--edition=2021", "--test", RUNTIME / "src/lib.rs", "-o", OUT / "runtime_unit")
run(OUT / "runtime_unit")

# 2. Multi-crate linkage for both Debug (-g) and Release (-O) profiles
for profile, opt_flag in [("debug", "-g"), ("release", "-O")]:
    profile_dir = OUT / profile
    profile_dir.mkdir(parents=True, exist_ok=True)
    runtime = profile_dir / "libmoss_runtime.rlib"
    provider = profile_dir / "libprovider.rlib"
    consumer_a = profile_dir / "libconsumer_a.rlib"
    consumer_b = profile_dir / "libconsumer_b.rlib"
    executable = profile_dir / "two_consumers"

    # Build shared moss_runtime
    run("rustc", "--edition=2021", "--crate-type=rlib", "--crate-name=moss_runtime",
        opt_flag, RUNTIME / "src/lib.rs", "-o", runtime)

    # Behavior 1 & 3: Build provider from temporary source, then remove source before consumer build
    temp_provider_src = profile_dir / "provider_isolated.rs"
    temp_provider_src.write_text((HERE / "provider.rs").read_text())
    run("rustc", "--edition=2021", "--crate-type=rlib", "--crate-name=provider",
        opt_flag, temp_provider_src, "--extern", f"moss_runtime={runtime}",
        "-L", f"dependency={profile_dir}", "-o", provider)
    temp_provider_src.unlink()  # Provider source removed!
    assert not temp_provider_src.exists()

    # Behavior 2: Independently compile two consumers with only provider and runtime rlibs
    for name, target in (("consumer_a", consumer_a), ("consumer_b", consumer_b)):
        run("rustc", "--edition=2021", "--crate-type=rlib", f"--crate-name={name}",
            opt_flag, HERE / f"{name}.rs", "--extern", f"moss_runtime={runtime}",
            "--extern", f"provider={provider}", "-L", f"dependency={profile_dir}", "-o", target)

    # Link executable
    run("rustc", "--edition=2021", opt_flag, HERE / "main.rs",
        "--extern", f"moss_runtime={runtime}",
        "--extern", f"provider={provider}",
        "--extern", f"consumer_a={consumer_a}",
        "--extern", f"consumer_b={consumer_b}",
        "-L", f"dependency={profile_dir}", "-o", executable)

    output = run(executable)
    assert output == "shared moss_runtime Map and Set passed\n", f"Unexpected output in {profile}: {output}"
    print(f"moss_runtime {profile} ({opt_flag}) passed; artifact SHA-256: {hashlib.sha256(runtime.read_bytes()).hexdigest()}")

# 3. Behavior 8: Runtime identity mismatch detection
mismatch_dir = OUT / "mismatch"
mismatch_dir.mkdir(parents=True, exist_ok=True)
forged_runtime_src = mismatch_dir / "lib.rs"
forged_runtime_src.write_text("pub struct MismatchedRuntime;\n")
forged_runtime = mismatch_dir / "libmoss_runtime.rlib"
run("rustc", "--edition=2021", "--crate-type=rlib", "--crate-name=moss_runtime",
    forged_runtime_src, "-o", forged_runtime)

# A: Standalone consumer compiled against forged runtime fails because MossMap is missing
standalone_src = mismatch_dir / "standalone.rs"
standalone_src.write_text("use moss_runtime::MossMap;\nfn main() {}\n")
standalone_res = subprocess.run(
    ["rustc", "--edition=2021", "--crate-type=rlib", str(standalone_src),
     "--extern", f"moss_runtime={forged_runtime}"],
    cwd=ROOT, text=True, capture_output=True)
assert standalone_res.returncode != 0, "Standalone consumer build should have failed with forged runtime!"
assert "no `MossMap` in the root" in standalone_res.stderr or "could not find `MossMap`" in standalone_res.stderr

# B: Cross-crate linkage with mismatched runtime fails because provider was compiled against real moss_runtime
cross_res = subprocess.run(
    ["rustc", "--edition=2021", "--crate-type=rlib", "--crate-name=consumer_a",
     str(HERE / "consumer_a.rs"), "--extern", f"moss_runtime={forged_runtime}",
     "--extern", f"provider={OUT / 'debug' / 'libprovider.rlib'}",
     "-L", f"dependency={OUT / 'debug'}",
     "-o", str(mismatch_dir / "libconsumer_a.rlib")],
    cwd=ROOT, text=True, capture_output=True)
assert cross_res.returncode != 0, "Cross-crate build should have failed due to runtime crate mismatch!"
assert "can't find crate for `provider`" in cross_res.stderr or "mismatched" in cross_res.stderr


# Invariant checks
source = (RUNTIME / "src/map.rs").read_text()
assert "pub struct MossMap<K, V>" in source
assert "pub struct MossSet<K>" in source
assert "dyn " not in source
assert "RandomState" not in source
print("moss_runtime all unit, profile, source-removal, and identity-mismatch proofs passed;")
