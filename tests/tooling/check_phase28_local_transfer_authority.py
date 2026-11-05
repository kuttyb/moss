#!/usr/bin/env python3
"""Check exact last-use transfer authority for loop-local owned vectors."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
TMP = ROOT / "tmp" / "phase28-local-transfer-authority"


def regex_transfer_fixture() -> Path:
    package = TMP / "regex-transfer"
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
        ("char_matches(re.chars[st_id],", "char_matches(copy(re.chars[st_id]),"),
        ("char_in_class(re.classes[st_id],", "char_in_class(copy(re.classes[st_id]),"),
    ]
    for old, new in edits:
        if old not in source:
            raise RuntimeError(f"Regex fixture anchor missing: {old}")
        source = source.replace(old, new)
    (source_dir / "main.moss").write_text(source)
    return source_dir / "main.moss"


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
    if result.returncode:
        raise RuntimeError(f"{label} failed:\n{result.stdout}\n{result.stderr}")


def require_failure(result: subprocess.CompletedProcess[str], label: str) -> None:
    if not result.returncode or "local transfer fact" not in (result.stdout + result.stderr):
        raise RuntimeError(f"{label} did not fail closed on local transfer authority:\n{result.stdout}\n{result.stderr}")


def baseline(compiler: Path, source: Path, mode: str) -> None:
    result = run(compiler, source, mode, {
        "MOSS_TEST_VALIDATE_LOCAL_TRANSFER_CONTRACT": "1",
        "MOSS_TEST_REQUIRE_LOCAL_TRANSFER_COUNT": "2",
        "MOSS_TEST_DUMP_LOCAL_TRANSFER_FACTS": "1",
    })
    require_ok(result, f"{mode} exact local transfer facts")
    facts = [line for line in result.stderr.splitlines()
             if line.startswith("MOSS_LOCAL_TRANSFER_FACT ")]
    if len(facts) != 2:
        raise RuntimeError(f"expected two local transfer facts, got {facts}")
    expected = [(867, 821), (868, 822)]
    for line, (target, source_line) in zip(facts, expected):
        if f"line={target} " not in line or f"@{source_line}:statement" not in line or \
                "source_scope=" not in line or "target_scope=" not in line or \
                "action=0" not in line or "source_owned=1" not in line or \
                "source_dead=1" not in line or "nested_scope=1" not in line or \
                "source_type=\"vector[int]\"" not in line or \
                "target_type=\"vector[int]\"" not in line:
            raise RuntimeError(f"local transfer fact did not bind expected source/target: {line}")


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: check_phase28_local_transfer_authority.py <moss-compiler>")
    compiler = Path(sys.argv[1]).resolve()
    if not compiler.is_file():
        raise SystemExit(f"compiler not found: {compiler}")
    if TMP.exists():
        shutil.rmtree(TMP)
    TMP.mkdir(parents=True)
    source = regex_transfer_fixture()
    common = {
        "MOSS_TEST_VALIDATE_LOCAL_TRANSFER_CONTRACT": "1",
        "MOSS_TEST_REQUIRE_LOCAL_TRANSFER_COUNT": "2",
    }
    baseline(compiler, source, "default")
    require_ok(run(compiler, source, "shadow", common), "shadow local transfer facts")

    corruptions = [
        "source-binding", "source-definition", "target-binding",
        "target-definition", "killed-target-definition", "scope",
        "liveness", "last-use", "rhs-handle", "rhs-span", "rhs-type",
        "context", "specialization", "provenance", "owned",
    ]
    for field in corruptions:
        require_failure(run(compiler, source, "default", {
            **common, "MOSS_TEST_PERTURB_LOCAL_TRANSFER_FACT": field,
        }), f"perturbed {field}")
    require_failure(run(compiler, source, "default", {
        **common, "MOSS_TEST_DROP_LOCAL_TRANSFER_FACT": "1",
    }), "dropped local transfer fact")
    require_failure(run(compiler, source, "default", {
        **common, "MOSS_TEST_DUPLICATE_LOCAL_TRANSFER_FACT": "1",
    }), "duplicate local transfer fact")
    require_failure(run(compiler, source, "shadow", {
        **common, "MOSS_TEST_DROP_LOCAL_TRANSFER_FACT": "1",
    }), "shadow dropped local transfer fact")
    require_failure(run(compiler, source, "shadow", {
        **common, "MOSS_TEST_PERTURB_LOCAL_TRANSFER_FACT": "scope",
    }), "shadow perturbed local transfer fact")
    require_ok(run(compiler, source, "default", {
        "MOSS_TEST_VALIDATE_LOCAL_TRANSFER_CONTRACT": "1",
        "MOSS_TEST_REQUIRE_LOCAL_TRANSFER_COUNT": "0",
        "MOSS_TEST_WITHHOLD_LOCAL_TRANSFER_SOURCE_IDENTITY": "1",
    }), "missing exact source-definition identity")
    require_ok(run(compiler, source, "default", {
        "MOSS_TEST_VALIDATE_LOCAL_TRANSFER_CONTRACT": "1",
        "MOSS_TEST_REQUIRE_LOCAL_TRANSFER_COUNT": "0",
        "MOSS_TEST_FORCE_LOCAL_TRANSFER_SOURCE_LIVE": "1",
    }), "source still live after assignment")
    print("Phase 28 local transfer authority: 23 scenarios PASS (default/shadow)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
