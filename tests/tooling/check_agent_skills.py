#!/usr/bin/env python3
"""Check the repository-owned Codex Moss skills and their live source example."""

from __future__ import annotations

import re
import json
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
AGENTS = ROOT / "AGENTS.md"
MARGO = ROOT / "margo"


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


def agent_document(compiler: Path, command: str) -> dict[str, object]:
    completed = run([str(compiler), "agent", command, "--json"], ROOT)
    if completed.returncode != 0:
        fail(f"agent {command} failed:\n{completed.stdout}{completed.stderr}")
    try:
        document = json.loads(completed.stdout)
    except json.JSONDecodeError as error:
        fail(f"agent {command} emitted invalid JSON: {error}")
    if not document.get("ok"):
        fail(f"agent {command} reported failure: {document}")
    return document


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
            "project_driver": "margo",
            "project_manifest": "Moss.toml",
            "project_lockfile": "Moss.lock",
            "package_resolution": "path-git",
            "module_resolution": "moss-compiler",
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
            "project_driver": "margo",
            "project_manifest": "Moss.toml",
            "project_lockfile": "Moss.lock",
            "package_resolution": "path-git",
            "module_resolution": "moss-compiler",
            "semantic_oracle": "moss-agent-1",
        },
        "moss-agent-workflow",
    )

    agents = AGENTS.read_text(encoding="utf-8")
    for marker in (
        "moss-language",
        "moss-agent-workflow",
        "./moss agent bootstrap --json",
        "run `make` first",
        "./margo build",
        "./margo run",
        "./margo test",
        "./margo bench",
        "./margo clean",
    ):
        if marker not in agents:
            fail(f"AGENTS.md no longer routes a fresh agent to: {marker}")
    if not MARGO.is_file() or not MARGO.stat().st_mode & 0o111:
        fail("repository Margo package driver is unavailable to a fresh agent")

    bootstrap = agent_document(compiler, "bootstrap")["result"]
    capabilities = agent_document(compiler, "capabilities")["result"]
    schema = agent_document(compiler, "schema")["result"]
    if bootstrap["language_version"] != "moss-0.1":
        fail("live bootstrap disagrees with the moss-language version contract")
    if bootstrap["agent_protocol_version"] != 1:
        fail("live bootstrap disagrees with the moss-agent-1 workflow contract")
    capability_names = set(capabilities["capabilities"])
    for capability in (
        "semantic_inspection",
        "semantic_edits",
            "impact_analysis",
            "affected_tests",
            "fast_debug",
            "structured_execution_trace",
        "synchronization_plan",
        "package_project_driver",
        "package_dependencies",
        "package_lockfile",
    ):
        if capability not in capability_names:
            fail(f"live capability discovery omitted {capability}")
    for capability in ("package_project_driver", "package_dependencies", "package_lockfile"):
        if not bootstrap["capability_flags"].get(capability):
            fail(f"bootstrap did not flag {capability} as available")
    catalog = {item["id"]: item for item in capabilities["capability_catalog"]}
    for capability, marker in {
        "package_project_driver": "margo build|run|test|bench|clean",
        "package_dependencies": "Moss.toml",
        "package_lockfile": "Moss.lock",
        "module_interfaces": "margo build --json",
    }.items():
        if capability not in catalog or marker not in catalog[capability].get("entrypoint", ""):
            fail(f"live {capability} discovery lacks current Margo/Moss routing")
    fast_debug = catalog.get("fast_debug", {})
    if not any(command in fast_debug.get("entrypoint", "")
               for command in ("moss run --interp", "moss debug")):
        fail("live fast_debug discovery lacks a current Fast Debug command")
    actions = {item["name"]: item for item in bootstrap["actions"]}
    for action in ("package_build", "package_run", "package_test", "package_bench", "package_clean"):
        if action not in actions or not actions[action]["command"].startswith("margo "):
            fail(f"bootstrap does not expose canonical Margo action {action}")
    if not any("Margo for package/project operations" in step
               for step in bootstrap["recommended_workflow"]):
        fail("bootstrap workflow does not route package work to Margo")
    if not any("Moss owns module/.mossi/semantic truth" in step
               for step in bootstrap["recommended_workflow"]):
        fail("bootstrap workflow does not preserve Moss semantic/module authority")
    schema_names = {item["name"] for item in schema["schema"]["command_schemas"]}
    if "package_project_driver" not in schema_names:
        fail("agent schema does not describe the Margo package driver")
    if "moss agent session-report-template --json" not in " ".join(workflow.split()):
        fail("moss-agent-workflow no longer advertises the session-report command")
    session = agent_document(compiler, "session-report-template")["result"]
    if not session.get("session_report_questions"):
        fail("live session-report-template omitted its structured questions")

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

    print("Moss agent skills passed bootstrap, Margo-discovery, drift, and live-example checks")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"agent skill check failed: {error}", file=sys.stderr)
        raise SystemExit(1)
