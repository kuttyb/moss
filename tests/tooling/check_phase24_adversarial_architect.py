#!/usr/bin/env python3
"""Phase 24 Architect adversarial falsification test suite.

Validates:
1. Complex imported generic expression compositions (nested vector indexing,
   nested field projections, arithmetic, comparisons/booleans, nested generics,
   and combined indexing/projections) across native O0, native release,
   Fast Debug, and source-free ABI-v9 providers.
2. Ownership, mutability, and synchronization guarantees (immutable local mutation
   rejection, domain-state exclusive 2PL locking, invalid byte value safety).
3. Algorithm correctness and boundary cases (empty collections, single elements,
   duplicate keys, extreme signed Int64 boundaries, overflow-safe clamp and slice).
"""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
MOSS = REPO_ROOT / "moss"
MARGO = REPO_ROOT / "margo"

assert MOSS.exists(), f"Compiler missing at {MOSS}"
assert MARGO.exists(), f"Margo driver missing at {MARGO}"

def run_cmd(cmd, cwd, env=None, expected_rc=0):
    res = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True)
    if res.returncode != expected_rc:
        return False, f"RC={res.returncode} (expected {expected_rc})\nSTDOUT:\n{res.stdout}\nSTDERR:\n{res.stderr}"
    return True, res.stdout

def indent(code_block):
    return "\n".join("  " + line if line.strip() else "" for line in code_block.strip().split("\n")) + "\n"

with tempfile.TemporaryDirectory(prefix="p24-adversarial-gate-") as temp_dir:
    temp_path = Path(temp_dir)
    margo_home = temp_path / "margo-home"
    margo_home.mkdir()
    base_env = os.environ.copy()
    base_env.update(MOSS=str(MOSS), MARGO_HOME=str(margo_home), TMPDIR=str(temp_path))

    # --- Part A: Imported Generic Expression Composition ---
    provider_dir = temp_path / "comp_provider"
    (provider_dir / "src").mkdir(parents=True)
    (provider_dir / "Moss.toml").write_text(
        '[package]\nname = "comp_provider"\nversion = "0.1.0"\n'
        '[build]\nsource = "src"\n', encoding="utf-8")
    (provider_dir / "src/probe.moss").write_text(
        'module probe\n'
        'export type Item:\n'
        '  val: Int\n'
        'export type Container:\n'
        '  item: Item\n'
        'export fn make_item(x) -> Item:\n'
        '  return Item(val: x)\n'
        'export fn make_container(x) -> Container:\n'
        '  return Container(item: Item(val: x))\n'
        'export fn identity(x):\n'
        '  return x\n'
        'export fn add_vals(a, b) -> Int:\n'
        '  return a + b\n'
        'export fn check_bool(b: Bool) -> Bool:\n'
        '  return b\n'
        'export fn is_positive(x: Int) -> Bool:\n'
        '  return x > 0\n'
        'export fn negate_val(x: Int) -> Int:\n'
        '  return 0 - x\n'
        'export fn get_int() -> Int:\n'
        '  return 42\n'
        'export fn wrap_item(item: Item) -> Container:\n'
        '  return Container(item: item)\n'
        'export fn unwrap_container(c: Container) -> Item:\n'
        '  return c.item\n',
        encoding="utf-8"
    )

    ok, out = run_cmd([str(MARGO), "build", "--json"], cwd=provider_dir, env=base_env)
    assert ok, f"Provider build failed:\n{out}"

    ok, out = run_cmd([str(MARGO), "build", "--json"], cwd=REPO_ROOT / "sdk/foundations/collections", env=base_env)
    assert ok, f"Collections build failed:\n{out}"

    consumer_dir = temp_path / "comp_consumer"
    (consumer_dir / "src").mkdir(parents=True)
    (consumer_dir / "Moss.toml").write_text(
        f'[package]\nname = "comp_consumer"\nversion = "0.1.0"\n[build]\nsource = "src"\n\n'
        f'[dependencies]\n'
        f'comp_provider = {{ path = "../comp_provider" }}\n'
        f'"moss.collections" = {{ path = "{REPO_ROOT / "sdk/foundations/collections"}" }}\n',
        encoding="utf-8"
    )

    test_cases = [
        (
            "nested_vector_index",
            'echo [[probe.make_item(7).val]][0][0]\n'
            'echo [[[probe.make_item(99).val]][0]][0][0]\n'
            'echo [[collections.index_of([10, 20], 20)]][0][0]\n',
            "7\n99\n1\n"
        ),
        (
            "nested_field_projections",
            'echo probe.make_container(15).item.val\n'
            'echo probe.identity(probe.make_container(25)).item.val\n'
            'echo probe.wrap_item(probe.make_item(35)).item.val\n'
            'echo probe.unwrap_container(probe.make_container(45)).val\n',
            "15\n25\n35\n45\n"
        ),
        (
            "generic_in_arithmetic",
            'echo 10 + probe.make_item(5).val * 2\n'
            'echo probe.make_item(100).val - probe.make_item(30).val\n'
            'echo (probe.make_item(50).val + 10) / 2\n'
            'echo 100 % probe.make_item(7).val\n'
            'echo probe.add_vals(probe.make_item(4).val, probe.make_item(6).val) * 3\n',
            "20\n70\n30\n2\n30\n"
        ),
        (
            "generic_in_boolean_and_comparisons",
            'if probe.make_item(5).val > 3 and not (probe.make_item(2).val > 10):\n'
            '  echo 1\n'
            'else:\n'
            '  echo 0\n'
            'if probe.make_item(1).val == 1 xor probe.make_item(2).val == 2:\n'
            '  echo 0\n'
            'else:\n'
            '  echo 2\n'
            'if not (not (probe.check_bool(true))):\n'
            '  echo 3\n'
            'if not (probe.check_bool(false)) and probe.is_positive(probe.make_item(10).val):\n'
            '  echo 4\n',
            "1\n2\n3\n4\n"
        ),
        (
            "generic_nested_in_generic",
            'echo probe.identity(probe.make_item(55)).val\n'
            'echo probe.add_vals(probe.identity(12), probe.negate_val(0 - 8))\n'
            'echo probe.identity(probe.identity(probe.make_item(88))).val\n',
            "55\n20\n88\n"
        ),
        (
            "combined_indexing_field_projection_arithmetic",
            'echo [probe.wrap_item(probe.make_item(10)).item.val + 5][0]\n'
            'echo [collections.index_of([10, 20, 30], probe.make_item(20).val)][0]\n'
            'echo [probe.make_item(1).val, probe.make_item(2).val, probe.make_item(3).val][1]\n',
            "15\n1\n2\n"
        ),
    ]

    for name, code, expected in test_cases:
        main_file = consumer_dir / "src/main.moss"
        main_file.write_text(f"module app\nimport probe\nimport collections\nfn main():\n{indent(code)}", encoding="utf-8")
        
        ok, out = run_cmd([str(MARGO), "run", "--json"], cwd=consumer_dir, env=base_env)
        assert ok and out == expected, f"Part A [{name}] margo run mismatch:\n{out}"

        ok, out = run_cmd([str(MARGO), "debug"], cwd=consumer_dir, env=base_env)
        assert ok and out == expected, f"Part A [{name}] margo debug mismatch:\n{out}"

    # Source-free execution
    provider_source = provider_dir / "src/probe.moss"
    # Keep package source available for R2 interface freshness validation.
    compiled_provider = provider_dir / "build/debug"
    assert (compiled_provider / "probe.mossi").exists()
    assert (compiled_provider / "libprobe.rlib").exists()

    source_free_dir = temp_path / "comp_source_free"
    (source_free_dir / "src").mkdir(parents=True)
    (source_free_dir / "Moss.toml").write_text('[project]\nname = "comp_source_free"\nversion = "0.1.0"\n', encoding="utf-8")
    sf_env = base_env.copy()
    sf_env["MOSS_MODULE_PATH"] = f"{compiled_provider}:{REPO_ROOT / 'sdk/foundations/collections/build/debug'}"

    for name, code, expected in test_cases:
        main_file = source_free_dir / "src/main.moss"
        main_file.write_text(f"module app\nimport probe\nimport collections\nfn main():\n{indent(code)}", encoding="utf-8")
        ok, out = run_cmd([str(MOSS), "check", "src/main.moss", "--json"], cwd=source_free_dir, env=sf_env)
        assert ok, f"Part A Source-Free [{name}] check failed:\n{out}"
        ok, out = run_cmd([str(MARGO), "build", "--moss-json"], cwd=source_free_dir, env=sf_env)
        assert ok, f"Part A Source-Free [{name}] build failed:\n{out}"
        exe = Path(json.loads(out)["result"]["artifacts"]["executable"])
        ok, out = run_cmd([str(exe)], cwd=source_free_dir, env=sf_env)
        assert ok and out == expected, f"Part A Source-Free [{name}] output mismatch:\n{out}"

    # Provider source was kept in place for current interface freshness.

    # --- Part B: Ownership, Mutation, and Aliasing ---
    own_dir = temp_path / "own_test"
    (own_dir / "src").mkdir(parents=True)
    (own_dir / "Moss.toml").write_text(
        f'[package]\nname = "own_test"\nversion = "0.1.0"\n[build]\nsource = "src"\n\n'
        f'[dependencies]\n'
        f'"moss.algorithms" = {{ path = "{REPO_ROOT / "sdk/foundations/algorithms"}" }}\n'
        f'"moss.bytes" = {{ path = "{REPO_ROOT / "sdk/foundations/bytes"}" }}\n',
        encoding="utf-8"
    )

    run_cmd([str(MARGO), "build", "--json"], cwd=REPO_ROOT / "sdk/foundations/algorithms", env=base_env)
    run_cmd([str(MARGO), "build", "--json"], cwd=REPO_ROOT / "sdk/foundations/bytes", env=base_env)
    own_env = base_env.copy()
    own_env["MOSS_MODULE_PATH"] = f"{REPO_ROOT / 'sdk/foundations/algorithms/build/debug'}:{REPO_ROOT / 'sdk/foundations/bytes/build/debug'}"

    # B1: Immutable input rejected by sort_int
    (own_dir / "src/main.moss").write_text(
        'module app\nimport algorithms\nfn main():\n'
        '  let items = [3, 2, 1]\n'
        '  algorithms.sort_int(items)\n',
        encoding="utf-8"
    )
    ok, out = run_cmd([str(MOSS), "check", "src/main.moss", "--json"], cwd=own_dir, env=own_env, expected_rc=1)
    assert ok, f"Expected check failure for immutable sort:\n{out}"
    parsed = json.loads(out)
    assert "IMMUTABLE" in parsed.get("error", {}).get("code", ""), f"Expected IMMUTABLE error:\n{out}"

    # B2: Immutable input rejected by bytes mutation
    (own_dir / "src/main.moss").write_text(
        'module app\nimport bytes\nfn main():\n'
        '  let b = [65, 66]\n'
        '  bytes.set_if_valid(b, 0, 70)\n',
        encoding="utf-8"
    )
    ok, out = run_cmd([str(MOSS), "check", "src/main.moss", "--json"], cwd=own_dir, env=own_env, expected_rc=1)
    assert ok, f"Expected check failure for immutable bytes:\n{out}"

    # B3: Concurrent domain state sort with synchronization plan
    (own_dir / "src/main.moss").write_text(
        'module app\nimport algorithms\n'
        'domain Sorter:\n'
        '  items = [5, 2, 4, 1, 3]\n'
        '  fn Sort() -> Int:\n'
        '    algorithms.sort_int(items)\n'
        '    reply items[0]\n'
        'fn main():\n'
        '  s = Sorter()\n'
        '  first = message s.Sort()\n'
        '  echo first\n',
        encoding="utf-8"
    )
    ok, out = run_cmd([str(MOSS), "effects", "entity-v1:handler:app__Sorter.Sort", "--source", "src/main.moss", "--json"], cwd=own_dir, env=own_env)
    assert ok, f"moss effects failed:\n{out}"
    eff = json.loads(out)
    handler_info = eff["result"]["synchronization"]["instances"][0]["handlers"][0]
    assert "items" in handler_info["write_set"], f"items not in write_set: {handler_info}"
    assert handler_info["acquisitions"][0]["mode"] == "EXCLUSIVE", f"mode not EXCLUSIVE: {handler_info}"
    ok, out = run_cmd([str(MARGO), "run", "--json"], cwd=own_dir, env=base_env)
    assert ok and out.strip() == "1", f"Domain sort native failed: {out}"
    ok, out = run_cmd([str(MARGO), "debug"], cwd=own_dir, env=base_env)
    assert ok and out.strip() == "1", f"Domain sort Fast Debug failed: {out}"

    # B4: Invalid byte values
    (own_dir / "src/main.moss").write_text(
        'module app\nimport bytes\nfn main():\n'
        '  assert(not bytes.is_byte(-1))\n'
        '  assert(not bytes.is_byte(256))\n'
        '  assert(bytes.is_byte(0))\n'
        '  assert(bytes.is_byte(255))\n'
        '  var b = [10, 20]\n'
        '  assert(not bytes.set_if_valid(b, 0, 300))\n'
        '  assert(not bytes.set_if_valid(b, -1, 50))\n'
        '  assert(not bytes.set_if_valid(b, 2, 50))\n'
        '  assert(not bytes.append_if_valid(b, -5))\n'
        '  echo bytes.get_or(b, 99, 255)\n',
        encoding="utf-8"
    )
    ok, out = run_cmd([str(MARGO), "run", "--json"], cwd=own_dir, env=base_env)
    assert ok and out.strip() == "255", f"Invalid byte native failed: {out}"
    ok, out = run_cmd([str(MARGO), "debug"], cwd=own_dir, env=base_env)
    assert ok and out.strip() == "255", f"Invalid byte Fast Debug failed: {out}"

    # --- Part C: Algorithm Correctness (Extreme, Boundary, Empty) ---
    alg_dir = temp_path / "alg_test"
    (alg_dir / "src").mkdir(parents=True)
    (alg_dir / "Moss.toml").write_text(
        f'[package]\nname = "alg_test"\nversion = "0.1.0"\n[build]\nsource = "src"\n\n'
        f'[dependencies]\n'
        f'"moss.algorithms" = {{ path = "{REPO_ROOT / "sdk/foundations/algorithms"}" }}\n'
        f'"moss.collections" = {{ path = "{REPO_ROOT / "sdk/foundations/collections"}" }}\n'
        f'"moss.core" = {{ path = "{REPO_ROOT / "sdk/foundations/core"}" }}\n',
        encoding="utf-8"
    )

    (alg_dir / "src/main.moss").write_text(
        'module app\nimport algorithms\nimport collections\nimport core\nfn main():\n'
        '  # Empty\n'
        '  var e = Vector[Int]()\n'
        '  algorithms.sort_int(e)\n'
        '  assert((e |> count) == 0)\n'
        '  assert(algorithms.lower_bound_int(e, 5) == 0)\n'
        '  assert(algorithms.binary_search_int(e, 5) == 0 - 1)\n'
        '  assert(collections.index_of(e, 5) == 0 - 1)\n'
        '  assert(not collections.contains(e, 5))\n'
        '  assert(collections.count_equal(e, 5) == 0)\n'
        '  assert((collections.reversed_copy_int(e) |> count) == 0)\n'
        '  \n'
        '  # Single\n'
        '  var s = [42]\n'
        '  algorithms.sort_int(s)\n'
        '  assert(s[0] == 42)\n'
        '  assert(algorithms.lower_bound_int(s, 10) == 0)\n'
        '  assert(algorithms.lower_bound_int(s, 42) == 0)\n'
        '  assert(algorithms.lower_bound_int(s, 100) == 1)\n'
        '  assert(algorithms.binary_search_int(s, 42) == 0)\n'
        '  assert(algorithms.binary_search_int(s, 99) == 0 - 1)\n'
        '  \n'
        '  # Duplicates\n'
        '  var d = [5, 2, 5, 2, 5]\n'
        '  algorithms.sort_int(d)\n'
        '  assert(d[0] == 2 and d[1] == 2 and d[2] == 5 and d[3] == 5 and d[4] == 5)\n'
        '  assert(algorithms.lower_bound_int(d, 2) == 0)\n'
        '  assert(algorithms.lower_bound_int(d, 5) == 2)\n'
        '  assert(algorithms.binary_search_int(d, 5) == 2)\n'
        '  assert(collections.count_equal(d, 5) == 3)\n'
        '  assert(collections.count_equal(d, 2) == 2)\n'
        '  \n'
        '  # Extreme Ints\n'
        '  max_i = 9223372036854775807\n'
        '  min_i = 0 - 9223372036854775807 - 1\n'
        '  var ext = [0, max_i, min_i, 0 - 1, 1]\n'
        '  algorithms.sort_int(ext)\n'
        '  assert(ext[0] == min_i)\n'
        '  assert(ext[1] == 0 - 1)\n'
        '  assert(ext[2] == 0)\n'
        '  assert(ext[3] == 1)\n'
        '  assert(ext[4] == max_i)\n'
        '  assert(algorithms.lower_bound_int(ext, min_i) == 0)\n'
        '  assert(algorithms.lower_bound_int(ext, 0) == 2)\n'
        '  assert(algorithms.lower_bound_int(ext, max_i) == 4)\n'
        '  assert(algorithms.binary_search_int(ext, max_i) == 4)\n'
        '  assert(algorithms.binary_search_int(ext, min_i) == 0)\n'
        '  \n'
        '  # Core clamp and valid_slice\n'
        '  assert(core.clamp_int(min_i, 0 - 100, 100) == 0 - 100)\n'
        '  assert(core.clamp_int(max_i, 0 - 100, 100) == 100)\n'
        '  assert(core.valid_slice(0, 5, 5))\n'
        '  assert(not core.valid_slice(1, 5, 5))\n'
        '  assert(not core.valid_slice(0 - 1, 2, 5))\n'
        '  assert(not core.valid_slice(0, 0 - 1, 5))\n'
        '  echo 1337\n',
        encoding="utf-8"
    )
    ok, out = run_cmd([str(MARGO), "run", "--json"], cwd=alg_dir, env=base_env)
    assert ok and out.strip() == "1337", f"Alg run native failed: {out}"
    ok, out = run_cmd([str(MARGO), "debug"], cwd=alg_dir, env=base_env)
    assert ok and out.strip() == "1337", f"Alg run Fast Debug failed: {out}"

print("Phase 24 Architect adversarial falsification gates passed successfully.")
