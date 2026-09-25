#!/usr/bin/env python3
"""Focused Phase 15.14 regressions for SWARM-038 and SWARM-051."""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys
import tempfile


def fail(message: str) -> None:
    raise SystemExit(f"Phase 15.14 test failed: {message}")


def invoke(compiler: pathlib.Path, cwd: pathlib.Path, *arguments: str, expect: int = 0) -> dict[str, object]:
    completed = subprocess.run(
        [str(compiler), *arguments],
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if completed.returncode != expect:
        fail(
            f"{' '.join(arguments)} exited {completed.returncode}, expected {expect}:\n"
            f"stdout: {completed.stdout}\nstderr: {completed.stderr}"
        )
    try:
        return json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        fail(f"{' '.join(arguments)} did not emit valid JSON: {error}\nstdout: {completed.stdout}")


def main() -> None:
    if len(sys.argv) < 2:
        fail("usage: check_phase15_14_swarm_038_051.py <compiler> [workdir]")

    root = pathlib.Path.cwd().resolve()
    compiler = pathlib.Path(sys.argv[1]).resolve()

    # --- SWARM-038 Semantic-Query Regression ---
    repro_file = root / "tests" / "swarm_038_effects_loop_helper.moss"
    effects_doc = invoke(compiler, root, "effects", "zeros", "--source", str(repro_file), "--json")
    if not effects_doc.get("ok"):
        fail(f"effects query on zeros failed: {effects_doc}")
    observable = effects_doc["result"]["observable_effects"]
    if observable["may_diverge"]:
        fail("effects query reported bounded loop helper as may_diverge: true")
    if observable["unresolved"]:
        fail("effects query reported vector helper as unresolved: true")
    if observable["external_io"] or observable["message"] or observable["domain_write"] or observable["local_mutation"]:
        fail(f"effects query reported unexpected side effects for pure loop helper: {observable}")

    # --- SWARM-038 Impure Domain State Initializer Rejection ---
    impure_file = root / "tests" / "negative" / "swarm_038_impure_state_init.moss"
    check_doc = invoke(compiler, root, "check", str(impure_file), "--json", expect=1)
    if check_doc.get("ok") is not False:
        fail(f"check unexpectedly accepted impure domain state initializer: {check_doc}")
    err_msg = check_doc.get("error", {}).get("message", "")
    if "domain state initializers must be side-effect-free" not in err_msg:
        fail(f"unexpected error message for impure domain state initializer: {err_msg}")

    # --- SWARM-038 Infinite Monotonic-Looking Loop Rejection ---
    divergent_file = root / "tests" / "negative" / "swarm_038_divergent_loop_state_init.moss"
    check_doc = invoke(compiler, root, "check", str(divergent_file), "--json", expect=1)
    if check_doc.get("ok") is not False:
        fail(f"check unexpectedly accepted divergent loop domain state initializer: {check_doc}")
    err_msg = check_doc.get("error", {}).get("message", "")
    if "domain state initializers must be side-effect-free" not in err_msg:
        fail(f"unexpected error message for divergent loop domain state initializer: {err_msg}")

    # --- SWARM-038 Empty-Pop Initializer Rejection ---
    empty_pop_file = root / "tests" / "negative" / "swarm_038_empty_pop_state_init.moss"
    check_doc = invoke(compiler, root, "check", str(empty_pop_file), "--json", expect=1)
    if check_doc.get("ok") is not False:
        fail(f"check unexpectedly accepted empty-pop domain state initializer: {check_doc}")
    err_msg = check_doc.get("error", {}).get("message", "")
    if "domain state initializers must be side-effect-free" not in err_msg:
        fail(f"unexpected error message for empty-pop domain state initializer: {err_msg}")

    # --- SWARM-051 Multi-Module Callable Argument Convergence & Lexical Scope Coverage ---
    with tempfile.TemporaryDirectory(prefix="moss-swarm051-") as temporary:
        proj_root = pathlib.Path(temporary)
        (proj_root / "src").mkdir()
        (proj_root / "moss.toml").write_text(
            '[project]\nname = "swarm051_convergence"\nversion = "0.1.0"\n'
            '[build]\nsource = "src"\n'
        )
        (proj_root / "src" / "tools.moss").write_text(
            "module tools\n\n"
            "export fn map_by(items: Vector[Int], transform) -> Vector[Int]:\n"
            "  var out = Vector[Int]()\n"
            "  var idx = 0\n"
            "  while idx < 3:\n"
            "    out.push(transform(items[idx]))\n"
            "    idx = idx + 1\n"
            "  return out\n"
        )

        main_unqualified = (
            "module app\n"
            "import tools\n\n"
            "fn result() -> Int:\n"
            "  return 999\n\n"
            "fn double_val(x: Int) -> Int:\n"
            "  return x * 2\n\n"
            "fn compute(nums: Vector[Int]) -> Vector[Int]:\n"
            "  if true:\n"
            "    double_val = 99\n"
            "    echo double_val\n"
            "  var loop_var = 0\n"
            "  while loop_var < 1:\n"
            "    double_val = 100\n"
            "    echo double_val\n"
            "    loop_var = loop_var + 1\n"
            "  let result = tools.map_by(nums, double_val)\n"
            "  result\n\n"
            "fn main():\n"
            "  nums = [1, 2, 3]\n"
            "  res = compute(nums)\n"
            "  echo res[0]\n"
            "  echo res[1]\n"
            "  echo res[2]\n"
        )
        main_qualified = (
            "module app\n"
            "import tools\n\n"
            "fn result() -> Int:\n"
            "  return 999\n\n"
            "fn double_val(x: Int) -> Int:\n"
            "  return x * 2\n\n"
            "fn compute(nums: Vector[Int]) -> Vector[Int]:\n"
            "  if true:\n"
            "    double_val = 99\n"
            "    echo double_val\n"
            "  var loop_var = 0\n"
            "  while loop_var < 1:\n"
            "    double_val = 100\n"
            "    echo double_val\n"
            "    loop_var = loop_var + 1\n"
            "  let result = tools.map_by(nums, app.double_val)\n"
            "  result\n\n"
            "fn main():\n"
            "  nums = [1, 2, 3]\n"
            "  res = compute(nums)\n"
            "  echo res[0]\n"
            "  echo res[1]\n"
            "  echo res[2]\n"
        )

        expected_output = "99\n100\n2\n4\n6"

        # 1. Build and run unqualified sibling callable syntax (native)
        (proj_root / "src" / "main.moss").write_text(main_unqualified)
        build_doc_unqualified = invoke(compiler, proj_root, "build", "--json")
        if not build_doc_unqualified.get("ok"):
            fail(f"build failed for unqualified sibling callable syntax: {build_doc_unqualified}")
        exe_unqualified = pathlib.Path(build_doc_unqualified["result"]["artifacts"]["executable"])
        out_unqualified = subprocess.check_output([str(exe_unqualified)], text=True)
        if out_unqualified.strip() != expected_output:
            fail(f"unqualified sibling callable output incorrect: expected {expected_output!r}, got: {out_unqualified!r}")

        # 2. Run Fast Debug on unqualified project
        debug_run_unqualified = subprocess.run(
            [str(compiler), "debug", str(proj_root)],
            cwd=proj_root,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        if debug_run_unqualified.returncode != 0:
            fail(f"Fast Debug failed for unqualified project: {debug_run_unqualified.stderr}")
        if debug_run_unqualified.stdout.strip() != expected_output:
            fail(f"Fast Debug unqualified output mismatch: expected {expected_output!r}, got: {debug_run_unqualified.stdout!r}")

        # 3. Build and run explicitly qualified sibling callable syntax (native)
        (proj_root / "src" / "main.moss").write_text(main_qualified)
        build_doc_qualified = invoke(compiler, proj_root, "build", "--json")
        if not build_doc_qualified.get("ok"):
            fail(f"build failed for qualified sibling callable syntax: {build_doc_qualified}")
        exe_qualified = pathlib.Path(build_doc_qualified["result"]["artifacts"]["executable"])
        out_qualified = subprocess.check_output([str(exe_qualified)], text=True)
        if out_qualified.strip() != expected_output:
            fail(f"qualified sibling callable output incorrect: expected {expected_output!r}, got: {out_qualified!r}")

        # 4. Run Fast Debug on qualified project
        debug_run_qualified = subprocess.run(
            [str(compiler), "debug", str(proj_root)],
            cwd=proj_root,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        if debug_run_qualified.returncode != 0:
            fail(f"Fast Debug failed for qualified project: {debug_run_qualified.stderr}")
        if debug_run_qualified.stdout.strip() != expected_output:
            fail(f"Fast Debug qualified output mismatch: expected {expected_output!r}, got: {debug_run_qualified.stdout!r}")

        # Ensure both converge to the same native behaviour and specialization
        app_rs = (proj_root / "build" / "debug" / "app.rs").read_text()
        if "app__double_val" not in app_rs and "App__double_val" not in app_rs:
            fail("compiled app.rs does not contain resolved callable target")

        # 5. Native test for for-loop induction variable shadowing sibling function name
        main_for_loop = (
            "module app\n"
            "import tools\n\n"
            "fn double_val(x: Int) -> Int:\n"
            "  return x * 2\n\n"
            "fn compute(nums: Vector[Int]) -> Vector[Int]:\n"
            "  for double_val in [101]:\n"
            "    echo double_val\n"
            "  let result = tools.map_by(nums, double_val)\n"
            "  result\n\n"
            "fn main():\n"
            "  nums = [1, 2, 3]\n"
            "  res = compute(nums)\n"
            "  echo res[0]\n"
            "  echo res[1]\n"
            "  echo res[2]\n"
        )
        (proj_root / "src" / "main.moss").write_text(main_for_loop)
        build_doc_for = invoke(compiler, proj_root, "build", "--json")
        if not build_doc_for.get("ok"):
            fail(f"build failed for for-loop shadowing syntax: {build_doc_for}")
        exe_for = pathlib.Path(build_doc_for["result"]["artifacts"]["executable"])
        out_for = subprocess.check_output([str(exe_for)], text=True)
        expected_for = "101\n2\n4\n6"
        if out_for.strip() != expected_for:
            fail(f"for loop shadowing output incorrect: expected {expected_for!r}, got: {out_for!r}")

        # 6. If/Else joined binding shadowing sibling function name & single branch isolation
        main_shadow = (
            "module app\n"
            "import tools\n\n"
            "fn double_val(x: Int) -> Int:\n"
            "  return x * 2\n\n"
            "fn test_shadow_joined(cond: Bool) -> Int:\n"
            "  if cond:\n"
            "    let double_val = 10\n"
            "  else:\n"
            "    let double_val = 20\n"
            "  return double_val\n\n"
            "fn test_one_branch(cond: Bool, x: Int) -> Int:\n"
            "  if cond:\n"
            "    let double_val = 99\n"
            "  return double_val(x)\n\n"
            "fn main():\n"
            "  echo test_shadow_joined(true)\n"
            "  echo test_shadow_joined(false)\n"
            "  echo test_one_branch(true, 5)\n"
            "  echo test_one_branch(false, 5)\n"
        )
        (proj_root / "src" / "main.moss").write_text(main_shadow)
        expected_shadow = "10\n20\n10\n10"

        # Native build and execution
        build_doc_shadow = invoke(compiler, proj_root, "build", "--json")
        if not build_doc_shadow.get("ok"):
            fail(f"build failed for joined shadow syntax: {build_doc_shadow}")
        exe_shadow = pathlib.Path(build_doc_shadow["result"]["artifacts"]["executable"])
        out_shadow = subprocess.check_output([str(exe_shadow)], text=True)
        if out_shadow.strip() != expected_shadow:
            fail(f"joined shadow native output incorrect: expected {expected_shadow!r}, got: {out_shadow!r}")

        # Fast Debug execution
        debug_run_shadow = subprocess.run(
            [str(compiler), "debug", str(proj_root)],
            cwd=proj_root,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        if debug_run_shadow.returncode != 0:
            fail(f"Fast Debug failed for joined shadow project: {debug_run_shadow.stderr}")
        if debug_run_shadow.stdout.strip() != expected_shadow:
            fail(f"Fast Debug shadow output mismatch: expected {expected_shadow!r}, got: {debug_run_shadow.stdout!r}")

        # 7. Verify legality status of returning a joined statically-known callable
        callable_src = (
            "fn add_one(x: Int) -> Int:\n"
            "  return x + 1\n\n"
            "fn test_callable(cond: Bool):\n"
            "  if cond:\n"
            "    let double_val = add_one\n"
            "  else:\n"
            "    let double_val = add_one\n"
            "  return double_val\n"
        )
        callable_file = proj_root / "callable_check.moss"
        callable_file.write_text(callable_src)
        check_callable = invoke(compiler, proj_root, "check", str(callable_file), "--json", expect=1)
        if check_callable.get("ok") is not False:
            fail("check unexpectedly accepted returning a joined statically-known callable")
        callable_err = check_callable.get("error", {}).get("message", "")
        if "unknown return type" not in callable_err or "callable:add_one" not in callable_err:
            fail(f"unexpected diagnostic when returning joined callable: {callable_err}")
        # 8. New local introduced only inside a for, then referenced after loop -> reject at checking
        for_local_src = (
            "fn test_local():\n"
            "  for i in [1, 2]:\n"
            "    let y = 10\n"
            "  echo y\n"
        )
        f_for_local = proj_root / "for_local_reject.moss"
        f_for_local.write_text(for_local_src)
        check_doc = invoke(compiler, proj_root, "check", str(f_for_local), "--json", expect=1)
        if check_doc.get("ok") is not False:
            fail("check unexpectedly accepted loop-local variable referenced after for loop")
        if "unknown identifier 'y'" not in check_doc.get("error", {}).get("message", ""):
            fail(f"unexpected error message: {check_doc.get('error', {})}")

        # 9. Same case with an empty iterable -> reject at checking
        for_empty_src = (
            "fn test_empty():\n"
            "  let items = Vector[Int]()\n"
            "  for i in items:\n"
            "    let y = 10\n"
            "  echo y\n"
        )
        f_for_empty = proj_root / "for_empty_reject.moss"
        f_for_empty.write_text(for_empty_src)
        check_doc = invoke(compiler, proj_root, "check", str(f_for_empty), "--json", expect=1)
        if check_doc.get("ok") is not False:
            fail("check unexpectedly accepted loop-local variable from empty iterable after for loop")
        if "unknown identifier 'y'" not in check_doc.get("error", {}).get("message", ""):
            fail(f"unexpected error message: {check_doc.get('error', {})}")

        # 10. Pre-existing mutable local updated inside a for -> remains legal and available afterward
        for_mut_src = (
            "fn compute() -> Int:\n"
            "  var total = 0\n"
            "  for x in [10, 20, 30]:\n"
            "    total = total + x\n"
            "  return total\n\n"
            "fn main():\n"
            "  echo compute()\n"
        )
        (proj_root / "src" / "main.moss").write_text(for_mut_src)
        build_doc_mut = invoke(compiler, proj_root, "build", "--json")
        if not build_doc_mut.get("ok"):
            fail(f"build failed for mutable local updated inside for loop: {build_doc_mut}")
        exe_mut = pathlib.Path(build_doc_mut["result"]["artifacts"]["executable"])
        out_mut = subprocess.check_output([str(exe_mut)], text=True)
        if out_mut.strip() != "60":
            fail(f"mutable update output incorrect: expected '60', got: {out_mut!r}")

        # 11. Loop induction variable referenced afterward -> reject
        for_ind_src = (
            "fn test_ind():\n"
            "  for i in [1, 2]:\n"
            "    echo i\n"
            "  echo i\n"
        )
        f_for_ind = proj_root / "for_ind_reject.moss"
        f_for_ind.write_text(for_ind_src)
        check_doc = invoke(compiler, proj_root, "check", str(f_for_ind), "--json", expect=1)
        if check_doc.get("ok") is not False:
            fail("check unexpectedly accepted induction variable referenced after for loop")
        if "unknown identifier 'i'" not in check_doc.get("error", {}).get("message", ""):
            fail(f"unexpected error message: {check_doc.get('error', {})}")

        # 12. Explicit-module version with loop-local colliding with sibling function -> visible again afterward
        mod_loop_shadow = (
            "module app\n\n"
            "fn double_val(x: Int) -> Int:\n"
            "  return x * 2\n\n"
            "fn test_loop_shadow(nums: Vector[Int], x: Int) -> Int:\n"
            "  for double_val in nums:\n"
            "    echo double_val\n"
            "  return double_val(x)\n\n"
            "fn main():\n"
            "  echo test_loop_shadow([101], 5)\n"
        )
        (proj_root / "src" / "main.moss").write_text(mod_loop_shadow)
        build_doc_mod = invoke(compiler, proj_root, "build", "--json")
        if not build_doc_mod.get("ok"):
            fail(f"build failed for module loop shadow project: {build_doc_mod}")
        exe_mod = pathlib.Path(build_doc_mod["result"]["artifacts"]["executable"])
        out_mod = subprocess.check_output([str(exe_mod)], text=True)
        expected_mod = "101\n10"
        if out_mod.strip() != expected_mod:
            fail(f"module loop shadow output incorrect: expected {expected_mod!r}, got: {out_mod!r}")


if __name__ == "__main__":
    main()

