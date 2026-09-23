#!/usr/bin/env python3
"""Phase 15.7: Multi-Module Formatter Semantic Convergence and Transactional Writes regression test."""
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
COMPILER = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "moss").resolve()


def run(args, check=True, **kwargs):
    completed = subprocess.run(args, text=True, capture_output=True, **kwargs)
    if check:
        assert completed.returncode == 0, (args, completed.stdout, completed.stderr)
    return completed


def test_selected_file_and_write_scope(temp_dir):
    project_dir = temp_dir / "pkg_selected"
    src_dir = project_dir / "src"
    src_dir.mkdir(parents=True, exist_ok=True)

    (project_dir / "Moss.toml").write_text(
        '[project]\nname = "pkg_selected"\nversion = "0.1.0"\n[build]\nsource = "src"\n'
    )

    model_src = """module model

export type Item:
  value:   Int
"""
    service_src = """module service
import model

export fn get_value(x: model.Item) -> Int:
  var v =   x.value
  return v
"""
    model_file = src_dir / "model.moss"
    service_file = src_dir / "service.moss"
    model_file.write_text(model_src)
    service_file.write_text(service_src)

    model_bytes_before = model_file.read_bytes()
    service_bytes_before = service_file.read_bytes()

    # 1. moss fmt service.moss --check should not fail with unknown parameter type 'model.Item'
    res = run([str(COMPILER), "fmt", str(service_file), "--check"], check=False)
    assert res.returncode == 1, f"Expected drift exit code 1, got {res.returncode}; stderr: {res.stderr}"
    assert "unknown parameter type" not in res.stderr
    assert "FORMAT_PARSE_ERROR" not in res.stderr
    # No files should be modified on disk
    assert model_file.read_bytes() == model_bytes_before
    assert service_file.read_bytes() == service_bytes_before

    # 2. moss fmt service.moss (without --check) should succeed and modify ONLY service.moss
    res = run([str(COMPILER), "fmt", str(service_file)])
    assert res.returncode == 0, f"fmt failed: {res.stderr}"

    # Verify write scope: model.moss MUST be byte-for-byte identical
    assert model_file.read_bytes() == model_bytes_before, "Sibling model.moss was modified!"
    # service.moss should be canonically formatted
    service_after = service_file.read_text()
    assert "var v = x.value" in service_after, f"Expected formatted assignment, got:\n{service_after}"
    assert "var v =   x.value" not in service_after

    # 3. Repeat check on service.moss should now pass with exit 0
    res = run([str(COMPILER), "fmt", str(service_file), "--check"])
    assert res.returncode == 0, f"Check failed after format: {res.stderr}"


def test_whole_project_and_check(temp_dir):
    project_dir = temp_dir / "pkg_whole"
    src_dir = project_dir / "src"
    src_dir.mkdir(parents=True, exist_ok=True)

    (project_dir / "Moss.toml").write_text(
        '[project]\nname = "pkg_whole"\nversion = "0.1.0"\n[build]\nsource = "src"\n'
    )

    (src_dir / "model.moss").write_text("""module model

export type Item:
  value:   Int
""")
    (src_dir / "service.moss").write_text("""module service
import model

export fn wrap(v: Int) -> model.Item:
  return model.Item(value:   v)
""")

    # 1. Project-wide check should detect drift without modifying files
    res = run([str(COMPILER), "fmt", str(project_dir), "--check"], check=False)
    assert res.returncode == 1, f"Expected drift exit code 1, got {res.returncode}"

    # 2. Format whole project
    res = run([str(COMPILER), "fmt", str(project_dir)])
    assert res.returncode == 0, f"fmt failed: {res.stderr}"

    # Both files must now be canonically formatted
    assert "value: Int" in (src_dir / "model.moss").read_text()
    assert "value: v" in (src_dir / "service.moss").read_text()

    # 3. Project-wide format should be idempotent
    model_formatted = (src_dir / "model.moss").read_bytes()
    service_formatted = (src_dir / "service.moss").read_bytes()
    res = run([str(COMPILER), "fmt", str(project_dir)])
    assert res.returncode == 0
    assert (src_dir / "model.moss").read_bytes() == model_formatted
    assert (src_dir / "service.moss").read_bytes() == service_formatted

    # 4. Project-wide --check should now exit 0
    res = run([str(COMPILER), "fmt", str(project_dir), "--check"])
    assert res.returncode == 0, f"Check failed after formatting: {res.stderr}"

    # 5. Compiler check should pass
    res = run([str(COMPILER), "check", str(src_dir / "service.moss"), "--json"])
    assert res.returncode == 0


def test_transactional_failure(temp_dir):
    project_dir = temp_dir / "pkg_transactional"
    src_dir = project_dir / "src"
    src_dir.mkdir(parents=True, exist_ok=True)

    (project_dir / "Moss.toml").write_text(
        '[project]\nname = "pkg_transactional"\nversion = "0.1.0"\n[build]\nsource = "src"\n'
    )

    # Valid file needing formatting
    model_src = """module model

export type Config:
  retries:   Int
"""
    # Invalid file with semantic error (unknown import / unknown type)
    broken_src = """module broken
import nonexistent_module

export fn run():
  return   1
"""
    model_file = src_dir / "model.moss"
    broken_file = src_dir / "broken.moss"
    model_file.write_text(model_src)
    broken_file.write_text(broken_src)

    model_bytes_before = model_file.read_bytes()
    broken_bytes_before = broken_file.read_bytes()

    # Format whole project - must fail because project has semantic error
    res = run([str(COMPILER), "fmt", str(project_dir)], check=False)
    assert res.returncode != 0, "Expected formatting to fail on semantically invalid project"

    # Transactional invariant: NO files must be modified on disk!
    assert model_file.read_bytes() == model_bytes_before, "Transaction failed: model.moss was modified on disk!"
    assert broken_file.read_bytes() == broken_bytes_before, "Transaction failed: broken.moss was modified on disk!"

    # Also test selected-file transactional failure
    invalid_file = src_dir / "type_error.moss"
    invalid_src = """module type_error

fn bad() -> Int:
  x =   "string value"
  return x
"""
    invalid_file.write_text(invalid_src)
    invalid_bytes_before = invalid_file.read_bytes()

    res = run([str(COMPILER), "fmt", str(invalid_file)], check=False)
    assert res.returncode != 0, "Expected formatting to fail on file with type error"
    assert invalid_file.read_bytes() == invalid_bytes_before, "Transaction failed: type_error.moss was modified on disk!"


with tempfile.TemporaryDirectory(dir=ROOT / "tmp") as directory:
    d = Path(directory)
    test_selected_file_and_write_scope(d)
    test_whole_project_and_check(d)
    test_transactional_failure(d)

print("Phase 15.7 multi-module formatter convergence regressions passed")
