"""Generic-DAG and torture-extension generators (Phase 28.5, Team 2).

Registered in generate.py's GENERATORS. Five deterministic families:

  b1_leak        provider package `lib` exports an untyped generic DAG; the
                 consumer package `app` instantiates the root with K distinct
                 user types. Grid D x W x K = {5,20,50} x {1,4} x {1,8,32}.
  b2_equiv       the same DAG with no package boundary (one file), K = 1..32.
  torture_ext    1.1.0-ext. Extends (never edits) the frozen compiler_torture
                 1.0.0 suite: W4 deep untyped chains at depth 100/500/1000 (+200), a default-stack crash record and a
                 W4 x W2 cross (deep chain, root instantiated with K user types).
                 The chain pattern is copied from compiler_torture/generate.py
                 `gen_w4_inference_deep`; nothing is imported from it.
  dag_fanin      feasible shared-callee family: W entries into one shared deep chain,
                 as a package-boundary variant (pkg) and a single file (file)
  dag_diamond    shared-callee DAG (every node calls two children), K = 1: the
                 shape that exposes a super-linear re-walk of shared callees.

DAG node shape (Moss v0.1 constraints: no recursion, methods only on untyped
parameters, no nested calls of inferred generics, which hits the known
"unresolved state layout" bug; and NO `var c = f(x)` bindings, which make
today's checker exponential in depth, see README "Fixture-shape constraints"):

  leaf   fn n<D-1>_<i>(x): return x.score()
  inner  fn n<l>_<i>(x):   return n<l+1>_<i>(x) + 1          (spine shape)
  root   fn root(x):       return n0_0(x) + n0_1(x) + ... + n0_<W-1>(x)

Spine shape = W parallel chains of depth D under one root: D*W + 1 callables,
the root's argument type flows through every one, no shared callees. The
"diamond" family instead makes node (l,i) call (l+1,i) and (l+1,(i+1)%W).
"""
from __future__ import annotations

from pathlib import Path
import random
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ect_common as C  # noqa: E402

Generator = C.Generator

B1_DEPTHS = (5, 20, 50)
B1_WIDTHS = (1, 4)
B1_TYPES = (1, 8, 32)
B2_DEPTH, B2_WIDTH, B2_MAX_K = 20, 4, 32
# Deep dependent chains recurse deeply in the checker, so torture_ext fixtures
# run with a large stack (4 GiB soft limit, virtual only) and no longer depend on
# the machine's default 8 MiB or the build flavor. One small fixture keeps the
# default-stack crash on record (environment dependent by nature).
TORTURE_STACK_KIB = 4 * 1024 * 1024
DEFAULT_STACK_KIB = 8192
CRASH_DEPTH = 300      # cliff on moss-opt with 8 MiB is ~258 (Int) / ~250 (user type)
W4_DEPTHS = (100, 200, 500, 1000)
CROSS_CASES = ((100, 2), (100, 8), (100, 32), (200, 4), (500, 4))
FANIN_DEPTH, FANIN_WIDTH, FANIN_TYPES = 50, 4, (1, 8, 32)
DIAMOND_DEPTHS = (6, 8, 10, 12)
DIAMOND_WIDTH = 4


def user_types(k: int, base: int) -> list[str]:
    lines: list[str] = []
    for i in range(k):
        lines += [f"type T{i:03d}:", "  v: Int", "", "  fn score() -> Int:",
                  f"    return v + {base + i}", ""]
    return lines


def node(level: int, i: int) -> str:
    return f"n{level:03d}_{i}"


def dag_functions(depth: int, width: int, *, shared: bool, export_root: bool) -> list[str]:
    lines: list[str] = []
    for level in range(depth - 1, -1, -1):
        for i in range(width):
            lines.append(f"fn {node(level, i)}(x):")
            if level == depth - 1:
                lines.append("  return x.score()")
            else:
                kids = sorted({i, (i + 1) % width}) if shared else [i]
                lines.append("  return " + " + ".join(f"{node(level + 1, c)}(x)" for c in kids)
                             + ("" if shared and len(kids) > 1 else " + 1"))
            lines.append("")
    lines.append(("export " if export_root else "") + "fn root(x):")
    lines.append("  return " + " + ".join(f"{node(0, i)}(x)" for i in range(width)))
    lines.append("")
    return lines


def main_calls(k: int, callee: str) -> list[str]:
    return (["fn main():", "  var total = 0"] +
            [f"  total = total + {callee}(T{i:03d}(v: {i + 1}))" for i in range(k)] +
            ["  echo total", ""])


class B1LeakGenerator(Generator):
    name = "b1_leak"
    version = "1.0.0"
    category = "b1_leak"

    def generate(self, rng: random.Random, out_dir: Path) -> list[dict]:
        base = rng.randint(1, 9)
        rows = []
        for depth in B1_DEPTHS:
            for width in B1_WIDTHS:
                for k in B1_TYPES:
                    fid = f"b1_d{depth:03d}_w{width}_k{k:03d}"
                    d = out_dir / "b1_leak" / fid
                    (d / "lib" / "src").mkdir(parents=True)
                    (d / "app" / "src").mkdir(parents=True)
                    (d / "lib" / "Moss.toml").write_text(
                        '[project]\nname = "dag_lib"\nversion = "0.1.0"\n')
                    (d / "lib" / "src" / "dag_lib.moss").write_text(
                        "module dag_lib\n\n" + "\n".join(
                            dag_functions(depth, width, shared=False, export_root=True)))
                    (d / "app" / "Moss.toml").write_text(
                        '[project]\nname = "dag_app"\nversion = "0.1.0"\n\n'
                        '[dependencies]\ndag_lib = { path = "../lib" }\n')
                    (d / "app" / "src" / "main.moss").write_text(
                        "module main\nimport dag_lib\n\n" + "\n".join(
                            user_types(k, base) + main_calls(k, "dag_lib.root")))
                    nodes = depth * width + 1
                    rows.append({
                        "id": fid, "path": f"b1_leak/{fid}", "kind": "package",
                        "entry": "app", "provider": "lib", "timeout_s": 900,
                        "expected_status": "pass",
                        "metadata": {"family": "b1_leak", "depth": depth, "width": width,
                                     "types": k, "nodes": nodes, "packages": 2,
                                     "dag_functions_in_consumer": 0,
                                     "predicted_functions_analyzed": nodes,
                                     "predicted_spec_keys": nodes * k}})
        return rows


class B2EquivGenerator(Generator):
    name = "b2_equiv"
    version = "1.0.0"
    category = "b2_equiv"

    def generate(self, rng: random.Random, out_dir: Path) -> list[dict]:
        base = rng.randint(1, 9)
        rows = []
        nodes = B2_DEPTH * B2_WIDTH + 1
        for k in range(1, B2_MAX_K + 1):
            fid = f"b2_k{k:02d}"
            d = out_dir / "b2_equiv" / fid
            d.mkdir(parents=True)
            (d / "main.moss").write_text("\n".join(
                user_types(k, base) +
                dag_functions(B2_DEPTH, B2_WIDTH, shared=False, export_root=False) +
                main_calls(k, "root")))
            rows.append({
                "id": fid, "path": f"b2_equiv/{fid}", "kind": "file",
                "entry": "main.moss", "timeout_s": 600, "expected_status": "pass",
                "metadata": {"family": "b2_equiv", "depth": B2_DEPTH, "width": B2_WIDTH,
                             "types": k, "nodes": nodes, "packages": 1,
                             "predicted_functions_analyzed": nodes,
                             "predicted_spec_keys": nodes * k}})
        return rows


class TortureExtGenerator(Generator):
    """Extends compiler_torture 1.0.0; versioned separately, never an edit of it."""
    name = "torture_ext"
    version = "1.1.0-ext"
    category = "torture_ext"

    @staticmethod
    def w4_source(depth: int) -> str:
        # Same pattern as compiler_torture gen_w4_inference_deep (copied, not imported).
        lines = ["# ECT torture extension 1.1.0-ext: W4 deep dependent inference",
                 f"# Depth={depth}", "", "fn step000(x):", "  return x + 1", ""]
        for i in range(1, depth + 1):
            lines += [f"fn step{i:03d}(x):", f"  return step{i - 1:03d}(x) + 1", ""]
        lines += ["fn main():", f"  echo step{depth:03d}(10)", ""]
        return "\n".join(lines)

    @staticmethod
    def cross_source(depth: int, k: int, base: int) -> str:
        lines = ["# ECT torture extension 1.1.0-ext: W4 x W2 cross",
                 f"# Depth={depth}, distinct concrete types={k}", ""]
        lines += user_types(k, base)
        lines += ["fn step000(x):", "  return x.score() + 1", ""]
        for i in range(1, depth + 1):
            lines += [f"fn step{i:03d}(x):", f"  return step{i - 1:03d}(x) + 1", ""]
        lines += ["fn main():", "  var total = 0"]
        lines += [f"  total = total + step{depth:03d}(T{i:03d}(v: {i + 1}))" for i in range(k)]
        lines += ["  echo total", ""]
        return "\n".join(lines)

    def generate(self, rng: random.Random, out_dir: Path) -> list[dict]:
        base = rng.randint(1, 9)
        rows = []
        d = out_dir / "torture_ext"
        d.mkdir(parents=True)
        for depth in W4_DEPTHS:
            fid = f"tx_w4_d{depth:04d}"
            (d / fid).mkdir()
            (d / fid / "main.moss").write_text(self.w4_source(depth))
            meta = {"family": "torture_ext_w4", "workload": "W4",
                    "depth": depth, "types": 0, "nodes": depth + 1,
                    "extends": "compiler_torture 1.0.0",
                    "predicted_functions_analyzed": depth + 1,
                    "predicted_spec_keys": depth + 1}
            rows.append({"id": fid, "path": f"torture_ext/{fid}", "kind": "file",
                         "entry": "main.moss", "timeout_s": 1800,
                         "stack_kib": TORTURE_STACK_KIB,
                         "expected_status": "pass", "metadata": meta})
        # Documented default-stack crash (environment dependent): same chain,
        # explicit 8 MiB stack, expected SIGSEGV (exit 139).
        fid = f"tx_w4_d{CRASH_DEPTH:04d}_default_stack_crash"
        (d / fid).mkdir()
        (d / fid / "main.moss").write_text(self.w4_source(CRASH_DEPTH))
        rows.append({"id": fid, "path": f"torture_ext/{fid}", "kind": "file",
                     "entry": "main.moss", "timeout_s": 600, "stack_kib": DEFAULT_STACK_KIB,
                     "expected_status": "fail", "expected_exit_code": 139,
                     "metadata": {"family": "torture_ext_crash", "workload": "W4",
                                  "depth": CRASH_DEPTH, "types": 0, "nodes": CRASH_DEPTH + 1,
                                  "environment_dependent": True,
                                  "expected_failure_reason":
                                  "checker SIGSEGV (stack exhaustion) with an 8 MiB stack; "
                                  "cliff on moss-opt ~258 (Int) / ~250 (user type); depends on "
                                  "build flavor (moss-dev -O0 crashes near depth 40) and stack "
                                  "size. A warm client/daemon must provide an equally large "
                                  "stack for the other torture_ext fixtures."}})
        for depth, k in CROSS_CASES:
            fid = f"tx_cross_d{depth:04d}_k{k:02d}"
            (d / fid).mkdir()
            (d / fid / "main.moss").write_text(self.cross_source(depth, k, base))
            meta = {"family": "torture_ext_cross", "workload": "W4xW2", "depth": depth,
                    "types": k, "nodes": depth + 1, "extends": "compiler_torture 1.0.0",
                    "predicted_functions_analyzed": depth + 1,
                    "predicted_spec_keys": (depth + 1) * k}
            rows.append({"id": fid, "path": f"torture_ext/{fid}", "kind": "file",
                         "entry": "main.moss", "timeout_s": 1800,
                         "stack_kib": TORTURE_STACK_KIB,
                         "expected_status": "pass", "metadata": meta})
        return rows


class DagDiamondGenerator(Generator):
    name = "dag_diamond"
    version = "1.0.0"
    category = "dag_diamond"

    def generate(self, rng: random.Random, out_dir: Path) -> list[dict]:
        base = rng.randint(1, 9)
        rows = []
        for depth in DIAMOND_DEPTHS:
            fid = f"diamond_d{depth:03d}_w{DIAMOND_WIDTH}"
            d = out_dir / "dag_diamond" / fid
            d.mkdir(parents=True)
            (d / "main.moss").write_text("\n".join(
                user_types(1, base) +
                dag_functions(depth, DIAMOND_WIDTH, shared=True, export_root=False) +
                main_calls(1, "root")))
            nodes = depth * DIAMOND_WIDTH + 1
            rows.append({"id": fid, "path": f"dag_diamond/{fid}", "kind": "file",
                         "entry": "main.moss", "timeout_s": 900, "expected_status": "pass",
                         "metadata": {"family": "dag_diamond", "depth": depth,
                                      "width": DIAMOND_WIDTH, "types": 1, "nodes": nodes,
                                      "predicted_functions_analyzed": nodes,
                                      "predicted_spec_keys": nodes}})
        return rows


def fanin_functions(depth: int, width: int, export_root: bool) -> list[str]:
    """W entry nodes e_i(x) = c000(x) + i, all calling ONE shared chain
    c_l(x) = c_{l+1}(x) + 1 (leaf: x.score()). Shared callee, linear cost."""
    lines: list[str] = []
    for level in range(depth - 1, -1, -1):
        lines.append(f"fn c{level:03d}(x):")
        lines.append("  return x.score()" if level == depth - 1
                     else f"  return c{level + 1:03d}(x) + 1")
        lines.append("")
    for i in range(width):
        lines += [f"fn e{i}(x):", f"  return c000(x) + {i}", ""]
    lines.append(("export " if export_root else "") + "fn root(x):")
    lines.append("  return " + " + ".join(f"e{i}(x)" for i in range(width)))
    lines.append("")
    return lines


class DagFaninGenerator(Generator):
    """Shared-callee family that is feasible today: W entries into one deep
    chain. `fanin_pkg_*` has a package boundary (like B1); `fanin_file_*` is one
    file (like B2). Per type: requests = nodes + W-1, hits = W-1 (the chain head
    is requested by W entries; one miss, W-1 memo hits)."""
    name = "dag_fanin"
    version = "1.0.0"
    category = "dag_fanin"

    def generate(self, rng: random.Random, out_dir: Path) -> list[dict]:
        base = rng.randint(1, 9)
        rows = []
        depth, width = FANIN_DEPTH, FANIN_WIDTH
        nodes = depth + width + 1
        for variant in ("pkg", "file"):
            for k in FANIN_TYPES:
                fid = f"fanin_{variant}_k{k:03d}"
                d = out_dir / "dag_fanin" / fid
                meta = {"family": f"dag_fanin_{variant}", "depth": depth, "width": width,
                        "types": k, "nodes": nodes,
                        "packages": 2 if variant == "pkg" else 1,
                        "predicted_functions_analyzed": nodes,
                        "predicted_spec_keys": nodes * k,
                        "predicted_spec_requests": (nodes + width - 1) * k,
                        "predicted_spec_hits": (width - 1) * k}
                row = {"id": fid, "path": f"dag_fanin/{fid}", "timeout_s": 900,
                       "expected_status": "pass", "metadata": meta}
                if variant == "pkg":
                    (d / "lib" / "src").mkdir(parents=True)
                    (d / "app" / "src").mkdir(parents=True)
                    (d / "lib" / "Moss.toml").write_text(
                        '[project]\nname = "dag_lib"\nversion = "0.1.0"\n')
                    (d / "lib" / "src" / "dag_lib.moss").write_text(
                        "module dag_lib\n\n" + "\n".join(fanin_functions(depth, width, True)))
                    (d / "app" / "Moss.toml").write_text(
                        '[project]\nname = "dag_app"\nversion = "0.1.0"\n\n'
                        '[dependencies]\ndag_lib = { path = "../lib" }\n')
                    (d / "app" / "src" / "main.moss").write_text(
                        "module main\nimport dag_lib\n\n" + "\n".join(
                            user_types(k, base) + main_calls(k, "dag_lib.root")))
                    meta["dag_functions_in_consumer"] = 0
                    row.update(kind="package", entry="app", provider="lib")
                else:
                    d.mkdir(parents=True)
                    (d / "main.moss").write_text("\n".join(
                        user_types(k, base) + fanin_functions(depth, width, False)
                        + main_calls(k, "root")))
                    row.update(kind="file", entry="main.moss")
                rows.append(row)
        return rows


DAG_GENERATORS = [B1LeakGenerator(), B2EquivGenerator(), TortureExtGenerator(),
                  DagDiamondGenerator(), DagFaninGenerator()]
