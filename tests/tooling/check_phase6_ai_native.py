#!/usr/bin/env python3
"""Focused Phase 6B--6J checks over the public Moss agent workflow."""

from __future__ import annotations

import json
from pathlib import Path
import shutil
import subprocess
import sys


def run(compiler: Path, project: Path, *args: str, expected: int = 0) -> dict:
    result = subprocess.run(
        [str(compiler), *args], cwd=project, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
    )
    if result.returncode != expected:
        raise AssertionError(
            f"{' '.join(args)} returned {result.returncode}, expected {expected}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise AssertionError(f"invalid JSON from {' '.join(args)}: {error}")


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: check_phase6_ai_native.py <moss> <scratch-dir>")
    compiler = Path(sys.argv[1]).resolve()
    root = Path(__file__).resolve().parents[2]
    scratch = Path(sys.argv[2]).resolve() / "phase6-ai-native"
    if scratch.exists():
        shutil.rmtree(scratch)
    scratch.mkdir(parents=True)
    project = scratch / "demo"
    shutil.copytree(root / "examples/projects/phase7_demo", project)

    bootstrap = run(compiler, project, "agent", "bootstrap", "--json")
    flags = bootstrap["result"]["capability_flags"]
    for name in ("impact_analysis", "affected_tests", "formatter",
                 "semantic_edits", "repair_actions", "cost_facts"):
        assert flags[name] is True
    template = run(compiler, project, "agent", "session-report-template", "--json")
    assert len(template["result"]["session_report_questions"]) >= 5

    checked = run(compiler, project, "check", "src/main.moss", "--json")
    assert checked["ok"] is True
    run(compiler, project, "test", "--json")
    unchanged = run(compiler, project, "impact", "fn:add", "--json")
    assert unchanged["result"]["change_kind"] == "unchanged"
    assert unchanged["result"]["changed_semantic_unit"]["durable_identity"] == \
        "entity-v1:function:add"
    assert unchanged["result"]["incremental"]["units_reused"] > 0

    source = project / "src/main.moss"
    source.write_text(source.read_text(encoding="utf-8").replace(
        "  left + right\n", "  left + right + 0\n"), encoding="utf-8")
    implementation = run(compiler, project, "impact", "fn:add", "--json")
    assert implementation["result"]["change_kind"] == "implementation_only"
    assert "entity-v1:test:src/main.moss:addition" in \
        implementation["result"]["affected_tests"]
    affected = run(compiler, project, "test", "--affected", "--json")
    assert affected["result"]["affected_only"] is True
    assert [item["name"] for item in affected["result"]["tests"]] == ["addition"]
    assert len(affected["result"]["selection"]["skipped"]) == 1

    source.write_text(source.read_text(encoding="utf-8").replace(
        "  left + right + 0\n", "  echo left\n  left + right + 0\n"),
        encoding="utf-8")
    interface = run(compiler, project, "impact", "fn:add", "--json")
    assert interface["result"]["change_kind"] == "semantic_interface_change"
    assert interface["result"]["invalidated_dependents"]
    cost = run(compiler, project, "cost", "fn:add", "--source",
               "src/main.moss", "--json")
    assert "cost_facts" in cost["result"]

    source.write_text(source.read_text(encoding="utf-8") + "\n", encoding="utf-8")
    assert run(compiler, project, "fmt", "--check", "--json",
               expected=1)["ok"] is False
    run(compiler, project, "fmt", "--json")
    first = source.read_bytes()
    run(compiler, project, "fmt", "--json")
    assert source.read_bytes() == first

    renamed = scratch / "renamed"
    shutil.copytree(root / "examples/projects/phase7_demo", renamed)
    edit = run(compiler, renamed, "edit", "rename", "entity-v1:function:add",
               "plus", "--json")
    assert edit["result"]["resulting_target"]["durable_identity"] == \
        "entity-v1:function:plus"
    assert "plus(2, 3)" in (renamed / "src/main.moss").read_text(encoding="utf-8")
    stale = run(compiler, renamed, "edit", "rename",
                "entity-v1:function:add", "again", "--json", expected=1)
    assert stale["error"]["code"] == "EDIT_TARGET_STALE"

    malformed = scratch / "malformed"
    malformed.mkdir()
    (malformed / "moss.toml").write_text(
        '[project]\nname = "malformed"\nversion = "0.1.0"\n\n'
        '[build]\nsource = "src"\n', encoding="utf-8")
    (malformed / "src").mkdir()
    bad_source = malformed / "src/main.moss"
    bad_source.write_text('test "broken"\n  assert(true)\n', encoding="utf-8")
    repair = run(compiler, malformed, "check", "src/main.moss", "--json",
                 expected=1)
    assert repair["error"]["fixes"][0]["kind"] == "add_block_colon"
    print("Phase 6 AI-native impact, formatter, edit, repair, and cost checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
