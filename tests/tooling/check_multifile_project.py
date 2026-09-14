#!/usr/bin/env python3
"""Regression coverage for the temporary multi-file Moss project unit."""

import json
import pathlib
import shutil
import subprocess
import sys
import tempfile


def run(compiler, cwd, *args, expected=0):
    result = subprocess.run(
        [str(compiler), *args], cwd=cwd, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    if result.returncode != expected:
        raise AssertionError(
            f"{args} returned {result.returncode}, expected {expected}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def envelope(result):
    return json.loads(result.stdout)


def main():
    compiler = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="moss-multifile-") as name:
        root = pathlib.Path(name)
        for directory in (root / "src", root / "tests", root / "benches"):
            directory.mkdir()
        (root / "moss.toml").write_text(
            '[project]\nname = "multifile"\nversion = "0.1.0"\n\n'
            '[build]\nsource = "src"\n', encoding="utf-8"
        )
        (root / "src" / "a_consumer.moss").write_text(
            "fn use_value() -> int:\n  provider()\n\n"
            "fn use_effect() -> int:\n  effectful_provider()\n\n"
            "proc main():\n  echo use_value()\n", encoding="utf-8"
        )
        (root / "src" / "z_provider.moss").write_text(
            "fn provider() -> int:\n  return 7\n\n"
            "fn effectful_provider() -> int:\n  echo \"provider\"\n  return 7\n",
            encoding="utf-8"
        )
        (root / "tests" / "provider_test.moss").write_text(
            'test "provider":\n  assertEqual(use_value(), 7)\n', encoding="utf-8"
        )
        (root / "benches" / "provider_bench.moss").write_text(
            'bench "provider":\n  use_value()\n', encoding="utf-8"
        )

        first = envelope(run(compiler, root, "build", "--json"))
        assert first["ok"]
        release = envelope(run(compiler, root, "build", "--release", "--json"))
        assert release["ok"] and release["result"]["profile"] == "release"
        app_rust = pathlib.Path(first["result"]["artifacts"]["generated_rust"])
        assert "__moss_test_" not in app_rust.read_text()
        assert "/src/z_provider.moss" in pathlib.Path(
            first["result"]["artifacts"]["debug_map"]
        ).read_text()

        # Semantic queries use the same src uber-module as the project build;
        # the consumer's call target is resolved from the later provider file.
        calls = envelope(run(
            compiler, root, "calls", "fn:use_value@1",
            "--source", "src/a_consumer.moss", "--json"
        ))
        assert calls["ok"]
        assert calls["result"]["direct_calls"][0]["target"] == "fn:provider"
        assert calls["result"]["direct_calls"][0]["source_file"].endswith(
            "src/a_consumer.moss"
        )
        inspect = envelope(run(
            compiler, root, "inspect", "fn:provider@1",
            "--source", "src/a_consumer.moss", "--json"
        ))
        assert inspect["ok"] and inspect["result"]["target"]["source"]["file"].endswith(
            "src/z_provider.moss"
        )
        effect_query = envelope(run(
            compiler, root, "effects", "fn:use_effect@4",
            "--source", "src/a_consumer.moss", "--json"
        ))
        assert effect_query["ok"]
        assert effect_query["result"]["observable_effects"]["external_io"] is True

        tests = envelope(run(compiler, root, "test", "--json"))
        assert tests["ok"] and tests["result"]["summary"] == {
            "passed": 1, "failed": 0, "total": 1
        }
        assert tests["result"]["tests"][0]["source_file"].endswith(
            "tests/provider_test.moss"
        )
        test_rust = next((root / "build" / "test").glob("*.rs"))
        assert "use_value" in test_rust.read_text()

        benches = envelope(run(compiler, root, "bench", "--json"))
        assert benches["ok"] and len(benches["result"]["benchmarks"]) == 1
        assert benches["result"]["benchmarks"][0]["source_file"].endswith(
            "benches/provider_bench.moss"
        )
        bench_rust = next((root / "build" / "bench").glob("*.rs"))
        assert "__moss_test_" not in bench_rust.read_text()

        test_query = envelope(run(
            compiler, root, "calls", "test:provider",
            "--source", "tests/provider_test.moss", "--json"
        ))
        assert test_query["ok"]
        assert any(edge["target"] == "fn:use_value"
                   for edge in test_query["result"]["direct_calls"])
        bench_query = envelope(run(
            compiler, root, "calls", "bench:provider",
            "--source", "benches/provider_bench.moss", "--json"
        ))
        assert bench_query["ok"]
        assert any(edge["target"] == "fn:use_value"
                   for edge in bench_query["result"]["direct_calls"])

        # Rename is transactional and project-wide within the selected src
        # context: definition and cross-file source references both change.
        renamed = envelope(run(
            compiler, root, "edit", "rename", "entity-v1:function:provider",
            "compute", "--source", "src/z_provider.moss", "--json"
        ))
        assert renamed["ok"]
        changed = {pathlib.Path(path).resolve() for path in
                   renamed["result"]["changed_files"]}
        assert changed == {(root / "src" / "a_consumer.moss").resolve(),
                           (root / "src" / "z_provider.moss").resolve()}
        assert "compute()" in (root / "src" / "a_consumer.moss").read_text()
        assert "fn compute" in (root / "src" / "z_provider.moss").read_text()
        assert envelope(run(compiler, root, "build", "--json"))["ok"]

        reused = envelope(run(compiler, root, "build", "--json"))
        assert reused["result"]["reused"] is True
        provider = root / "src" / "z_provider.moss"
        provider.write_text(
            "fn compute() -> int:\n  return 8\n\n"
            "fn effectful_provider() -> int:\n  echo \"provider\"\n  return 7\n",
            encoding="utf-8"
        )
        changed = envelope(run(compiler, root, "build", "--json"))
        assert changed["result"]["reused"] is False
        extra = root / "src" / "m_extra.moss"
        extra.write_text("fn extra() -> int:\n  return 1\n", encoding="utf-8")
        assert envelope(run(compiler, root, "build", "--json"))["result"]["reused"] is False
        extra.unlink()
        assert envelope(run(compiler, root, "build", "--json"))["result"]["reused"] is False

        (root / "src" / "duplicate.moss").write_text(
            "fn compute() -> int:\n  return 9\n", encoding="utf-8"
        )
        duplicate = envelope(run(compiler, root, "build", "--json", expected=1))
        assert duplicate["error"]["code"] == "DUPLICATE_SYMBOL"
        assert duplicate["error"]["source_file"].endswith("src/z_provider.moss")

    print("multi-file uber-module project checks passed")


if __name__ == "__main__":
    main()
