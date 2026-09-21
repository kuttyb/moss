#!/usr/bin/env python3
"""Check the repository-owned Codex Moss skills and their live source example."""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SKILLS = ROOT / ".agents" / "skills"
LANGUAGE = SKILLS / "moss-language" / "SKILL.md"
WORKFLOW = SKILLS / "moss-agent-workflow" / "SKILL.md"
EXAMPLE = ROOT / "tests" / "tooling" / "fixtures" / "moss_language_skill_example.moss"


def fail(message: str) -> None:
    raise AssertionError(message)


def front_matter(text: str) -> str:
    match = re.match(r"\A---\n(.*?)\n---\n", text, re.DOTALL)
    if not match:
        fail("skill has no YAML front matter")
    return match.group(1)


def require_markers(text: str, markers: dict[str, str], label: str) -> None:
    for key, value in markers.items():
        needle = f"  {key}: {value}"
        if needle not in text:
            fail(f"{label} drifted: missing contract marker {key}: {value}")


def extract_valid_example(text: str) -> str:
    match = re.search(
        r"<!-- moss-skill-valid-example:start -->\n```moss\n(.*?)\n```\n"
        r"<!-- moss-skill-valid-example:end -->",
        text,
        re.DOTALL,
    )
    if not match:
        fail("moss-language has no designated valid Moss example")
    return match.group(1).strip() + "\n"


def run(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, text=True, capture_output=True, check=False)


def main() -> int:
    compiler = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "moss"
    if not compiler.is_absolute():
        compiler = (ROOT / compiler).resolve()
    if not compiler.exists():
        fail(f"Moss compiler not found: {compiler}")

    for path, name in ((LANGUAGE, "moss-language"), (WORKFLOW, "moss-agent-workflow")):
        if not path.is_file():
            fail(f"missing repository skill: {path.relative_to(ROOT)}")
        metadata = front_matter(path.read_text(encoding="utf-8"))
        if f"name: {name}" not in metadata:
            fail(f"{name} metadata has an unexpected name")
        if "description:" not in metadata:
            fail(f"{name} metadata has no discovery description")
        if "language: moss-0.1" not in metadata or 'skill-version: "1"' not in metadata:
            fail(f"{name} metadata omits its Moss/skill version")

    language = LANGUAGE.read_text(encoding="utf-8")
    workflow = WORKFLOW.read_text(encoding="utf-8")
    require_markers(
        language,
        {
            "language_version": "moss-0.1",
            "skill_version": "1",
            "message": "synchronous-blocking",
            "await": "retired",
            "spawn": "retired",
            "domain_handles": "routing-capabilities",
            "self_send": "forbidden",
            "same_domain_message": "forbidden",
            "locks": "compiler-derived",
            "traits": "structural-compile-time",
            "parameter_effects": "inferred-read-write-consume",
            "recursion": "unsupported",
            "general_first_class_closures": "unsupported",
            "legacy_runtime_model": "forbidden",
        },
        "moss-language",
    )
    require_markers(
        workflow,
        {
            "language_version": "moss-0.1",
            "skill_version": "1",
            "bootstrap": "moss-agent-1",
            "diagnostics": "structured-json",
            "source_of_truth": "compiler",
            "generated_rust": "implementation-artifact",
            "fast_debug": "checked-moss-interpreter",
            "trace": "newline-delimited-json",
        },
        "moss-agent-workflow",
    )

    lower_language = language.lower()
    for obsolete in ("sender fifo", "total commit order", "worker queue", "mailbox"):
        if obsolete in lower_language:
            fail(f"moss-language teaches obsolete runtime wording: {obsolete}")

    if extract_valid_example(language) != EXAMPLE.read_text(encoding="utf-8"):
        fail("moss-language's advertised valid example differs from its checked fixture")

    check = run([str(compiler), "--check", str(EXAMPLE)], ROOT)
    if check.returncode != 0:
        fail(f"skill example failed Moss checking:\n{check.stdout}{check.stderr}")

    tmp_root = ROOT / "tmp"
    tmp_root.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="moss-agent-skills-", dir=tmp_root) as directory:
        directory_path = Path(directory)
        generated = directory_path / "skill_example.rs"
        native = directory_path / "skill_example"
        generate = run([str(compiler), str(EXAMPLE), "-o", str(generated)], ROOT)
        if generate.returncode != 0:
            fail(f"skill example failed Moss lowering:\n{generate.stdout}{generate.stderr}")
        rustc = shutil.which("rustc")
        if rustc is None:
            fail("rustc is required to validate the advertised Moss skill example")
        native_build = run([rustc, "-D", "warnings", str(generated), "-o", str(native)], ROOT)
        if native_build.returncode != 0:
            fail(f"skill example generated Rust failed strict compilation:\n{native_build.stdout}{native_build.stderr}")
        executed = run([str(native)], ROOT)
        if executed.returncode != 0 or executed.stdout != "5\n":
            fail(f"skill example produced unexpected output: {executed.stdout}{executed.stderr}")

    print("Moss agent skills passed discovery, drift, and live-example checks")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"agent skill check failed: {error}", file=sys.stderr)
        raise SystemExit(1)
