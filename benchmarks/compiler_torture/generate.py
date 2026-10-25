#!/usr/bin/env python3
"""Deterministic Portable Moss Torture Benchmark Suite Generator (Phase 25.4 B1).

This generator produces standard, portable Moss source files and projects.
The generated suite has NO dependencies on C++ compiler internals and can be
run unchanged against the Java Moss compiler (Phase 25.3).
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

GENERATOR_VERSION = "1.0.0"
GENERATOR_SEED = 20261025

ROOT_DIR = Path(__file__).resolve().parent
FIXTURES_DIR = ROOT_DIR / "fixtures"
EXPECTED_DIR = ROOT_DIR / "expected"
REPO_ROOT = ROOT_DIR.parent.parent


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


# ---------------------------------------------------------------------------
# Workload Generators
# ---------------------------------------------------------------------------

def gen_w2_specialization(n: int) -> str:
    """W2: Concrete specialization stress with distinct pairs (N concrete types, N^2 distinct calls)."""
    lines = [
        "# Generated Portable Moss Benchmark: W2 Concrete Specialization (Distinct)",
        f"# Generator: {GENERATOR_VERSION}, N={n}, Distinct Call Sites={n*n}",
        ""
    ]
    for i in range(n):
        lines.extend([
            f"type Sensor{i:03d}:",
            "  value: Int",
            "",
            "  fn score() -> Int:",
            f"    return value + {i + 1}",
            ""
        ])
    lines.extend([
        "fn project(item):",
        "  return item.score()",
        "",
        "fn combine(a, b):",
        "  return project(a) + project(b)",
        "",
        "fn main():",
        "  var total = 0"
    ])
    for i in range(n):
        for j in range(n):
            lines.append(f"  total = total + combine(Sensor{i:03d}(value: 1), Sensor{j:03d}(value: 2))")
    lines.extend([
        "  echo total",
        ""
    ])
    return "\n".join(lines)


def gen_w3_specialization(n: int) -> str:
    """W3: Concrete specialization control with repeated pairs (same N and N^2 calls, but only 4 distinct signatures)."""
    lines = [
        "# Generated Portable Moss Benchmark: W3 Concrete Specialization (Repeated)",
        f"# Generator: {GENERATOR_VERSION}, N={n}, Total Call Sites={n*n}, Distinct Signatures=4",
        ""
    ]
    for i in range(n):
        lines.extend([
            f"type Sensor{i:03d}:",
            "  value: Int",
            "",
            "  fn score() -> Int:",
            f"    return value + {i + 1}",
            ""
        ])
    lines.extend([
        "fn project(item):",
        "  return item.score()",
        "",
        "fn combine(a, b):",
        "  return project(a) + project(b)",
        "",
        "fn main():",
        "  var total = 0"
    ])
    for i in range(n):
        for j in range(n):
            a, b = i % 2, j % 2
            lines.append(f"  total = total + combine(Sensor{a:03d}(value: 1), Sensor{b:03d}(value: 2))")
    lines.extend([
        "  echo total",
        ""
    ])
    return "\n".join(lines)


def gen_w4_inference_deep(depth: int) -> str:
    """W4: Long chains of dependent untyped type inference."""
    lines = [
        "# Generated Portable Moss Benchmark: W4 Deep Dependent Inference",
        f"# Generator: {GENERATOR_VERSION}, Depth={depth}",
        "",
        "fn step000(x):",
        "  return x + 1",
        ""
    ]
    for i in range(1, depth + 1):
        lines.extend([
            f"fn step{i:03d}(x):",
            f"  return step{i-1:03d}(x) + 1",
            ""
        ])
    lines.extend([
        "fn main():",
        f"  echo step{depth:03d}(10)",
        ""
    ])
    return "\n".join(lines)


def gen_w5_inference_wide(width: int) -> str:
    """W5: Many independent inference chains of similar total complexity."""
    lines = [
        "# Generated Portable Moss Benchmark: W5 Wide Independent Inference",
        f"# Generator: {GENERATOR_VERSION}, Width={width}",
        ""
    ]
    for i in range(width):
        lines.extend([
            f"fn wide_{i:03d}_a(x):",
            f"  return x + {i + 1}",
            "",
            f"fn wide_{i:03d}_b(x):",
            f"  return wide_{i:03d}_a(x) + 1",
            ""
        ])
    lines.extend([
        "fn main():",
        "  var total = 0"
    ])
    for i in range(width):
        lines.append(f"  total = total + wide_{i:03d}_b(1)")
    lines.extend([
        "  echo total",
        ""
    ])
    return "\n".join(lines)


def gen_w6_types(count: int) -> str:
    """W6: Large numbers of distinct value types with fields and methods."""
    lines = [
        "# Generated Portable Moss Benchmark: W6 Types Count Stress",
        f"# Generator: {GENERATOR_VERSION}, Type Count={count}",
        ""
    ]
    for i in range(count):
        lines.extend([
            f"type Entity{i:03d}:",
            "  id: Int",
            "  weight: Int",
            "",
            "  fn score() -> Int:",
            "    return id * weight",
            ""
        ])
    lines.extend([
        "fn main():",
        "  var total = 0"
    ])
    for i in range(count):
        lines.append(f"  let e{i:03d} = Entity{i:03d}(id: {i + 1}, weight: 2)")
        lines.append(f"  total = total + e{i:03d}.score()")
    lines.extend([
        "  echo total",
        ""
    ])
    return "\n".join(lines)


def gen_w7_collections(nesting_depth: int) -> str:
    """W7: Nested collection constructions with explicit nesting depth."""
    lines = [
        "# Generated Portable Moss Benchmark: W7 Nested Collections",
        f"# Generator: {GENERATOR_VERSION}, Nesting Depth={nesting_depth}",
        "",
        "fn main():",
        "  var v_1 = [1, 2, 3]"
    ]
    for i in range(2, nesting_depth + 1):
        lines.append(f"  var v_{i} = [v_{i-1}]")
    if nesting_depth == 1:
        lines.append("  echo v_1[0]")
    else:
        lines.append(f"  var cur_{nesting_depth - 1} = v_{nesting_depth}[0]")
        for i in range(nesting_depth - 1, 1, -1):
            lines.append(f"  var cur_{i - 1} = cur_{i}[0]")
        lines.append("  echo cur_1[0]")
    lines.append("")
    return "\n".join(lines)


def gen_w8_traits(type_count: int) -> str:
    """W8: Trait and method resolution across many concrete types."""
    lines = [
        "# Generated Portable Moss Benchmark: W8 Static Method Resolution",
        f"# Generator: {GENERATOR_VERSION}, Concrete Types={type_count}",
        ""
    ]
    for i in range(type_count):
        lines.extend([
            f"type Worker{i:03d}:",
            "  code: Int",
            "",
            "  fn execute() -> Int:",
            f"    return code + {i + 1}",
            ""
        ])
    lines.extend([
        "fn dispatch(w):",
        "  return w.execute()",
        "",
        "fn main():",
        "  var total = 0"
    ])
    for i in range(type_count):
        lines.append(f"  total = total + dispatch(Worker{i:03d}(code: 10))")
    lines.extend([
        "  echo total",
        ""
    ])
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Module Galaxy Generators
# ---------------------------------------------------------------------------

def gen_mg1_multi_file(base_dir: Path, num_files: int = 10):
    """MG1: Multi-file project - single logical module split across many source files."""
    base_dir.mkdir(parents=True, exist_ok=True)
    src_dir = base_dir / "src"
    src_dir.mkdir(parents=True, exist_ok=True)

    (base_dir / "Moss.toml").write_text(
        '[project]\nname = "mg1-multi-file"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n'
    )
    for i in range(1, num_files + 1):
        content = [
            f"module mg1_multi_file",
            "",
            f"export fn compute_part_{i:02d}(x: Int) -> Int:",
            f"  return x + {i}",
            ""
        ]
        (src_dir / f"part_{i:02d}.moss").write_text("\n".join(content))

    main_content = [
        "module mg1_multi_file",
        "",
        "fn main():",
        "  var total = 0"
    ]
    for i in range(1, num_files + 1):
        main_content.append(f"  total = total + compute_part_{i:02d}(10)")
    main_content.extend([
        "  echo total",
        ""
    ])
    (src_dir / "main.moss").write_text("\n".join(main_content))


def gen_mg2_multi_module(base_dir: Path, num_modules: int = 8):
    """MG2: Multi-module project - multiple distinct modules imported by app."""
    base_dir.mkdir(parents=True, exist_ok=True)
    src_dir = base_dir / "src"
    src_dir.mkdir(parents=True, exist_ok=True)

    (base_dir / "Moss.toml").write_text(
        '[project]\nname = "mg2-multi-module"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n'
    )
    for i in range(1, num_modules + 1):
        content = [
            f"module mod_{i:02d}",
            "",
            f"export type Item{i:02d}:",
            "  val: Int",
            "",
            f"export fn evaluate_{i:02d}(item: Item{i:02d}) -> Int:",
            f"  return item.val * {i}",
            ""
        ]
        (src_dir / f"mod_{i:02d}.moss").write_text("\n".join(content))

    main_lines = [
        "module app",
    ]
    for i in range(1, num_modules + 1):
        main_lines.append(f"import mod_{i:02d}")
    main_lines.extend([
        "",
        "fn main():",
        "  var total = 0"
    ])
    for i in range(1, num_modules + 1):
        main_lines.append(f"  total = total + mod_{i:02d}.evaluate_{i:02d}(mod_{i:02d}.Item{i:02d}(val: 5))")
    main_lines.extend([
        "  echo total",
        ""
    ])
    (src_dir / "main.moss").write_text("\n".join(main_lines))


def gen_mg3_deep_chain(base_dir: Path, chain_len: int = 8):
    """MG3: Deep linear module dependency chain (mod01 -> mod02 -> ... -> mod08 -> app)."""
    base_dir.mkdir(parents=True, exist_ok=True)
    src_dir = base_dir / "src"
    src_dir.mkdir(parents=True, exist_ok=True)

    (base_dir / "Moss.toml").write_text(
        '[project]\nname = "mg3-deep-chain"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n'
    )
    # First module
    (src_dir / "chain_01.moss").write_text(
        "module chain_01\n\nexport fn calculate_01(x: Int) -> Int:\n  return x + 1\n"
    )
    # Intermediate modules
    for i in range(2, chain_len + 1):
        prev = f"chain_{i-1:02d}"
        curr = f"chain_{i:02d}"
        content = [
            f"module {curr}",
            f"import {prev}",
            "",
            f"export fn calculate_{i:02d}(x: Int) -> Int:",
            f"  return {prev}.calculate_{i-1:02d}(x) + {i}",
            ""
        ]
        (src_dir / f"{curr}.moss").write_text("\n".join(content))

    # App module
    last = f"chain_{chain_len:02d}"
    main_lines = [
        "module app",
        f"import {last}",
        "",
        "fn main():",
        f"  echo {last}.calculate_{chain_len:02d}(0)",
        ""
    ]
    (src_dir / "main.moss").write_text("\n".join(main_lines))


def gen_mg4_fan_out(base_dir: Path, num_consumers: int = 8):
    """MG4: Shared generic provider module imported by many consumers."""
    base_dir.mkdir(parents=True, exist_ok=True)
    src_dir = base_dir / "src"
    src_dir.mkdir(parents=True, exist_ok=True)

    (base_dir / "Moss.toml").write_text(
        '[project]\nname = "mg4-fan-out"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n'
    )
    # Provider
    (src_dir / "shared_provider.moss").write_text(
        "module shared_provider\n\n"
        "export type Config:\n"
        "  multiplier: Int\n\n"
        "export fn transform(cfg: Config, base: Int) -> Int:\n"
        "  return cfg.multiplier * base\n"
    )
    # Consumers
    for i in range(1, num_consumers + 1):
        content = [
            f"module consumer_{i:02d}",
            "import shared_provider",
            "",
            f"export fn run_worker_{i:02d}(input_val: Int) -> Int:",
            f"  let cfg = shared_provider.Config(multiplier: {i})",
            "  return shared_provider.transform(cfg, input_val)",
            ""
        ]
        (src_dir / f"consumer_{i:02d}.moss").write_text("\n".join(content))

    # App
    main_lines = ["module app"]
    for i in range(1, num_consumers + 1):
        main_lines.append(f"import consumer_{i:02d}")
    main_lines.extend([
        "",
        "fn main():",
        "  var total = 0"
    ])
    for i in range(1, num_consumers + 1):
        main_lines.append(f"  total = total + consumer_{i:02d}.run_worker_{i:02d}(10)")
    main_lines.extend([
        "  echo total",
        ""
    ])
    (src_dir / "main.moss").write_text("\n".join(main_lines))


def gen_mg5_diamond(base_dir: Path):
    """MG5: Diamond module dependency topology (base -> left & right -> app)."""
    base_dir.mkdir(parents=True, exist_ok=True)
    src_dir = base_dir / "src"
    src_dir.mkdir(parents=True, exist_ok=True)

    (base_dir / "Moss.toml").write_text(
        '[project]\nname = "mg5-diamond"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n'
    )
    # Base
    (src_dir / "diamond_base.moss").write_text(
        "module diamond_base\n\n"
        "export type Metric:\n"
        "  value: Int\n\n"
        "export fn format_metric(m: Metric) -> Int:\n"
        "  return m.value * 2\n"
    )
    # Left
    (src_dir / "diamond_left.moss").write_text(
        "module diamond_left\n"
        "import diamond_base\n\n"
        "export fn left_flow(val: Int) -> Int:\n"
        "  let m = diamond_base.Metric(value: val)\n"
        "  return diamond_base.format_metric(m) + 10\n"
    )
    # Right
    (src_dir / "diamond_right.moss").write_text(
        "module diamond_right\n"
        "import diamond_base\n\n"
        "export fn right_flow(val: Int) -> Int:\n"
        "  let m = diamond_base.Metric(value: val)\n"
        "  return diamond_base.format_metric(m) + 20\n"
    )
    # App
    (src_dir / "main.moss").write_text(
        "module app\n"
        "import diamond_left\n"
        "import diamond_right\n\n"
        "fn main():\n"
        "  let res_l = diamond_left.left_flow(5)\n"
        "  let res_r = diamond_right.right_flow(5)\n"
        "  echo res_l + res_r\n"
    )


# ---------------------------------------------------------------------------
# Corpus Collection: Examples, Negative Tests, Core Tests
# ---------------------------------------------------------------------------

CANONICAL_EXAMPLES = [
    "checkout.moss",
    "collections.moss",
    "counter.moss",
    "domain_typing.moss",
    "domains_and_messages.moss",
    "functional_basics.moss",
    "functional_dataflow.moss",
    "functional_pipeline.moss",
    "shared_memory.moss",
    "static_duck_typing.moss",
]

CANONICAL_NEGATIVE_TESTS = [
    "tests/negative/await_retired.moss",
    "tests/negative/conflicting_access.moss",
    "tests/negative/domain_route_cycle.moss",
    "tests/negative/duck_missing_method.moss",
    "tests/negative/heterogeneous_collection.moss",
    "tests/negative/message_payload_consume.moss",
    "tests/negative/message_payload_rebind.moss",
    "tests/negative/message_payload_write.moss",
    "tests/negative/mutually_recursive_functions.moss",
    "tests/negative/recursive_call.moss",
    "tests/negative/self_send_rejected.moss",
    "tests/negative/spawn_retired.moss",
    "tests/negative/trait_missing_method.moss",
    "tests/negative/write_write_alias.moss",
]

CANONICAL_CORE_TESTS = [
    "tests/trait_open_forwarding.moss",
    "tests/unused_inferred_field_template.moss",
    "tests/phase45_short_circuit_failure_barrier.moss",
]


def copy_corpus():
    """Copy canonical existing repo examples, negative tests, and test fixtures into frozen suite."""
    examples_dir = FIXTURES_DIR / "examples"
    negative_dir = FIXTURES_DIR / "negative"
    tests_dir = FIXTURES_DIR / "tests"

    examples_dir.mkdir(parents=True, exist_ok=True)
    negative_dir.mkdir(parents=True, exist_ok=True)
    tests_dir.mkdir(parents=True, exist_ok=True)

    for item in CANONICAL_EXAMPLES:
        src = REPO_ROOT / "examples" / item
        if src.is_file():
            shutil.copy2(src, examples_dir / item)

    for item in CANONICAL_NEGATIVE_TESTS:
        src = REPO_ROOT / item
        if src.is_file():
            shutil.copy2(src, negative_dir / src.name)

    for item in CANONICAL_CORE_TESTS:
        src = REPO_ROOT / item
        if src.is_file():
            shutil.copy2(src, tests_dir / src.name)


# ---------------------------------------------------------------------------
# Main Generation & Manifest Assembly
# ---------------------------------------------------------------------------

def generate_all():
    FIXTURES_DIR.mkdir(parents=True, exist_ok=True)
    EXPECTED_DIR.mkdir(parents=True, exist_ok=True)

    spec_dir = FIXTURES_DIR / "specialization"
    infer_dir = FIXTURES_DIR / "inference"
    coll_dir = FIXTURES_DIR / "collections"
    trait_dir = FIXTURES_DIR / "traits"
    mod_dir = FIXTURES_DIR / "modules"

    for d in (spec_dir, infer_dir, coll_dir, trait_dir, mod_dir):
        d.mkdir(parents=True, exist_ok=True)

    # 1. Specialization W2 / W3
    for n in (8, 16, 32):
        (spec_dir / f"w2_spec_distinct_n{n:03d}.moss").write_text(gen_w2_specialization(n))
        (spec_dir / f"w3_spec_repeated_n{n:03d}.moss").write_text(gen_w3_specialization(n))

    # 2. Inference W4 / W5
    for d in (5, 10, 15, 18):
        (infer_dir / f"w4_infer_deep_d{d:03d}.moss").write_text(gen_w4_inference_deep(d))
    for w in (10, 25, 50, 100):
        (infer_dir / f"w5_infer_wide_w{w:03d}.moss").write_text(gen_w5_inference_wide(w))

    # 3. Collections & Types W6 / W7
    for t in (10, 25, 50, 100):
        (coll_dir / f"w6_types_count_t{t:03d}.moss").write_text(gen_w6_types(t))
    for k in (1, 2, 3, 4, 5):
        (coll_dir / f"w7_collections_nested_k{k}.moss").write_text(gen_w7_collections(k))

    # 4. Traits & Method Resolution W8
    for c in (10, 25, 50, 100):
        (trait_dir / f"w8_traits_dispatch_c{c:03d}.moss").write_text(gen_w8_traits(c))

    # 5. Module Galaxy Projects
    gen_mg1_multi_file(mod_dir / "mg1_multi_file", 10)
    gen_mg2_multi_module(mod_dir / "mg2_multi_module", 8)
    gen_mg3_deep_chain(mod_dir / "mg3_deep_chain", 8)
    gen_mg4_fan_out(mod_dir / "mg4_fan_out", 8)
    gen_mg5_diamond(mod_dir / "mg5_diamond")

    # 6. Real-World Corpus (Examples, Negative, Tests)
    copy_corpus()


def build_manifests():
    fixtures = []

    # Helper to register a fixture
    def register(fid: str, category: str, rel_path: str, expected_status: str,
                 is_project: bool, metadata: dict):
        abs_p = ROOT_DIR / rel_path
        if is_project:
            # Hash all project files in canonical sort order
            h = hashlib.sha256()
            for p in sorted(abs_p.rglob("*")):
                if p.is_file():
                    h.update(p.relative_to(abs_p).as_posix().encode())
                    h.update(p.read_bytes())
            entry_hash = h.hexdigest()
        else:
            entry_hash = file_sha256(abs_p)

        fixtures.append({
            "id": fid,
            "category": category,
            "path": rel_path,
            "is_project": is_project,
            "expected_status": expected_status,
            "sha256": entry_hash,
            "generator_version": GENERATOR_VERSION,
            "generator_seed": GENERATOR_SEED,
            "metadata": metadata
        })

    # W2 / W3
    for n in (8, 16, 32):
        register(f"w2_spec_distinct_n{n:03d}", "specialization",
                 f"fixtures/specialization/w2_spec_distinct_n{n:03d}.moss", "pass", False,
                 {"workload": "W2", "n": n, "distinct_signatures": n * n, "call_sites": n * n})
        register(f"w3_spec_repeated_n{n:03d}", "specialization",
                 f"fixtures/specialization/w3_spec_repeated_n{n:03d}.moss", "pass", False,
                 {"workload": "W3", "n": n, "distinct_signatures": 4, "call_sites": n * n})

    # W4 / W5
    for d in (5, 10, 15, 18):
        register(f"w4_infer_deep_d{d:03d}", "inference",
                 f"fixtures/inference/w4_infer_deep_d{d:03d}.moss", "pass", False,
                 {"workload": "W4", "depth": d, "chain_length": d + 1})
    for w in (10, 25, 50, 100):
        register(f"w5_infer_wide_w{w:03d}", "inference",
                 f"fixtures/inference/w5_infer_wide_w{w:03d}.moss", "pass", False,
                 {"workload": "W5", "width": w, "independent_chains": w})

    # W6 / W7
    for t in (10, 25, 50, 100):
        register(f"w6_types_count_t{t:03d}", "collections",
                 f"fixtures/collections/w6_types_count_t{t:03d}.moss", "pass", False,
                 {"workload": "W6", "type_count": t})
    for k in (1, 2, 3, 4, 5):
        register(f"w7_collections_nested_k{k}", "collections",
                 f"fixtures/collections/w7_collections_nested_k{k}.moss", "pass", False,
                 {"workload": "W7", "nesting_depth": k})

    # W8
    for c in (10, 25, 50, 100):
        register(f"w8_traits_dispatch_c{c:03d}", "traits",
                 f"fixtures/traits/w8_traits_dispatch_c{c:03d}.moss", "pass", False,
                 {"workload": "W8", "concrete_types": c})

    # Module Galaxy
    register("mg1_multi_file", "modules", "fixtures/modules/mg1_multi_file", "pass", True,
             {"workload": "MG1", "modules": 1, "files": 11})
    register("mg2_multi_module", "modules", "fixtures/modules/mg2_multi_module", "pass", True,
             {"workload": "MG2", "modules": 9, "files": 9})
    register("mg3_deep_chain", "modules", "fixtures/modules/mg3_deep_chain", "pass", True,
             {"workload": "MG3", "modules": 9, "chain_depth": 8, "files": 9})
    register("mg4_fan_out", "modules", "fixtures/modules/mg4_fan_out", "pass", True,
             {"workload": "MG4", "modules": 10, "consumers": 8, "files": 10})
    register("mg5_diamond", "modules", "fixtures/modules/mg5_diamond", "pass", True,
             {"workload": "MG5", "modules": 4, "topology": "diamond", "files": 4})

    # Examples
    for item in sorted((FIXTURES_DIR / "examples").glob("*.moss")):
        register(f"example_{item.stem}", "examples", f"fixtures/examples/{item.name}", "pass", False,
                 {"workload": "example", "source": item.name})

    # Negative Tests
    for item in sorted((FIXTURES_DIR / "negative").glob("*.moss")):
        register(f"negative_{item.stem}", "negative", f"fixtures/negative/{item.name}", "fail", False,
                 {"workload": "negative", "source": item.name})

    # Tests
    for item in sorted((FIXTURES_DIR / "tests").glob("*.moss")):
        register(f"test_{item.stem}", "tests", f"fixtures/tests/{item.name}", "pass", False,
                 {"workload": "test", "source": item.name})

    manifest = {
        "suite_name": "moss_compiler_torture",
        "version": GENERATOR_VERSION,
        "seed": GENERATOR_SEED,
        "total_fixtures": len(fixtures),
        "fixtures": fixtures
    }

    manifest_path = ROOT_DIR / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")

    val_manifest = {
        "suite_name": "moss_compiler_torture_validation",
        "version": GENERATOR_VERSION,
        "total_fixtures": len(fixtures),
        "expectations": {
            f["id"]: {
                "expected_status": f["expected_status"],
                "sha256": f["sha256"]
            }
            for f in fixtures
        }
    }
    (EXPECTED_DIR / "validation_manifest.json").write_text(json.dumps(val_manifest, indent=2) + "\n")

    print(f"Manifest written: {manifest_path} ({len(fixtures)} fixtures)")
    return manifest


def validate_against_compiler(moss_bin: Path):
    """Validate all positive fixtures pass and all negative fixtures fail with moss check."""
    manifest = json.loads((ROOT_DIR / "manifest.json").read_text())
    print(f"Validating {manifest['total_fixtures']} fixtures against {moss_bin}...")

    failures = []
    for f in manifest["fixtures"]:
        path = ROOT_DIR / f["path"]
        target = path / "src" / "main.moss" if f["is_project"] else path
        res = subprocess.run([str(moss_bin), "check", str(target), "--json"],
                             capture_output=True, text=True)
        try:
            ok = json.loads(res.stdout).get("ok") is True
        except Exception:
            ok = False

        expected_ok = f["expected_status"] == "pass"
        if ok != expected_ok:
            failures.append((f["id"], f["expected_status"], ok, res.stderr or res.stdout))
            print(f"  [FAIL] {f['id']}: expected {f['expected_status']}, got ok={ok}")
        else:
            print(f"  [OK]   {f['id']}: matched expected {f['expected_status']}")

    if failures:
        print(f"\n{len(failures)} fixtures failed validation!")
        sys.exit(1)
    else:
        print("\nAll fixtures 100% validated against compiler!")


def main():
    parser = argparse.ArgumentParser(description="Portable Moss Torture Benchmark Suite Generator")
    parser.add_argument("--moss", help="Path to moss compiler for validation", default=None)
    parser.add_argument("--validate-only", action="store_true", help="Only validate existing fixtures")
    args = parser.parse_args()

    if not args.validate_only:
        print("Generating portable torture fixtures...")
        generate_all()
        build_manifests()

    if args.moss:
        validate_against_compiler(Path(args.moss).resolve())


if __name__ == "__main__":
    main()
