#!/usr/bin/env python3
"""Run READ-only field aliases and explicit field-consume controls."""
from __future__ import annotations

import os
import json
from pathlib import Path
import shutil
import subprocess
import sys
from itertools import product


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests/tooling/fixtures/phase28_l8"
READ_CASES = {
    "o15b": "3",
    "field_read": "3",
    "field_nested_index": "3",
    "field_after_vector_literal": "1\n3",
    "field_parameter": "3",
    "field_parameter_caller_reuse": "3\n3",
    "field_rebind_after_vector_literal": "3",
}


def invoke(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=ROOT, capture_output=True, text=True,
                          timeout=120)


def run(args: list[str]) -> subprocess.CompletedProcess[str]:
    result = invoke(args)
    if result.returncode:
        raise RuntimeError(
            f"command failed ({result.returncode}): {args}\n"
            f"{result.stdout}{result.stderr}")
    return result


def native_output(compiler: str, source: Path, scratch: Path, tag: str) -> str:
    rust = scratch / f"{tag}.rs"
    binary = scratch / tag
    run([compiler, str(source), "-o", str(rust)])
    run(["rustc", "--edition=2021", "-D", "warnings", str(rust), "-o",
         str(binary)])
    return run([str(binary)]).stdout.strip()


def expect_step2_map_view(compiler: str, source: Path, scratch: Path,
                         tag: str, engine: str) -> None:
    if engine == "Fast Debug":
        result = invoke([compiler, "run", "--interp", str(source)])
        expected = "MOSS_INTERNAL_INVARIANT nonprimitive Map view lacks checked nonescaping CollectionView authority"
    else:
        result = invoke([compiler, str(source), "-o", str(scratch / f"{tag}.rs")])
        expected = "MOSS_INTERNAL_INVARIANT nonprimitive Map.keys requires checked nonescaping CollectionView authority"
    combined = result.stdout + result.stderr
    if result.returncode == 0 or expected not in combined:
        raise RuntimeError(
            f"{engine} did not reproduce the recorded Step 2 Map.keys blocker; "
            f"exit={result.returncode}\n{combined}")
    print(f"EXPECTED Step 2 CollectionView failure: {tag} {engine}: {expected}")


def grid_source(site: str, source_kind: str, binding: str, use: str) -> tuple[str, str | None]:
    """Build one field-effect control. Return source and expected diagnostic code."""
    if site == "main" and source_kind == "parameter":
        raise ValueError("main has no source parameters")

    declarations = """type Inner:
  frontier: Vector[Int]

type Harness:
  inner: Inner
"""
    if site == "method":
        declarations += "  fn probe(source: Vector[Int]) -> Int:\n    pass\n"
    elif site == "handler":
        declarations = """type Inner:
  frontier: Vector[Int]

domain Harness:
  inner: Inner
"""
        declarations += "  fn Probe(source: Vector[Int]) -> Int:\n    pass\n"

    param = "source: Vector[Int]" if source_kind == "parameter" else ""
    setup: list[str] = []
    source_expr = "source"
    if source_kind == "local":
        setup.append("source = [1, 2, 3]")
    elif source_kind == "vector-element":
        setup.append("items = [[1, 2, 3]]")
        source_expr = "items[0]"
    elif source_kind == "nested-field":
        if site in ("main", "function"):
            setup.append("h = Harness(inner = Inner(frontier = [1, 2, 3]))")
            source_expr = "h.inner.frontier"
        else:
            source_expr = "inner.frontier"

    body: list[str] = list(setup)
    if binding == "reassigned":
        body.append("b = [9]")
    body.append("b = " + source_expr)
    if use == "read":
        body.append("return b |> count")
    else:
        body.extend(("values = [b]", "return values |> count"))

    if site == "main":
        lines = "fn main():\n"
        main_body = list(setup)
        if binding == "reassigned": main_body.append("b = [9]")
        main_body.append("b = " + source_expr)
        if use == "read":
            main_body.extend(("echo (b |> count)",
                              f"echo ({source_expr} |> count)"))
        else:
            main_body.extend(("values = [b]", "echo (values |> count)"))
        lines += "\n".join("  " + line for line in main_body) + "\n"
        lines = declarations + "\n" + lines
    elif site == "function":
        lines = f"fn probe({param}) -> Int:\n" if param else "fn probe() -> Int:\n"
        lines += "\n".join("  " + line for line in body)
        if param:
            lines += "\n\nfn main():\n  source = [1, 2, 3]\n  echo probe(source)\n"
            if use == "read": lines += "  echo (source |> count)\n"
        else:
            lines += "\n\nfn main():\n  echo probe()\n"
        lines = declarations + "\n" + lines
    elif site == "method":
        lines = declarations.replace("  fn probe(source: Vector[Int]) -> Int:\n    pass\n", "")
        lines += "  fn probe(" + param + ") -> Int:\n"
        lines += "\n".join("    " + line for line in body)
        lines += "\n\nfn main():\n  h = Harness(inner = Inner(frontier = [1, 2, 3]))\n"
        if param:
            lines += "  source = [1, 2, 3]\n  echo h.probe(source)\n"
            if use == "read": lines += "  echo (source |> count)\n"
        else:
            lines += "  echo h.probe()\n"
    else:
        lines = declarations.replace("  fn Probe(source: Vector[Int]) -> Int:\n    pass\n", "")
        lines += "  fn Probe(" + param + ") -> Int:\n"
        lines += "\n".join("    " + line.replace("return ", "reply ") for line in body)
        lines += "\n\nfn main():\n  h = Harness(inner = Inner(frontier = [1, 2, 3]))\n"
        if param:
            # Proposal §§1.2/7.5: preserve the caller via an explicit owned
            # duplicate; the handler still receives an immutable payload.
            argument = "copy(source)" if use == "read" else "source"
            lines += f"  source = [1, 2, 3]\n  echo message h.Probe({argument})\n"
            if use == "read": lines += "  echo (source |> count)\n"
        else:
            lines += "  echo message h.Probe()\n"

    expected_rejection = None
    if use == "consume" and source_kind == "vector-element":
        expected_rejection = "OWNERSHIP_ILLEGAL_CONSUME"
    elif use == "consume" and source_kind == "nested-field":
        expected_rejection = "OWNERSHIP_ILLEGAL_CONSUME"
    elif use == "consume" and source_kind == "parameter" and site == "handler":
        # Proposal §7.5 forbids laundering incoming payloads into owned state.
        expected_rejection = ("cannot CONSUME incoming message payload 'source'; "
                              "message payloads may only be read or forwarded")
    return lines, expected_rejection


def run_field_grid(compiler: str, scratch: Path) -> tuple[list[str], int, int]:
    failures: list[str] = []
    passed = 0
    unavailable = 0
    grid = product(("local", "parameter", "vector-element", "nested-field"),
                   ("new", "reassigned"), ("read", "consume"),
                   ("main", "function", "method", "handler"))
    for source_kind, binding, use, site in grid:
        tag = f"grid-{source_kind}-{binding}-{use}-{site}"
        if source_kind == "parameter" and site == "main":
            unavailable += 1
            print(f"UNAVAILABLE {tag}: Moss main has no parameter bindings")
            continue
        text, expected_diagnostic = grid_source(site, source_kind, binding, use)
        source = scratch / f"{tag}.moss"
        source.write_text(text)
        case_failed = False
        fast_output: str | None = None
        for engine in ("Fast Debug", "Native"):
            if engine == "Fast Debug":
                result = invoke([compiler, "run", "--interp", str(source)])
                if result.returncode == 0:
                    fast_output = result.stdout.strip()
            else:
                result = invoke([compiler, str(source), "-o",
                                 str(scratch / f"{tag}.rs")])
            combined = result.stdout + result.stderr
            if expected_diagnostic:
                if result.returncode == 0 or expected_diagnostic not in combined:
                    failures.append(f"{tag} {engine}: expected {expected_diagnostic}, got exit={result.returncode}: {combined}")
                    case_failed = True
            elif result.returncode:
                failures.append(f"{tag} {engine}: expected accept, got {combined}")
                case_failed = True
            elif engine == "Native":
                try:
                    rust = scratch / f"{tag}.rs"
                    run(["rustc", "--edition=2021", "-D", "warnings", str(rust), "-o", str(scratch / tag)])
                    native = run([str(scratch / tag)]).stdout.strip()
                    expected_output = (
                        "3\n3" if use == "read" and
                        (site == "main" or source_kind == "parameter")
                        else ("3" if use == "read" else "1"))
                    if native != fast_output or native != expected_output:
                        failures.append(f"{tag}: expected={expected_output!r}, Fast Debug={fast_output!r}, Native={native!r}")
                        case_failed = True
                except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                    failures.append(f"{tag} Native execution: {error}")
                    case_failed = True
        if not case_failed:
            passed += 1
            print(f"PASS {tag}" + (f": expected {expected_diagnostic}" if expected_diagnostic else ""))
    # Report each failed cell once, retaining all independently observed causes.
    cell_causes: dict[str, set[str]] = {}
    for failure in failures:
        tag = failure.split(":", 1)[0].split(" Fast Debug", 1)[0].split(" Native", 1)[0]
        causes = cell_causes.setdefault(tag, set())
        if "unknown local 'source'" in failure:
            causes.add("FIELD: Fast Debug parameter alias binding")
        elif "OWNERSHIP_USE_AFTER_CONSUME" in failure:
            causes.add("FIELD-REVIEW: handler caller RootIngress transfer")
        elif "indexed read lacks exact checked" in failure:
            causes.add("STEP2: indexed-read authority")
        elif any(text in failure for text in (
                "checker call authority", "ReturnValue owner SpecId",
                "checker-owned caller SpecId", "method edge")):
            causes.add("STEP1: call/body SpecId authority")
    for tag, causes in sorted(cell_causes.items()):
        print(f"FAIL-CELL {tag}: " + "; ".join(sorted(causes or {"UNCLASSIFIED: inspect diagnostics"})))
    return failures, passed, unavailable


def run_echo_reply_controls(compiler: str, scratch: Path) -> list[str]:
    """Preserve §7.5's primitive-reply accept and String-reply reject pair."""
    failures: list[str] = []
    int_source = scratch / "echo_int_reply.moss"
    int_source.write_text("""domain EchoInt:
  fn Echo(n: Int) -> Int:
    reply n

fn main():
  worker = EchoInt()
  echo message worker.Echo(42)
""")
    try:
        fast = run([compiler, "run", "--interp", str(int_source)]).stdout.strip()
        native = native_output(compiler, int_source, scratch, "echo_int_reply")
        if fast != "42" or native != "42" or fast != native:
            failures.append(
                f"Echo Int direct reply: expected '42'; Fast Debug={fast!r}; Native={native!r}")
        else:
            print("PASS Echo Int direct reply: Fast Debug and Native output '42'")
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        failures.append(f"Echo Int direct reply: {error}")

    string_source = scratch / "echo_string_reply.moss"
    string_source.write_text("""domain EchoString:
  fn Echo(value: String) -> String:
    reply value

fn main():
  pass
""")
    result = invoke([compiler, "check", str(string_source), "--json"])
    combined = result.stdout + result.stderr
    if result.returncode == 0 or "OWNERSHIP_ILLEGAL_CONSUME" not in combined:
        failures.append(
            "Echo String direct reply: expected OWNERSHIP_ILLEGAL_CONSUME, "
            f"got exit={result.returncode}: {combined}")
    else:
        print("PASS Echo String direct reply: rejected with OWNERSHIP_ILLEGAL_CONSUME")
    return failures


def run_parameter_binding_controls(compiler: str, scratch: Path) -> list[str]:
    """Check Fast Debug passes checker-issued READ/WRITE/CONSUME effects."""
    cases = {
        "read": ("READ", "3\n3", """fn probe(source: Vector[Int]) -> Int:
  b = source
  return b |> count

fn main():
  source = [1, 2, 3]
  echo probe(source)
  echo (source |> count)
"""),
        "write": ("WRITE", "9\n9", """fn probe(source: Vector[Int]) -> Int:
  source[0] = 9
  return source[0]

fn main():
  source = [1, 2, 3]
  echo probe(source)
  echo source[0]
"""),
        "consume": ("CONSUME", "1", """fn probe(source: Vector[Int]) -> Int:
  b = source
  values = [b]
  return values |> count

fn main():
  echo probe([1, 2, 3])
"""),
    }
    failures: list[str] = []
    for name, (expected_effect, expected_output, source_text) in cases.items():
        source = scratch / f"parameter_binding_{name}.moss"
        source.write_text(source_text)
        try:
            query = run([compiler, "effects", "probe", "--source", str(source),
                         "--json"])
            payload = json.loads(query.stdout)
            effects = payload["result"]["ownership"]
            if len(effects) != 1 or effects[0]["effect"] != expected_effect:
                raise RuntimeError(
                    f"expected checker effect {expected_effect}, got {effects!r}")
            fast = run([compiler, "run", "--interp", str(source)]).stdout.strip()
            native = native_output(compiler, source, scratch,
                                   f"parameter_binding_{name}")
            if fast != expected_output or native != expected_output or fast != native:
                raise RuntimeError(
                    f"expected {expected_output!r}; Fast Debug={fast!r}; "
                    f"Native={native!r}")
            print(f"PASS parameter binding {name.upper()}: checker effect "
                  f"and both engines agree on {fast!r}")
        except (OSError, RuntimeError, KeyError, ValueError,
                subprocess.TimeoutExpired) as error:
            failures.append(f"parameter binding {name.upper()}: {error}")
    return failures


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_phase28_l8_field_controls.py COMPILER", file=sys.stderr)
        return 2
    compiler = str(Path(sys.argv[1]).resolve())
    scratch = ROOT / "tmp" / f"phase28-l8-field-controls-{os.getpid()}"
    scratch.mkdir(parents=True, exist_ok=True)
    failures: list[str] = []
    try:
        grid_failures, grid_passed, grid_unavailable = run_field_grid(
            compiler, scratch)
        failures.extend(grid_failures)
        print(f"FIELD GRID: {grid_passed}/60 runnable cells passed; "
              f"{grid_unavailable}/4 parameter-at-main cells structurally unavailable")
        for name, expected in READ_CASES.items():
            source = FIXTURES / f"{name}.moss"
            try:
                fast = run([compiler, "run", "--interp", str(source)]).stdout.strip()
                native = native_output(compiler, source, scratch, name)
                if fast != expected or native != expected or fast != native:
                    raise RuntimeError(
                        f"expected {expected!r}; Fast Debug={fast!r}; "
                        f"Native={native!r}")
                print(f"PASS {name}: Fast Debug and Native output {fast!r}")
            except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                failures.append(f"{name}: {error}")
                print(f"FAIL {name}: {error}", file=sys.stderr)

        consume = FIXTURES / "field_consume.moss"
        diagnostic = (
            "moss:9: error: [OWNERSHIP_ILLEGAL_CONSUME] "
            "plain field access cannot consume or transfer a non-Copy field; use take or copy")
        for engine, args in (
            ("Fast Debug", [compiler, "run", "--interp", str(consume)]),
            ("Native", [compiler, str(consume), "-o",
                        str(scratch / "field_consume.rs")]),
        ):
            result = invoke(args)
            if result.returncode == 0 or diagnostic not in result.stdout + result.stderr:
                failures.append(
                    f"field_consume {engine}: expected unchanged rejection at line 9, "
                    f"got exit={result.returncode}: {result.stdout}{result.stderr}")
            else:
                print(f"PASS field_consume {engine}: unchanged reject at line 9")

        failures.extend(run_echo_reply_controls(compiler, scratch))
        failures.extend(run_parameter_binding_controls(compiler, scratch))

        for tag, expected in (
            ("swarm_027_indexed_collection_method", True),
            ("swarm_028_self_method_argument_borrow", False),
            ("swarm_030_string_comparison_borrowed", True),
        ):
            source = ROOT / f"tests/{tag}.moss"
            if expected:
                for engine in ("Fast Debug", "Native"):
                    try:
                        expect_step2_map_view(compiler, source, scratch, tag, engine)
                    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                        failures.append(f"{tag} expected Step 2 classification: {error}")
                        print(f"FAIL {tag}: {error}", file=sys.stderr)
                continue
            fast = native = None
            for engine in ("Fast Debug", "Native"):
                try:
                    if engine == "Fast Debug":
                        fast = run([compiler, "run", "--interp", str(source)]).stdout.strip()
                    else:
                        native = native_output(compiler, source, scratch, tag)
                except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
                    combined = str(error)
                    if "CollectionView" in combined or "Map.keys" in combined:
                        failure = (
                            f"{tag} {engine}: separate CollectionView failure, "
                            f"unexpected in this gate: {error}")
                        failures.append(failure)
                        print(f"UNEXPECTED CollectionView failure {failure}", file=sys.stderr)
                    else:
                        failure = f"{tag} {engine}: {error}"
                        failures.append(failure)
                        print(f"FAIL {failure}", file=sys.stderr)
            if fast is not None and native is not None:
                if fast == native:
                    print(f"PASS {tag}: Fast Debug and Native output {fast!r}")
                else:
                    failures.append(
                        f"{tag}: Fast Debug output {fast!r} differs from Native {native!r}")
                    print(f"FAIL {tag}: Fast Debug={fast!r}; Native={native!r}",
                          file=sys.stderr)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    if failures:
        print("\nFAILURES:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"FAIL check_phase28_l8_field_controls: {error}", file=sys.stderr)
        raise SystemExit(1)
