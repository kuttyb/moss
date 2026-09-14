#!/usr/bin/env python3
"""Deterministic Phase 7 project/test/benchmark integration checks."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import sys


def run(
    compiler: Path,
    project: Path,
    *arguments: str,
    expected: int = 0,
    extra_env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(compiler), *arguments],
        cwd=project,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        env={**os.environ, "RUST_BACKTRACE": "0", **(extra_env or {})},
    )
    if result.returncode != expected:
        raise AssertionError(
            f"{' '.join(arguments)} returned {result.returncode}, expected {expected}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def envelope(result: subprocess.CompletedProcess[str], command: str) -> dict:
    value = json.loads(result.stdout)
    assert value["protocol_version"] == 1
    assert value["schema_version"] == "moss-agent-1"
    assert value["command"] == command
    return value


def write_rustc_wrapper(path: Path, rustc: Path, identity: str) -> None:
    path.write_text(
        "#!/bin/sh\n"
        'if [ "$#" -eq 2 ] && [ "$1" = "--version" ] && '
        '[ "$2" = "--verbose" ]; then\n'
        f"  printf '%s\\n' 'rustc 1.99.0-moss-test ({identity})' "
        f"'binary: rustc' 'host: moss-test' 'LLVM version: test-{identity}'\n"
        "  exit 0\n"
        "fi\n"
        f"exec {shlex.quote(str(rustc))} \"$@\"\n",
        encoding="utf-8",
    )
    path.chmod(0o755)


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: check_project_workflow.py <moss> <scratch-dir>")
    compiler = Path(sys.argv[1]).resolve()
    scratch = Path(sys.argv[2]).resolve() / "phase7-projects"
    repository = Path(__file__).resolve().parents[2]
    if scratch.exists():
        shutil.rmtree(scratch)
    scratch.mkdir(parents=True)

    demo = scratch / "demo"
    failing = scratch / "failing"
    shutil.copytree(repository / "examples/projects/phase7_demo", demo)
    shutil.copytree(repository / "examples/projects/phase7_failing_test", failing)
    for generated in (demo / "build", demo / ".moss", failing / "build"):
        if generated.exists():
            shutil.rmtree(generated)

    bootstrap = envelope(
        run(compiler, demo / "src", "agent", "bootstrap", "--json"),
        "agent bootstrap",
    )
    assert Path(bootstrap["result"]["project_root"]) == demo
    assert "project_builds" in bootstrap["result"]["capabilities"]

    first_build = envelope(run(compiler, demo, "build", "--json"), "build")
    artifacts = first_build["result"]["artifacts"]
    assert first_build["result"]["profile"] == "debug"
    assert first_build["result"]["reused"] is False
    executable = Path(artifacts["executable"])
    assert executable.is_file()
    assert Path(artifacts["generated_rust"]).is_file()
    assert Path(artifacts["cache_metadata"]).is_file()
    backend = first_build["result"]["backend_toolchain"]
    assert Path(backend["resolved_rustc"]).is_absolute()
    assert backend["version_verbose"].startswith("rustc ")
    assert backend["compile_flags"] == [
        "--edition=2021",
        "-D",
        "warnings",
        "-g",
        "-C",
        "opt-level=0",
    ]
    assert backend["fingerprint"]
    application_rust = Path(artifacts["generated_rust"]).read_text(encoding="utf-8")
    assert "MOSS_TEST|" not in application_rust
    assert "MOSS_BENCH|" not in application_rust
    assert "std::hint::black_box" not in application_rust
    cache_text = Path(artifacts["cache_metadata"]).read_text(encoding="utf-8")
    assert backend["fingerprint"] in cache_text
    assert backend["resolved_rustc"] in cache_text
    debug_map = Path(artifacts["debug_map"])
    first_map = debug_map.read_bytes()
    program = subprocess.run(
        [str(executable)], text=True, stdout=subprocess.PIPE, check=True
    )
    assert program.stdout.strip() == "42"

    second_build = envelope(run(compiler, demo / "src", "build", "--json"), "build")
    assert second_build["result"]["artifacts"] == artifacts
    assert second_build["result"]["reused"] is True
    assert second_build["result"]["backend_toolchain"] == backend
    assert debug_map.read_bytes() == first_map

    configured_rustc = os.environ.get("RUSTC", "rustc")
    resolved_rustc = shutil.which(configured_rustc)
    assert resolved_rustc, "the workflow test requires the same rustc as Moss"
    rustc_wrapper = scratch / "rustc-identity-wrapper"
    write_rustc_wrapper(rustc_wrapper, Path(resolved_rustc), "identity-a")
    fake_environment = {"RUSTC": str(rustc_wrapper)}
    changed_toolchain = envelope(
        run(compiler, demo, "build", "--json", extra_env=fake_environment),
        "build",
    )
    assert changed_toolchain["result"]["reused"] is False
    assert (
        changed_toolchain["result"]["backend_toolchain"]["fingerprint"]
        != backend["fingerprint"]
    )
    same_fake_toolchain = envelope(
        run(compiler, demo, "build", "--json", extra_env=fake_environment),
        "build",
    )
    assert same_fake_toolchain["result"]["reused"] is True
    write_rustc_wrapper(rustc_wrapper, Path(resolved_rustc), "identity-b")
    changed_identity = envelope(
        run(compiler, demo, "build", "--json", extra_env=fake_environment),
        "build",
    )
    assert changed_identity["result"]["reused"] is False
    assert (
        changed_identity["result"]["backend_toolchain"]["fingerprint"]
        != same_fake_toolchain["result"]["backend_toolchain"]["fingerprint"]
    )
    assert "identity-b" in Path(
        changed_identity["result"]["artifacts"]["cache_metadata"]
    ).read_text(encoding="utf-8")

    ordinary_source = scratch / "ordinary-value-expression.moss"
    ordinary_rust = scratch / "ordinary-value-expression.rs"
    ordinary_source.write_text(
        "proc main():\n  value = 40\n  value + 2\n", encoding="utf-8"
    )
    run(
        compiler,
        demo,
        str(ordinary_source),
        "-O",
        "-o",
        str(ordinary_rust),
    )
    ordinary_text = ordinary_rust.read_text(encoding="utf-8")
    assert "(value).wrapping_add(2_i64);" in ordinary_text
    assert "std::hint::black_box" not in ordinary_text

    inspected_test = envelope(
        run(
            compiler,
            demo,
            "inspect",
            "line:11",
            "--source",
            str(demo / "src/main.moss"),
            "--json",
        ),
        "inspect",
    )
    assert inspected_test["result"]["target"]["construct_kind"] == "test"

    release = envelope(
        run(compiler, demo, "build", "--release", "--json"), "build"
    )
    assert release["result"]["profile"] == "release"
    assert "/release/" in release["result"]["artifacts"]["executable"]

    tests = envelope(run(compiler, demo, "test", "--json"), "test")
    assert tests["ok"] is True
    assert tests["result"]["summary"] == {"passed": 2, "failed": 0, "total": 2}
    test_ids = [item["id"] for item in tests["result"]["tests"]]
    assert test_ids == sorted(test_ids)
    assert "test:src/main.moss:addition" in test_ids
    assert "test:tests/arithmetic.moss:external arithmetic" in test_ids
    test_maps = list((demo / "build/test").glob("*.mossmap"))
    assert any(
        '"construct_kind": "test"' in path.read_text(encoding="utf-8")
        and "test:src/main.moss:addition" in path.read_text(encoding="utf-8")
        for path in test_maps
    )

    filtered_tests = envelope(
        run(compiler, demo, "test", "external", "--json"), "test"
    )
    assert filtered_tests["result"]["summary"]["total"] == 1
    assert filtered_tests["result"]["tests"][0]["name"] == "external arithmetic"

    failed_tests = envelope(
        run(compiler, failing, "test", "--json", expected=1), "test"
    )
    assert failed_tests["ok"] is False
    assert failed_tests["result"]["summary"] == {
        "passed": 1,
        "failed": 1,
        "total": 2,
    }
    failure = next(
        item for item in failed_tests["result"]["tests"] if item["status"] == "fail"
    )
    assert failure["diagnostic"]["code"] == "TEST_ASSERTION_FAILED"
    assert "actual=4, expected=5" in failure["diagnostic"]["message"]
    assert failure["diagnostic"]["details"] == {
        "expression": "assertEqual(2 + 2, 5)",
        "actual": "4",
        "expected": "5",
    }
    assert failure["line"] == 2
    assert failure["source_file"].endswith("src/main.moss")

    benchmarks = envelope(run(compiler, demo, "bench", "--json"), "bench")
    assert benchmarks["ok"] is True
    assert benchmarks["result"]["profile"] == "release"
    assert len(benchmarks["result"]["benchmarks"]) == 4
    for benchmark in benchmarks["result"]["benchmarks"]:
        assert benchmark["samples"] == 31
        assert benchmark["warmup"] == 5
        assert benchmark["iterations"] == 1000
        assert benchmark["p25_ns"] <= benchmark["median_ns"] <= benchmark["p75_ns"]

    arithmetic_rust = next(
        path.read_text(encoding="utf-8")
        for path in (demo / "build/bench").glob("*.rs")
        if "bench:benches/arithmetic.moss:bare arithmetic value"
        in path.read_text(encoding="utf-8")
    )
    assert (
        "std::hint::black_box(((1234_i64).wrapping_mul(7_i64)).wrapping_sub(3_i64));"
        in arithmetic_rust
    )
    assert "std::hint::black_box({\n" in arithmetic_rust
    assert "FUSED FUNCTIONAL PIPELINE" in arithmetic_rust
    assert (
        "std::hint::black_box(mixed(std::hint::black_box(1234_i64)));"
        in arithmetic_rust
    )

    filtered_bench = envelope(
        run(compiler, demo, "bench", "score", "--json"), "bench"
    )
    assert [item["name"] for item in filtered_bench["result"]["benchmarks"]] == [
        "score"
    ]
    generated_text = next(
        text
        for path in (demo / "build/bench").glob("*.rs")
        if "std::hint::black_box" in (text := path.read_text(encoding="utf-8"))
        and "bench:src/main.moss:score" in text
    )
    assert "std::hint::black_box" in generated_text
    assert "const SAMPLES: usize = 31" in generated_text

    saved = envelope(
        run(compiler, demo, "bench", "score", "--save", "phase7", "--json"),
        "bench",
    )
    assert saved["result"]["saved_baseline"] == "phase7"
    baseline_file = demo / ".moss/benchmarks/phase7.json"
    baseline = json.loads(baseline_file.read_text(encoding="utf-8"))
    assert baseline["format"] == "moss-benchmark-baseline"
    assert baseline["version"] == 2
    assert baseline["profile"] == "release"
    assert baseline["compiler_version"]
    assert baseline["platform_hex"]
    assert baseline["timestamp_utc"].endswith("Z")
    baseline_backend = baseline["backend_toolchain"]
    assert baseline_backend["fingerprint"] == saved["result"]["backend_toolchain"]["fingerprint"]
    assert baseline_backend["resolved_rustc_hex"]
    assert baseline_backend["version_verbose_hex"]
    assert baseline_backend["compile_flags_hex"]
    assert len(baseline["benchmarks"][0]["sample_values_ns"]) == 31

    compared = envelope(
        run(
            compiler,
            demo,
            "bench",
            "score",
            "--compare",
            "phase7",
            "--fail-over",
            "1000000%",
            "--json",
        ),
        "bench",
    )
    assert len(compared["result"]["comparisons"]) == 1
    assert compared["result"]["comparisons"][0]["id"] == "bench:src/main.moss:score"
    assert compared["result"]["baseline_compatible"] is True
    assert compared["result"]["regression"] is False

    incompatible = envelope(
        run(
            compiler,
            demo,
            "bench",
            "score",
            "--compare",
            "phase7",
            "--fail-over",
            "0%",
            "--json",
            extra_env=fake_environment,
        ),
        "bench",
    )
    assert incompatible["ok"] is True
    assert incompatible["result"]["baseline_compatible"] is False
    assert incompatible["result"]["comparisons"] == []
    assert incompatible["result"]["regression"] is False
    assert any(
        warning["code"] == "BASELINE_INCOMPATIBLE"
        and "backend Rust toolchain differs" in warning["message"]
        and "--fail-over enforcement were skipped" in warning["message"]
        for warning in incompatible["result"]["warnings"]
    )

    missing = envelope(
        run(compiler, demo, "test", "does-not-exist", "--json", expected=1),
        "test",
    )
    assert missing["error"]["code"] == "TEST_DISCOVERY_ERROR"
    bench_arguments = envelope(
        run(compiler, demo, "bench", "--fail-over", "5%", "--json", expected=1),
        "bench",
    )
    assert bench_arguments["error"]["code"] == "BENCHMARK_CONFIGURATION_ERROR"

    malformed = scratch / "malformed"
    malformed.mkdir()
    (malformed / "moss.toml").write_text(
        '[project]\nname = "broken"\n', encoding="utf-8"
    )
    manifest_error = envelope(
        run(compiler, malformed, "build", "--json", expected=1), "build"
    )
    assert manifest_error["error"]["code"] == "PROJECT_MANIFEST_ERROR"

    invalid_assertion = scratch / "invalid-assertion"
    (invalid_assertion / "src").mkdir(parents=True)
    (invalid_assertion / "moss.toml").write_text(
        '[project]\nname = "invalid-assertion"\nversion = "0.1.0"\n'
        '[build]\nsource = "src"\n',
        encoding="utf-8",
    )
    (invalid_assertion / "src/main.moss").write_text(
        'test "not boolean":\n  assert(1)\n\nproc main():\n  echo 1\n',
        encoding="utf-8",
    )
    assertion_error = envelope(
        run(compiler, invalid_assertion, "test", "--json", expected=1), "test"
    )
    assert assertion_error["error"]["code"] == "TEST_ASSERTION_CONFIGURATION_ERROR"
    assert assertion_error["error"]["line"] == 2

    cleaned = envelope(run(compiler, demo, "clean", "--json"), "clean")
    assert cleaned["ok"] is True
    assert not (demo / "build").exists()
    assert baseline_file.is_file(), "clean should preserve saved benchmark evidence"

    print("Phase 7 project, test, benchmark, and baseline workflows passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
