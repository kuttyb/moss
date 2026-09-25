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

    # --- SWARM-051 Multi-Module Callable Argument Convergence ---
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
            "  for x in items:\n"
            "    out.push(transform(x))\n"
            "  return out\n"
        )

        main_unqualified = (
            "module app\n"
            "import tools\n\n"
            "fn double_val(x: Int) -> Int:\n"
            "  return x * 2\n\n"
            "fn main():\n"
            "  nums = [1, 2, 3]\n"
            "  res = tools.map_by(nums, double_val)\n"
            "  echo res[0]\n"
            "  echo res[1]\n"
            "  echo res[2]\n"
        )
        main_qualified = (
            "module app\n"
            "import tools\n\n"
            "fn double_val(x: Int) -> Int:\n"
            "  return x * 2\n\n"
            "fn main():\n"
            "  nums = [1, 2, 3]\n"
            "  res = tools.map_by(nums, app.double_val)\n"
            "  echo res[0]\n"
            "  echo res[1]\n"
            "  echo res[2]\n"
        )

        # Build and run unqualified sibling callable syntax
        (proj_root / "src" / "main.moss").write_text(main_unqualified)
        build_doc_unqualified = invoke(compiler, proj_root, "build", "--json")
        if not build_doc_unqualified.get("ok"):
            fail(f"build failed for unqualified sibling callable syntax: {build_doc_unqualified}")
        exe_unqualified = pathlib.Path(build_doc_unqualified["result"]["artifacts"]["executable"])
        out_unqualified = subprocess.check_output([str(exe_unqualified)], text=True)
        if out_unqualified.strip() != "2\n4\n6":
            fail(f"unqualified sibling callable output incorrect: expected '2\\n4\\n6', got: {out_unqualified!r}")

        # Build and run explicitly qualified sibling callable syntax
        (proj_root / "src" / "main.moss").write_text(main_qualified)
        build_doc_qualified = invoke(compiler, proj_root, "build", "--json")
        if not build_doc_qualified.get("ok"):
            fail(f"build failed for qualified sibling callable syntax: {build_doc_qualified}")
        exe_qualified = pathlib.Path(build_doc_qualified["result"]["artifacts"]["executable"])
        out_qualified = subprocess.check_output([str(exe_qualified)], text=True)
        if out_qualified.strip() != "2\n4\n6":
            fail(f"qualified sibling callable output incorrect: expected '2\\n4\\n6', got: {out_qualified!r}")

        # Ensure both converge to the same native behaviour and specialization
        app_rs = (proj_root / "build" / "debug" / "app.rs").read_text()
        if "app__double_val" not in app_rs and "App__double_val" not in app_rs:
            fail("compiled app.rs does not contain resolved callable target")


if __name__ == "__main__":
    main()
