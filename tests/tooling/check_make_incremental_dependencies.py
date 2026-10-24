#!/usr/bin/env python3
"""Exercise compiler object and depfile behavior in an isolated tiny project."""
from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[2]
SCRATCH = ROOT / "tmp"


def make(project: Path, *args: str) -> None:
    result = subprocess.run(
        ["make", "--no-print-directory", *args,
         "COMPILER_SOURCES=src/moss.cpp"], cwd=project, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
    )
    if result.returncode:
        raise AssertionError(f"make {' '.join(args)} failed:\n{result.stdout}")


def snapshot(paths: list[Path]) -> dict[Path, int]:
    return {path: path.stat().st_mtime_ns for path in paths if path.exists()}


def assert_all_changed(before: dict[Path, int], after: dict[Path, int],
                       paths: list[Path], label: str) -> None:
    unchanged = [p for p in paths if p not in before or p not in after or before[p] == after[p]]
    if unchanged:
        raise AssertionError(f"{label} did not rebuild every expected output: {unchanged}")


def assert_unchanged(before: dict[Path, int], after: dict[Path, int],
                     paths: list[Path], label: str) -> None:
    changed = [p for p in paths if before.get(p) != after.get(p)]
    if changed:
        raise AssertionError(f"{label} unexpectedly rebuilt outputs: {changed}")


def main() -> None:
    SCRATCH.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="make-deps-", dir=SCRATCH) as temp:
        project = Path(temp)
        shutil.copy2(ROOT / "Makefile", project / "Makefile")
        source = project / "src"
        nested = source / "nested"
        nested.mkdir(parents=True)
        (source / "moss.cpp").write_text(
            '#include "base.hpp"\nint main() { return base_value() == 42 ? 0 : 1; }\n'
        )
        (source / "base.hpp").write_text(
            '#pragma once\n#include "nested/bridge.inc"\n'
            'inline int base_value() { return BRIDGE_VALUE; }\n'
        )
        bridge = nested / "bridge.inc"
        bridge.write_text("#define BRIDGE_VALUE 42\n")
        unrelated = source / "unrelated.hpp"
        unrelated.write_text("#pragma once\n#define UNRELATED_VALUE 1\n")

        # Request both profiles in parallel and verify the executables run.
        make(project, "--jobs=8", "moss-dev", "moss-opt")
        dev = project / "build/compiler/moss-dev"
        opt = project / "build/compiler/moss-opt"
        wrapper = project / "moss"
        for binary in (dev, opt, wrapper):
            if not binary.is_file():
                raise AssertionError(f"missing output {binary}")
            subprocess.run([binary], check=True)

        dev_object = next((project / "build/compiler/obj").glob("dev-*/src/moss.o"))
        opt_object = next((project / "build/compiler/obj").glob("opt-*/src/moss.o"))
        dev_dep, opt_dep = dev_object.with_suffix(".d"), opt_object.with_suffix(".d")
        for dep in (dev_dep, opt_dep):
            contents = dep.read_text()
            if "base.hpp" not in contents or "bridge.inc" not in contents:
                raise AssertionError(f"direct/transitive dependencies missing from {dep}")
        tracked = [dev, opt, dev_object, opt_object]

        before = snapshot(tracked)
        time.sleep(0.02)
        make(project, "--jobs=8", "moss-dev", "moss-opt")
        assert_unchanged(before, snapshot(tracked), tracked, "no-op build")

        for label, path, contents in (
            ("implementation edit", source / "moss.cpp", "// implementation edit\n"),
            ("direct header edit", source / "base.hpp", "// direct header edit\n"),
            ("transitive header edit", bridge, "// transitive header edit\n"),
        ):
            before = snapshot(tracked)
            with path.open("a") as stream:
                stream.write(contents)
            make(project, "--jobs=8", "moss-dev", "moss-opt")
            assert_all_changed(before, snapshot(tracked), tracked, label)

        # Adding a new transitive include updates the depfiles and drops the old one.
        leaf = nested / "new_leaf.inc"
        leaf.write_text("#define NEW_VALUE 42\n")
        (source / "base.hpp").write_text(
            '#pragma once\n#include "nested/new_leaf.inc"\n'
            'inline int base_value() { return NEW_VALUE; }\n'
        )
        before = snapshot(tracked)
        make(project, "--jobs=8", "moss-dev", "moss-opt")
        assert_all_changed(before, snapshot(tracked), tracked, "new dependency")
        for dep in (dev_dep, opt_dep):
            contents = dep.read_text()
            if "new_leaf.inc" not in contents or "bridge.inc" in contents:
                raise AssertionError(f"depfile did not update include edges: {dep}")
        bridge.unlink()
        before = snapshot(tracked)
        time.sleep(0.02)
        make(project, "--jobs=8", "moss-dev", "moss-opt")
        assert_unchanged(before, snapshot(tracked), tracked, "removed dependency")

        before = snapshot(tracked)
        with unrelated.open("a") as stream:
            stream.write("// unrelated header edit\n")
        make(project, "--jobs=8", "moss-dev", "moss-opt")
        assert_unchanged(before, snapshot(tracked), tracked, "unrelated header edit")

        # Removing a depfile or object must rebuild and relink that profile.
        before = snapshot([dev, dev_object])
        dev_dep.unlink()
        make(project, "--jobs=8", "moss-dev", "moss-opt")
        assert_all_changed(before, snapshot([dev, dev_object]), [dev, dev_object], "deleted depfile")
        if not dev_dep.is_file():
            raise AssertionError("missing depfile was not regenerated")

        before = snapshot([opt, opt_object])
        opt_object.unlink()
        make(project, "--jobs=8", "moss-dev", "moss-opt")
        assert_all_changed(before, snapshot([opt, opt_object]), [opt, opt_object], "deleted object")
        if not opt_object.is_file():
            raise AssertionError("missing object was not rebuilt")

        # A configuration change selects a new object directory.
        flags = "-std=c++17 -Wall -Wextra -pedantic -O0 -DPROFILE_VARIANT=1"
        make(project, "moss-dev", f"CXXFLAGS_DEV={flags}")
        configured = list((project / "build/compiler/obj").glob("dev-*/src/moss.o"))
        if len(configured) != 2:
            raise AssertionError("changed flags reused the old object directory")

        # Switching to the explicit GCC compatibility override must also pick
        # a separate object tree instead of reusing Clang-built objects.
        make(project, "moss-dev", "CXX=g++")
        gcc_objects = list((project / "build/compiler/obj").glob("dev-*/src/moss.o"))
        if len(gcc_objects) != 3:
            raise AssertionError("compiler override reused an existing object directory")
        if "dev-" not in str(gcc_objects[-1].parent.parent):
            raise AssertionError("GCC override did not create a development object tree")

        make(project, "clean")
        if dev.exists() or opt.exists() or wrapper.exists():
            raise AssertionError("clean left compiler outputs behind")
        make(project, "--jobs=8", "moss-dev", "moss-opt")
        if not all(path.is_file() for path in (dev, opt, wrapper)):
            raise AssertionError("clean rebuild did not restore both profiles")

    print("Makefile incremental dependency checks passed")


if __name__ == "__main__":
    main()
