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
    # Part 1: Whole-project transactional failure
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

    # Part 2: Isolated selected-file transactional failure in a clean project
    # Proves:
    # 1. the selected file itself needs formatting;
    # 2. the project context is otherwise valid;
    # 3. the selected file contains a semantic/type error;
    # 4. moss fmt selected_file.moss fails;
    # 5. the selected file remains byte-for-byte unchanged;
    # 6. valid sibling files remain byte-for-byte unchanged.
    sel_project_dir = temp_dir / "pkg_selected_transactional"
    sel_src_dir = sel_project_dir / "src"
    sel_src_dir.mkdir(parents=True, exist_ok=True)

    (sel_project_dir / "Moss.toml").write_text(
        '[project]\nname = "pkg_selected_transactional"\nversion = "0.1.0"\n[build]\nsource = "src"\n'
    )

    # Sibling file: valid Moss, unformatted spacing
    sibling_src = """module sibling

export fn valid_helper(x: Int) -> Int:
  return   x + 1
"""
    sibling_file = sel_src_dir / "sibling.moss"
    sibling_file.write_text(sibling_src)
    sibling_bytes_before = sibling_file.read_bytes()

    # Sibling by itself checks cleanly (proof point 2: project context is otherwise valid)
    sibling_check = run([str(COMPILER), "check", str(sibling_file), "--json"])
    assert sibling_check.returncode == 0, f"Expected sibling to check cleanly: {sibling_check.stderr}"

    # Selected file: needs formatting (x =   "string value") AND has a type error (returns String but annotated Int)
    selected_src = """module bad_selection

export fn broken_function() -> Int:
  x =   "string value"
  return x
"""
    selected_file = sel_src_dir / "bad_selection.moss"
    selected_file.write_text(selected_src)
    selected_bytes_before = selected_file.read_bytes()

    # Proof point 1: selected file itself needs formatting (contains uncanonical spacing)
    assert "x =   \"string value\"" in selected_src

    # Proof points 3 & 4: selected file has type error and moss fmt selected_file.moss fails
    res_sel = run([str(COMPILER), "fmt", str(selected_file)], check=False)
    assert res_sel.returncode != 0, "Expected formatting to fail on file with type error"
    assert "returns 'string' but is annotated 'int'" in res_sel.stderr or "MOSS_COMPILE_ERROR" in res_sel.stderr

    # Proof point 5: selected file remains byte-for-byte unchanged
    assert selected_file.read_bytes() == selected_bytes_before, (
        "Transaction failed: selected file was modified on disk despite validation failure!"
    )

    # Proof point 6: valid sibling files remain byte-for-byte unchanged
    assert sibling_file.read_bytes() == sibling_bytes_before, (
        "Transaction failed: valid sibling file was modified on disk!"
    )


def test_formatter_bypasses_codegen(temp_dir):
    """Prove that formatter semantic validation stops before optimizer/codegen."""
    import os
    project_dir = temp_dir / "pkg_codegen_boundary"
    src_dir = project_dir / "src"
    src_dir.mkdir(parents=True, exist_ok=True)

    (project_dir / "Moss.toml").write_text(
        '[project]\nname = "pkg_codegen_boundary"\nversion = "0.1.0"\n[build]\nsource = "src"\n'
    )

    app_src = """module app

domain Counter:
  total = 0

  fn Increment() -> Int:
    total = total +   1
    reply total

fn main():
  c = Counter()
  v = message c.Increment()
  echo v
"""
    app_file = src_dir / "main.moss"
    app_file.write_text(app_src)

    prof_env = dict(os.environ, MOSS_PROFILE_COMPILER="1")

    # 1. Whole-project formatting:
    # Must invoke Checker semantic analysis (effects / synchronization_plan),
    # but must NOT invoke FunctionalOptimizer or backend Generator.
    res_fmt = run([str(COMPILER), "fmt", str(project_dir)], env=prof_env)
    assert res_fmt.returncode == 0, f"fmt failed: {res_fmt.stderr}"

    assert "MOSS_PROFILE|synchronization_plan|" in res_fmt.stderr or "MOSS_PROFILE|effects|" in res_fmt.stderr, (
        f"Expected semantic stage profile in stderr, got:\n{res_fmt.stderr}"
    )
    assert "MOSS_PROFILE|rust_generation|" not in res_fmt.stderr, (
        f"Backend Generator was invoked during moss fmt!\n{res_fmt.stderr}"
    )
    assert "MOSS_PROFILE|functional_optimizer|" not in res_fmt.stderr, (
        f"FunctionalOptimizer was invoked during moss fmt!\n{res_fmt.stderr}"
    )

    # 2. Selected-file formatting within project context:
    # Also must stop after semantic checking and bypass codegen.
    app_file.write_text(app_src)
    res_sel_fmt = run([str(COMPILER), "fmt", str(app_file)], env=prof_env)
    assert res_sel_fmt.returncode == 0, f"selected fmt failed: {res_sel_fmt.stderr}"
    assert "MOSS_PROFILE|rust_generation|" not in res_sel_fmt.stderr, (
        f"Backend Generator was invoked during selected-file moss fmt!\n{res_sel_fmt.stderr}"
    )
    assert "MOSS_PROFILE|functional_optimizer|" not in res_sel_fmt.stderr, (
        f"FunctionalOptimizer was invoked during selected-file moss fmt!\n{res_sel_fmt.stderr}"
    )

    # 3. Contrast with compile command: compilation DOES invoke rust_generation
    out_rs = temp_dir / "out.rs"
    res_compile = run([str(COMPILER), "-Oshared-memory", str(app_file), "-o", str(out_rs)], env=prof_env)
    assert res_compile.returncode == 0, f"Compilation failed: {res_compile.stderr}"
    assert "MOSS_PROFILE|rust_generation|" in res_compile.stderr, (
        f"Expected rust_generation during compile, got:\n{res_compile.stderr}"
    )


with tempfile.TemporaryDirectory(dir=ROOT / "tmp") as directory:
    d = Path(directory)
    test_selected_file_and_write_scope(d)
    test_whole_project_and_check(d)
    test_transactional_failure(d)
    test_formatter_bypasses_codegen(d)

print("Phase 15.7 multi-module formatter convergence regressions passed")
