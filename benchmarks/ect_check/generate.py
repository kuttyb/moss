#!/usr/bin/env python3
"""Deterministic generator framework for the ECT check benchmark suite.

Later teams add generators (generic DAGs, edit sequences, worktree layouts,
replay logs) by subclassing `Generator` and appending to `GENERATORS`. The
manifest freezes every fixture's SHA-256 and expected status; fixtures are
never regenerated implicitly.

  generate.py                 write fixtures + manifest (refuses to overwrite)
  generate.py --regenerate    rewrite fixtures + manifest (intentional re-freeze)
  generate.py --verify-determinism   regenerate into tmp and compare to manifest
  generate.py --validate-only        hash every fixture vs manifest, run the
                                     reference compiler, compare expected status
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import random
import shutil
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ect_common as C  # noqa: E402


Generator = C.Generator


class TrivialGenerator(Generator):
    """Smoke fixtures that exercise every part of the harness."""
    name = "trivial"
    version = "1.0.0"
    category = "smoke"

    def generate(self, rng: random.Random, out_dir: Path) -> list[dict]:
        k = rng.randint(2, 9)
        rows = []

        # 1. Single-file positive fixture with an untyped (inferred) generic.
        d = out_dir / "trivial" / "file_pass"
        d.mkdir(parents=True)
        (d / "main.moss").write_text(
            "fn bump(x):\n  return x + 1\n\n"
            "fn twice(x):\n  var y = bump(x)\n  return bump(y)\n\n"
            "fn main():\n"
            f"  echo twice({k})\n  echo twice({k + 1})\n")
        rows.append({"id": "trivial_file_pass", "path": "trivial/file_pass",
                     "kind": "file", "entry": "main.moss",
                     "expected_status": "pass",
                     "metadata": {"shape": "file", "constant": k}})

        # 2. Single-file negative fixture: a deliberate semantic error.
        d = out_dir / "trivial" / "file_fail"
        d.mkdir(parents=True)
        (d / "main.moss").write_text(
            "fn main():\n  echo missing_function(1)\n")
        rows.append({"id": "trivial_file_fail", "path": "trivial/file_fail",
                     "kind": "file", "entry": "main.moss",
                     "expected_status": "fail",
                     "metadata": {"shape": "file", "reason": "unknown symbol"}})

        # 3. Two-package project: an app instantiating a library's untyped
        #    exported generics (the Mossi Leak shape, at size 2).
        d = out_dir / "trivial" / "pkg_pass"
        (d / "lib" / "src").mkdir(parents=True)
        (d / "app" / "src").mkdir(parents=True)
        (d / "lib" / "Moss.toml").write_text(
            '[project]\nname = "ect_trivial_lib"\nversion = "0.1.0"\n')
        (d / "lib" / "src" / "ect_trivial_lib.moss").write_text(
            "module ect_trivial_lib\n\n"
            "export fn double(x):\n  return x + x\n\n"
            "export fn twice_len(items):\n  return items.len() * 2\n\n"
            "fn helper(n):\n  return n + 1\n\n"
            "export fn bump(n):\n  return helper(n)\n")
        (d / "app" / "Moss.toml").write_text(
            '[project]\nname = "ect_trivial_app"\nversion = "0.1.0"\n\n'
            '[dependencies]\nect_trivial_lib = { path = "../lib" }\n')
        (d / "app" / "src" / "main.moss").write_text(
            "module main\nimport ect_trivial_lib\n\n"
            "fn main():\n"
            f"  echo ect_trivial_lib.double({k})\n"
            "  echo ect_trivial_lib.bump(1)\n")
        rows.append({"id": "trivial_pkg_pass", "path": "trivial/pkg_pass",
                     "kind": "package", "entry": "app",
                     "expected_status": "pass",
                     "metadata": {"shape": "package_graph", "packages": 2,
                                  "constant": k}})

        # 4. Failing package: the app references a missing library symbol, so
        #    margo wraps the compiler's JSON error inside its own `error`
        #    string (exercises the embedded-error comparison path).
        d = out_dir / "trivial" / "pkg_fail"
        (d / "lib" / "src").mkdir(parents=True)
        (d / "app" / "src").mkdir(parents=True)
        (d / "lib" / "Moss.toml").write_text(
            '[project]\nname = "ect_failing_lib"\nversion = "0.1.0"\n')
        (d / "lib" / "src" / "ect_failing_lib.moss").write_text(
            "module ect_failing_lib\n\nexport fn double(x):\n  return x + x\n")
        (d / "app" / "Moss.toml").write_text(
            '[project]\nname = "ect_failing_app"\nversion = "0.1.0"\n\n'
            '[dependencies]\nect_failing_lib = { path = "../lib" }\n')
        (d / "app" / "src" / "main.moss").write_text(
            "module main\nimport ect_failing_lib\n\n"
            "fn main():\n  echo ect_failing_lib.not_exported(1)\n")
        rows.append({"id": "trivial_pkg_fail", "path": "trivial/pkg_fail",
                     "kind": "package", "entry": "app",
                     "expected_status": "fail",
                     "metadata": {"shape": "package_graph", "packages": 2,
                                  "reason": "unknown library symbol"}})

        # 5. Package-boundary generics instantiated with distinct user types
        #    (Mossi Leak shape at size 2): untyped exported fns, two user types.
        d = out_dir / "trivial" / "pkg_generic_types"
        (d / "lib" / "src").mkdir(parents=True)
        (d / "app" / "src").mkdir(parents=True)
        (d / "lib" / "Moss.toml").write_text(
            '[project]\nname = "ect_generic_lib"\nversion = "0.1.0"\n')
        (d / "lib" / "src" / "ect_generic_lib.moss").write_text(
            "module ect_generic_lib\n\n"
            "export fn first_of(a, b):\n  return a\n\n"
            "export fn show_id(x):\n  return x.id()\n")
        (d / "app" / "Moss.toml").write_text(
            '[project]\nname = "ect_generic_app"\nversion = "0.1.0"\n\n'
            '[dependencies]\nect_generic_lib = { path = "../lib" }\n')
        (d / "app" / "src" / "main.moss").write_text(
            "module main\nimport ect_generic_lib\n\n"
            "type Alpha:\n  tag: Int\n\n  fn id() -> Int:\n    return tag\n\n"
            "type Beta:\n  tag: Int\n\n  fn id() -> Int:\n    return tag + 100\n\n"
            "fn main():\n"
            f"  echo ect_generic_lib.show_id(ect_generic_lib.first_of(Alpha(tag: {k}), Beta(tag: {k + 1})))\n"
            f"  echo ect_generic_lib.show_id(ect_generic_lib.first_of(Beta(tag: {k}), Alpha(tag: {k + 1})))\n")
        rows.append({"id": "trivial_pkg_generic_types", "path": "trivial/pkg_generic_types",
                     "kind": "package", "entry": "app",
                     "expected_status": "pass",
                     "metadata": {"shape": "package_boundary_generics", "packages": 2,
                                  "user_types": 2}})
        return rows


# Later teams: append generator instances here (generic-DAG and torture-extension
# generators live in dag_generators.py to keep this module small).
from dag_generators import DAG_GENERATORS  # noqa: E402
GENERATORS: list[Generator] = [TrivialGenerator(), *DAG_GENERATORS]


def reject_reserved_names(root: Path) -> None:
    """Fixture content may not use names the harness treats as build output."""
    for item in root.rglob("*"):
        if item.name in C.RESERVED_NAMES:
            raise SystemExit(f"fixture path uses reserved name {item.name!r}: {item}")


def build_rows(out_dir: Path) -> list[dict]:
    rows = []
    for gen in GENERATORS:
        # Each generator gets an independent, name-derived stream so adding a
        # generator never perturbs existing fixtures.
        rng = random.Random(f"{C.SUITE_SEED}:{gen.name}")
        for row in gen.generate(rng, out_dir):
            reject_reserved_names(out_dir / row["path"])
            row.update(category=gen.category, generator=gen.name,
                       generator_version=gen.version, generator_seed=C.SUITE_SEED,
                       sha256=C.tree_sha256(out_dir / row["path"]))
            rows.append(row)
    return rows


def write_manifest(rows: list[dict]) -> None:
    ids = [r["id"] for r in rows]
    assert len(ids) == len(set(ids)), "duplicate fixture ids"
    manifest = {"suite_name": C.SUITE_NAME, "version": C.SUITE_VERSION,
                "seed": C.SUITE_SEED, "total_fixtures": len(rows),
                "generators": {g.name: g.version for g in GENERATORS},
                "fixtures": rows}
    C.MANIFEST_PATH.write_text(json.dumps(manifest, indent=2) + "\n")


def cmd_generate(regenerate: bool) -> int:
    if C.MANIFEST_PATH.exists() and not regenerate:
        print("manifest.json exists; fixtures are frozen. Use --regenerate to re-freeze.",
              file=sys.stderr)
        return 2
    if C.FIXTURES_DIR.exists():
        shutil.rmtree(C.FIXTURES_DIR)
    C.FIXTURES_DIR.mkdir(parents=True)
    rows = build_rows(C.FIXTURES_DIR)
    write_manifest(rows)
    print(f"wrote {len(rows)} fixtures")
    return 0


def cmd_verify_determinism() -> int:
    scratch = C.TMP_ROOT / "determinism"
    if scratch.exists():
        shutil.rmtree(scratch)
    scratch.mkdir(parents=True)
    rows = build_rows(scratch)
    frozen = {f["id"]: f["sha256"] for f in C.load_manifest()["fixtures"]}
    bad = [r["id"] for r in rows if frozen.get(r["id"]) != r["sha256"]]
    for fid in sorted(set(frozen) - {r["id"] for r in rows}):
        bad.append(fid)
    if bad:
        print(f"NON-DETERMINISTIC or drifted: {bad}", file=sys.stderr)
        return 1
    print(f"determinism ok: {len(rows)} fixtures regenerate byte-identically")
    return 0


def cmd_validate(moss: Path, margo: Path, compile_check: bool) -> int:
    manifest = C.load_manifest()
    failures = []
    tc = C.Toolchain(moss, margo) if compile_check else None
    for fx in manifest["fixtures"]:
        actual = C.tree_sha256(C.FIXTURES_DIR / fx["path"])
        line = f"{fx['id']}: sha256 {'ok' if actual == fx['sha256'] else 'MISMATCH'}"
        if actual != fx["sha256"]:
            failures.append(f"{fx['id']}: sha256 mismatch")
        if compile_check:
            work = C.TMP_ROOT / "validate" / fx["id"]
            values = C.stage_fixture(fx, work)
            argv = C.command_for(fx, {**tc.values(), **values}, None)
            if fx.get("provider"):
                C.build_provider(fx, tc, work)
            res = C.run_measured(argv, cwd=work, env=tc.env(),
                                 timeout=C.fixture_timeout(fx), stack_kib=C.fixture_stack(fx))
            verdict = C.extract_verdict(res, C.path_replacements(work, tc))
            status = C.status_of(verdict)
            if not C.fixture_ok(fx, verdict):
                failures.append(f"{fx['id']}: exit {verdict.get('exit_code')} status {status} "
                                f"timed_out={verdict.get('timed_out')} does not satisfy the manifest")
            line += f", status {status} (expected {fx['expected_status']})"
        print(line)
    if failures:
        print("VALIDATION FAILED:\n  " + "\n  ".join(failures), file=sys.stderr)
        return 1
    print(f"validated {len(manifest['fixtures'])} fixtures")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--validate-only", action="store_true")
    ap.add_argument("--verify-determinism", action="store_true")
    ap.add_argument("--regenerate", action="store_true")
    ap.add_argument("--no-compile", action="store_true",
                    help="with --validate-only: hash check only, no compiler")
    ap.add_argument("--moss", default=str(C.DEFAULT_MOSS))
    ap.add_argument("--margo", default=str(C.DEFAULT_MARGO))
    args = ap.parse_args()
    if args.validate_only:
        return cmd_validate(Path(args.moss), Path(args.margo), not args.no_compile)
    if args.verify_determinism:
        return cmd_verify_determinism()
    return cmd_generate(args.regenerate)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except C.ToolError as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(2)
