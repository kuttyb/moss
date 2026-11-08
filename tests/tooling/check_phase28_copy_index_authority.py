#!/usr/bin/env python3
"""Check that copy authority is emitted only for exact checked EST handles."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / "tmp" / "phase28-copy-index-authority"


def run(compiler: Path, source: Path, mode: str, hooks: dict[str, str]) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env.pop("MOSS_EST", None)
    env.update(hooks)
    if mode == "shadow":
        env["MOSS_EST"] = "shadow"
    return subprocess.run(
        [str(compiler), "check", str(source), "--json"],
        cwd=ROOT,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def require_ok(result: subprocess.CompletedProcess[str], label: str) -> None:
    if result.returncode != 0:
        raise RuntimeError(f"{label} failed ({result.returncode}):\n{result.stdout}\n{result.stderr}")


def require_failure(result: subprocess.CompletedProcess[str], label: str) -> None:
    if result.returncode == 0:
        raise RuntimeError(f"{label} unexpectedly passed:\n{result.stdout}")
    if "checked copy fact" not in (result.stdout + result.stderr):
        raise RuntimeError(f"{label} failed for the wrong reason:\n{result.stdout}\n{result.stderr}")


def package_backed_copy_fixture() -> Path:
    package = TMP / "regex-copy"
    source_dir = package / "src"
    source_dir.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / "packages" / "regex" / "Moss.toml", package / "Moss.toml")
    source = (ROOT / "packages" / "regex" / "src" / "main.moss").read_text()
    edits = [
        ("push_str(chars_list, raw_chars[r_idx])", "push_str(chars_list, copy(raw_chars[r_idx]))"),
        ("push_str(classes_list, raw_classes[r_idx])", "push_str(classes_list, copy(raw_classes[r_idx]))"),
        ("tk, tc, tclass, tneg)", "tk, copy(tc), copy(tclass), tneg)"),
        ("push_str(st_chars, pc)", "push_str(st_chars, copy(pc))"),
        ("push_str(st_classes, pcl)", "push_str(st_classes, copy(pcl))"),
        ("char_matches(re.chars[st_id],", "let state_char = copy(re.chars[st_id])"),
        ("char_in_class(re.classes[st_id],", "let class_chars = copy(re.classes[st_id])"),
    ]
    for old, new in edits:
        if old in source:
            source = source.replace(old, new)
        elif new not in source:
            raise RuntimeError(f"Regex copy fixture source anchor missing: {old}")
    (source_dir / "main.moss").write_text(source)
    return source_dir / "main.moss"


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_phase28_copy_index_authority.py <moss-compiler>")
    compiler = Path(sys.argv[1]).resolve()
    if not compiler.is_file():
        raise SystemExit(f"compiler not found: {compiler}")
    if TMP.exists():
        shutil.rmtree(TMP)
    TMP.mkdir(parents=True)
    standalone = ROOT / "tests" / "phase28" / "r7_010" / "team_a" / "estless_indexed_copy.moss"
    package_source = package_backed_copy_fixture()
    map_key_sources = (
        ROOT / "examples" / "swarm" / "Python" / "Counter" / "src" / "main.moss",
        ROOT / "examples" / "swarm" / "Surface" / "repros" / "swarm_061b_map_get_before_write.moss",
        ROOT / "examples" / "swarm" / "modules" / "text_toolkit" / "toolkit" / "src" / "stats.moss",
    )

    for mode in ("default", "shadow"):
        require_ok(run(compiler, standalone, mode, {
            "MOSS_TEST_VALIDATE_COPY_CONTRACT": "1",
            "MOSS_TEST_ASSERT_NO_COPY_REQUIREMENT": "1",
        }), f"{mode} EST-less standalone check")

        common = {
            "MOSS_TEST_VALIDATE_COPY_CONTRACT": "1",
            "MOSS_TEST_REQUIRE_COPY_FACT": "1",
        }
        require_ok(run(compiler, package_source, mode, common),
                   f"{mode} package-backed indexed copy authority")
        require_failure(run(compiler, package_source, mode, {
            **common, "MOSS_TEST_DROP_COPY_FACT": "1",
        }), f"{mode} dropped indexed copy fact")
        require_failure(run(compiler, package_source, mode, {
            **common, "MOSS_TEST_PERTURB_COPY_FACT": "1",
        }), f"{mode} perturbed indexed copy fact")

        for map_key_source in map_key_sources:
            source_label = map_key_source.relative_to(ROOT)
            require_ok(run(compiler, map_key_source, mode, common),
                       f"{mode} explicit Map-key copy authority: {source_label}")
            for mutation, label in (
                ("MOSS_TEST_DROP_COPY_FACT", "dropped"),
                ("MOSS_TEST_DUPLICATE_COPY_FACT", "duplicated"),
                ("MOSS_TEST_PERTURB_COPY_FACT", "perturbed"),
            ):
                require_failure(run(compiler, map_key_source, mode, {
                    **common, mutation: "1",
                }), f"{mode} {label} Map-key copy fact: {source_label}")

    print("Phase 28 indexed-copy authority: 32 scenarios PASS (default/shadow)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
