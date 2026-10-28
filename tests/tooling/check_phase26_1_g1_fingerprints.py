#!/usr/bin/env python3
"""G1 package fingerprints, check-only interfaces, and native relinking."""
from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
MOSS = Path(sys.argv[1]).resolve()
MARGO = ROOT / "margo"
SCRATCH = ROOT / "tmp/phase26-1-g1-fingerprints"


def run(argv, cwd: Path, env: dict[str, str], *, ok: bool = True):
    result = subprocess.run([str(arg) for arg in argv], cwd=cwd, env=env,
                            capture_output=True, text=True, timeout=180)
    assert (result.returncode == 0) == ok, (
        argv, result.returncode, result.stdout, result.stderr)
    return result


def make_package(root: Path, name: str, module: str, body: str,
                 dependency: tuple[str, str] | None = None) -> Path:
    (root / "src").mkdir(parents=True, exist_ok=True)
    manifest = ["[project]", f'name = "{name}"', 'version = "0.1.0"', ""]
    if dependency:
        manifest += ["[dependencies]",
                     f'{dependency[0]} = {{ path = "{dependency[1]}" }}', ""]
    (root / "Moss.toml").write_text("\n".join(manifest))
    (root / "src" / f"{module}.moss").write_text(
        f"module {module}\n{body}")
    return root


def main() -> int:
    shutil.rmtree(SCRATCH, ignore_errors=True)
    SCRATCH.mkdir(parents=True)
    base = make_package(SCRATCH / "semantic" / "base", "g1_base", "g1_base",
        "export fn value():\n  return 1\n"
        "export fn inferred_anchor():\n  return 1\n"
        "export fn explicit_anchor() -> Int:\n  return 1\n")
    middle = make_package(SCRATCH / "semantic" / "middle", "g1_middle", "g1_middle",
        "import g1_base\n"
        "export fn value():\n  return g1_base.value()\n"
        "export fn explicit_forwarded():\n  return g1_base.explicit_anchor()\n",
        ("g1_base", "../base"))
    app = make_package(SCRATCH / "semantic" / "app", "g1_app", "g1_app",
        "import g1_middle\n"
        "export fn forwarded():\n  return g1_middle.value()\n"
        "export fn explicit_forwarded():\n  return g1_middle.explicit_forwarded()\n"
        "fn main():\n  echo forwarded()\n",
        ("g1_middle", "../middle"))

    rustc_marker = SCRATCH / "rustc-called.log"
    fake_rustc = SCRATCH / "rustc-must-not-run"
    fake_rustc.write_text(
        '#!/bin/sh\necho invoked >> "$RUSTC_MARKER"\nexit 97\n')
    fake_rustc.chmod(0o755)
    env = dict(os.environ)
    env.update({"MOSS": str(MOSS), "MARGO_HOME": str(SCRATCH / "margo-home"),
                "MOSS_STAGED_ABI": "v10", "RUSTC": str(fake_rustc),
                "RUSTC_MARKER": str(rustc_marker)})
    command = [MARGO, "build", "--check", "--json"]

    def check(expected: list[str]) -> dict:
        payload = json.loads(run(command, app, env).stdout)
        assert payload["ok"] and payload["check_only"] is True, payload
        assert payload["checked"] == expected, payload
        assert not rustc_marker.exists(), rustc_marker.read_text()
        return payload

    check(["g1_base", "g1_middle", "g1_app"])
    assert check([])["skipped"] == ["g1_base", "g1_middle", "g1_app"]
    for package, module in ((base, "g1_base"), (middle, "g1_middle"),
                            (app, "g1_app")):
        interface = package / "build/check-debug" / f"{module}.mossi"
        assert interface.is_file(), interface
        assert "package_source_hash sha256:" in interface.read_text()
    assert not list((SCRATCH / "semantic").rglob("*.rs"))
    assert not list((SCRATCH / "semantic").rglob("*.rlib"))

    # Semantic checks consume only fresh .mossi data; check-only interfaces do
    # not need an rlib even for staged ABI-v10.
    check_env = dict(env)
    check_env["MOSS_MODULE_PATH"] = os.pathsep.join(
        str(package / "build/check-debug") for package in (base, middle))
    checked = run([MOSS, "check", app / "src/g1_app.moss", "--json"], app,
                  check_env)
    assert json.loads(checked.stdout)["ok"] is True

    source = base / "src/g1_base.moss"
    original = source.read_text()

    # E0: mtime only. E1: implementation-only concrete body change.
    source.touch()
    check([])
    source.write_text(original.replace("export fn value():\n  return 1",
                                       "export fn value():\n  return 1 + 0", 1))
    check(["g1_base"])
    source.write_text(original)
    check(["g1_base"])

    # E2 changes an inferred export not used by dependents: check direct
    # dependents, then stop when their semantic interface hashes stay equal.
    source.write_text(original.replace("export fn inferred_anchor():\n  return 1",
                                       'export fn inferred_anchor():\n  return "changed"', 1))
    check(["g1_base", "g1_middle"])
    source.write_text(original)
    check(["g1_base", "g1_middle"])

    # E3 changes a used explicit export and propagates through the full chain.
    source.write_text(original.replace(
        "export fn explicit_anchor() -> Int:\n  return 1",
        "export fn explicit_anchor() -> Float:\n  return 1.0", 1))
    check(["g1_base", "g1_middle", "g1_app"])
    source.write_text(original)
    check(["g1_base", "g1_middle", "g1_app"])

    # A package without an explicit module still gets an empty, freshness-
    # bearing package interface in check-only mode.
    legacy = SCRATCH / "legacy"
    (legacy / "src").mkdir(parents=True)
    (legacy / "Moss.toml").write_text(
        '[project]\nname = "g1_legacy"\nversion = "0.1.0"\n')
    (legacy / "src/main.moss").write_text("fn main():\n  echo 42\n")
    legacy_result = json.loads(run(command, legacy, env).stdout)
    assert legacy_result["checked"] == ["g1_legacy"], legacy_result
    assert (legacy / "build/check-debug/g1_legacy.mossi").is_file()
    assert not rustc_marker.exists(), rustc_marker.read_text()

    # Native fingerprints also observe provider binaries. A concrete body edit
    # keeps the semantic interface stable, but the consumer must relink to
    # embed the new provider rlib.
    native = SCRATCH / "native"
    native_base = make_package(native / "base", "g1_native_base", "g1_native_base",
        "export fn value() -> Int:\n  return 42\n")
    native_app = make_package(native / "app", "g1_native_app", "g1_native_app",
        "import g1_native_base\nfn main():\n  echo g1_native_base.value()\n",
        ("g1_native_base", "../base"))
    native_env = dict(os.environ, MOSS=str(MOSS),
                      MARGO_HOME=str(SCRATCH / "native-margo-home"))
    native_env.pop("MOSS_STAGED_ABI", None)
    native_env.pop("MOSS_STAGED_STRING_REPR", None)
    run([MARGO, "run"], native_app, native_env)
    executable = native_app / "build/debug/g1_native_app"
    assert run([executable], native_app, native_env).stdout == "42\n"
    source = native_base / "src/g1_native_base.moss"
    source.write_text(source.read_text().replace("return 42", "return 43", 1))
    run([MARGO, "run"], native_app, native_env)
    assert run([executable], native_app, native_env).stdout == "43\n"

    print("Phase 26.1 G1 check-only interfaces, fingerprints, checkstop, ABI-v10, and native relinking passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
