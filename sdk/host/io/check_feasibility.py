"""Permanent Team H probes for the current host-I/O package boundary."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
MOSS = ROOT / "moss"
MARGO = ROOT / "margo"
TMP_ROOT = ROOT / "tmp"
ENV = os.environ | {"TMPDIR": str(ROOT / "tmp")}


def run(arguments: list[str], *, cwd: Path = ROOT) -> subprocess.CompletedProcess[str]:
    return subprocess.run(arguments, cwd=cwd, env=ENV, text=True,
                          capture_output=True, check=False)


with tempfile.TemporaryDirectory(prefix="host-io-feasibility-", dir=TMP_ROOT) as scratch_dir:
    SCRATCH = Path(scratch_dir)
    fixture = SCRATCH / "supported.bin"
    fixture.write_bytes(b"Moss\n")
    fixture_path = fixture.relative_to(ROOT).as_posix()
    copy_path = (SCRATCH / "copy.bin").relative_to(ROOT).as_posix()
    same_package_source = SCRATCH / "same_package.moss"
    same_package_source.write_text(
        (HERE / "feasibility" / "same_package.moss").read_text(encoding="utf-8")
        .replace("tmp/phase24-host-io-supported.bin", fixture_path)
        .replace("tmp/phase24-host-io-copy.bin", copy_path),
        encoding="utf-8")

    native = SCRATCH / "same-package-native"
    built = run([str(ROOT / "tools" / "moss-build-debug"),
                 str(same_package_source), "-o", str(native)])
    assert built.returncode == 0, built.stdout + built.stderr
    same = run([str(native)])
    assert same.returncode == 0, same.stdout + same.stderr
    assert same.stdout == "5\n428\n5\n5\n", same.stdout

    release_rust = SCRATCH / "same-package-release.rs"
    release_native = SCRATCH / "same-package-release"
    lowered = run([str(MOSS), str(same_package_source), "-o", str(release_rust)])
    assert lowered.returncode == 0, lowered.stdout + lowered.stderr
    optimized = run(["rustc", "-D", "warnings", "-C", "opt-level=3",
                     str(release_rust), "-o", str(release_native)])
    assert optimized.returncode == 0, optimized.stdout + optimized.stderr
    release_same = run([str(release_native)])
    assert release_same.returncode == 0, release_same.stdout + release_same.stderr
    assert release_same.stdout == same.stdout, release_same.stdout

    fast = run([str(MOSS), "run", "--interp", str(same_package_source)])
    assert fast.returncode != 0, "Fast Debug unexpectedly executed FileIO"
    assert "FileIO" in fast.stdout + fast.stderr, fast.stdout + fast.stderr

    unbounded = run([str(MOSS), "check",
                     str(HERE / "feasibility" / "unbounded_range_write.moss"), "--json"])
    unbounded_payload = json.loads(unbounded.stdout)
    assert unbounded.returncode != 0
    assert unbounded_payload["error"]["code"] == "FILEIO_UNBOUNDED_REQUEST", unbounded.stdout

    for name, symbol in (("env_probe.moss", "Env"), ("process_probe.moss", "Process")):
        checked = run([str(MOSS), "check", str(HERE / "feasibility" / name), "--json"])
        payload = json.loads(checked.stdout)
        assert checked.returncode != 0
        assert payload["error"]["code"] == "UNKNOWN_SYMBOL_OR_TYPE", checked.stdout
        assert symbol in payload["error"]["message"], checked.stdout

    project = SCRATCH / "provider-boundary"
    shutil.copytree(HERE / "feasibility", project,
                    ignore=shutil.ignore_patterns("*.mossbuild", "build"))
    (project / "consumer" / "input.bin").write_bytes(b"M")

    provider = run([str(MARGO), "build", "--json"], cwd=project / "provider")
    assert provider.returncode == 0, provider.stdout + provider.stderr
    consumer = run([str(MARGO), "build", "--json"], cwd=project / "consumer")
    assert consumer.returncode == 0, consumer.stdout + consumer.stderr
    executed = run([str(project / "consumer" / "build" / "debug" /
                        "hostio_consumer_reproducer")], cwd=project / "consumer")
    assert executed.returncode == 0, executed.stdout + executed.stderr
    assert executed.stdout == "77\n", executed.stdout

    provider_release = run([str(MARGO), "build", "--release", "--json"],
                           cwd=project / "provider")
    assert provider_release.returncode == 0, provider_release.stdout + provider_release.stderr
    consumer_release = run([str(MARGO), "build", "--release", "--json"],
                           cwd=project / "consumer")
    assert consumer_release.returncode == 0, consumer_release.stdout + consumer_release.stderr
    release_executed = run([str(project / "consumer" / "build" / "release" /
                                "hostio_consumer_reproducer")], cwd=project / "consumer")
    assert release_executed.returncode == 0, release_executed.stdout + release_executed.stderr
    assert release_executed.stdout == "77\n", release_executed.stdout

    capability_provider = run([str(MARGO), "build", "--json"],
                              cwd=project / "capability_provider")
    assert capability_provider.returncode == 0, capability_provider.stdout + capability_provider.stderr
    capability_consumer = run([str(MARGO), "build", "--json"],
                              cwd=project / "capability_consumer")
    assert capability_consumer.returncode != 0, "cross-package FileIO capability unexpectedly linked"
    capability_payload = json.loads(capability_consumer.stdout)
    assert capability_payload["ok"] is False, capability_consumer.stdout
    detail = capability_payload["error"]
    assert "BUILD_BACKEND_ERROR" in detail, detail
    assert "moss_hostcap::FileIO" in detail, detail
    assert "moss_fileio::FileIO" in detail, detail

print("Team H host-I/O feasibility: local and path-only provider passed; capability boundary blocker reproduced")
