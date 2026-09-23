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
README = ROOT / "README.md"
AGENT_SKILLS_DOC = ROOT / "docs" / "AGENT_SKILLS.md"
AGENT_API_DOC = ROOT / "docs" / "AGENT_API.md"
EXAMPLE = ROOT / "tests" / "tooling" / "fixtures" / "moss_language_skill_example.moss"
SOURCE_SURFACE = ROOT / "tests" / "tooling" / "fixtures" / "moss_language_surface.moss"
IMMUTABLE_LET = ROOT / "tests" / "tooling" / "fixtures" / "moss_language_surface_immutable_let.moss"
LOCAL_ANNOTATION = ROOT / "tests" / "tooling" / "fixtures" / "moss_language_surface_local_annotation.moss"
FAST_DEBUG_CONTAINER_FIELD = ROOT / "tests" / "tooling" / "fixtures" / "moss_fast_debug_container_field_limit.moss"
DISCOVERY_COLLECTIONS = ROOT / "tests" / "tooling" / "fixtures" / "moss_discovery_collections.moss"
COMPOSITION_PURE_HELPER = ROOT / "tests" / "tooling" / "fixtures" / "moss_composition_initializer_pure_helper.moss"
TEST_COMPOSITION_ROOT_REJECTED = ROOT / "tests" / "tooling" / "fixtures" / "moss_test_domain_composition_root_rejected.moss"
IMPURE_COMPOSITION_INITIALIZER = ROOT / "tests" / "negative" / "phase106b_impure_state_initializer.moss"
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
    readme = README.read_text(encoding="utf-8")
    agent_skills_doc = AGENT_SKILLS_DOC.read_text(encoding="utf-8")
    agent_api_doc = AGENT_API_DOC.read_text(encoding="utf-8")
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
            "source_surface": "bootstrap-discoverable",
            "canonical_docs": "bootstrap-discoverable",
            "project_surface": "bootstrap-discoverable",
            "collection_operations": "bootstrap-discoverable",
            "test_domain_topology": "bootstrap-discoverable",
            "composition_initializers": "bootstrap-discoverable",
            "domain_fn": "handler",
            "explicit_let": "immutable",
            "explicit_var": "mutable",
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
            "source_surface": "bootstrap-discoverable",
            "canonical_docs": "bootstrap-discoverable",
            "project_surface": "bootstrap-discoverable",
            "collection_operations": "bootstrap-discoverable",
            "test_domain_topology": "bootstrap-discoverable",
            "composition_initializers": "bootstrap-discoverable",
            "gap_classification": "minimal-reproducer-first",
        },
        "moss-agent-workflow",
    )

    for marker in (
        "Ensure `./moss` exists; run `make` if it does not.",
        "Run `./moss agent bootstrap --json`.",
        "Read its `tool_invocation`",
        "Use `./margo` for package/project work",
        "prefer `./moss` and `./margo`",
        "Bare `moss` and\n`margo` are valid when",
    ):
        if marker not in workflow:
            fail(f"moss-agent-workflow no longer exposes invocation contract: {marker}")

    onboarding = readme.split("### Coding-agent onboarding", 1)[1]
    loop = onboarding.split("```sh", 1)[1].split("```", 1)[0].strip().splitlines()
    if not loop or loop[0] != "./moss agent bootstrap --json":
        fail("README coding-agent onboarding no longer starts with ./moss bootstrap")
    if "live ./moss agent bootstrap --json" not in agent_skills_doc:
        fail("AGENT_SKILLS discovery hierarchy no longer uses ./moss")
    if "./moss agent bootstrap --json" not in agent_api_doc:
        fail("AGENT_API bootstrap documentation no longer uses ./moss")
    if "Use ./moss for language/semantic operations and ./margo for project operations." not in agent_api_doc:
        fail("AGENT_API minimal onboarding snippet lost the checkout tool split")

    agents = AGENTS.read_text(encoding="utf-8")
    for marker in (
        "moss-language",
        "moss-agent-workflow",
        "./moss agent bootstrap --json",
        "run `make` first",
        "compiler: `./moss`",
        "project driver: `./margo`",
        "Bare `moss` or `margo` are valid only when",
        "./margo build",
        "./margo run",
        "./margo test",
        "./margo bench",
        "./margo clean",
        "Before declaring a Moss gap",
        "backend compiler bug even when Moss source semantics are valid",
    ):
        if marker not in agents:
            fail(f"AGENTS.md no longer routes a fresh agent to: {marker}")
    startup = agents.split("## Before declaring a Moss gap", 1)[0]
    load_skills = startup.find("**Load Skills**")
    run_discovery = startup.find("**Run Discovery**")
    follow_routing = startup.find("**Follow Discovery Routing**")
    if min(load_skills, run_discovery, follow_routing) < 0 or not load_skills < run_discovery < follow_routing:
        fail("AGENTS.md no longer orders skills, bootstrap, and discovery routing")
    if "result.canonical_docs.practical_language_guide" not in startup:
        fail("AGENTS.md no longer routes fresh source work through bootstrap canonical docs")
    status = (ROOT / ".codex" / "CURRENT_STATUS.md").read_text(encoding="utf-8")
    if "Uncommitted discovery work" in status:
        fail("CURRENT_STATUS.md retains stale uncommitted discovery wording")
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
            "language_surface",
            "structured_execution_trace",
        "synchronization_plan",
        "package_project_driver",
        "package_dependencies",
        "package_lockfile",
        "tool_invocation",
    ):
        if capability not in capability_names:
            fail(f"live capability discovery omitted {capability}")
    for capability in ("package_project_driver", "package_dependencies", "package_lockfile", "tool_invocation"):
        if not bootstrap["capability_flags"].get(capability):
            fail(f"bootstrap did not flag {capability} as available")
    catalog = {item["id"]: item for item in capabilities["capability_catalog"]}
    for capability, marker in {
        "package_project_driver": "margo build|run|test|bench|clean|debug",
        "package_dependencies": "Moss.toml",
        "package_lockfile": "Moss.lock",
        "module_interfaces": "margo build --json",
        "language_surface": "moss agent bootstrap --json",
        "tool_invocation": "moss agent bootstrap --json",
    }.items():
        if capability not in catalog or marker not in catalog[capability].get("entrypoint", ""):
            fail(f"live {capability} discovery lacks current Margo/Moss routing")
    invocation = bootstrap.get("tool_invocation", {})
    if invocation.get("compiler") != {
        "repo_local": "./moss", "path_name": "moss", "build_if_missing": "make"
    }:
        fail("bootstrap tool_invocation compiler contract drifted")
    if invocation.get("project_driver") != {
        "repo_local": "./margo", "path_name": "margo"
    }:
        fail("bootstrap tool_invocation project-driver contract drifted")
    if (invocation.get("preferred_in_checkout") != "repo_local" or
            not invocation.get("path_names_allowed") or
            invocation.get("path_fallback") != "PATH"):
        fail("bootstrap tool_invocation checkout/PATH policy drifted")
    if invocation.get("bootstrap_command") != "./moss agent bootstrap --json":
        fail("bootstrap tool_invocation bootstrap command drifted")
    fast_debug = catalog.get("fast_debug", {})
    if not any(command in fast_debug.get("entrypoint", "")
               for command in ("moss run --interp", "moss debug")):
        fail("live fast_debug discovery lacks a current Fast Debug command")
    surface = bootstrap.get("source_surface", {})
    if surface.get("locals", {}).get("immutable") != "let x = expression":
        fail("bootstrap source surface no longer exposes immutable let syntax")
    if surface.get("locals", {}).get("mutable") != "var x = expression":
        fail("bootstrap source surface no longer exposes mutable var syntax")
    if surface.get("operators", {}).get("boolean_negation") != "not expression":
        fail("bootstrap source surface no longer exposes not expression")
    operators = surface.get("operators", {})
    if operators.get("arithmetic") != ["+", "-", "*", "/", "%"] or operators.get("integer_remainder") != "%":
        fail("bootstrap source surface no longer exposes integer remainder")
    collections = surface.get("collections", {})
    if collections.get("empty_typed_vector") != "Vector[T]()" or collections.get("local_type_annotations") is not False:
        fail("bootstrap source surface no longer exposes typed empty Vector construction")
    if surface.get("domains", {}).get("fn_inside_domain") != "handler":
        fail("bootstrap source surface no longer distinguishes domain handlers")
    collections = surface.get("collections", {})
    vector = collections.get("Vector", {})
    if (vector.get("construction") != {"literal": "[a, b, c]", "empty_typed": "Vector[T]()"}
            or vector.get("methods") != ["push(item)", "pop()"]
            or vector.get("indexing") != {"read": "vec[i]", "write": "vec[i] = item"}
            or vector.get("cardinality") != "vec |> count"):
        fail("bootstrap collection surface no longer exposes Vector operations")
    map_surface = collections.get("Map", {})
    if (map_surface.get("construction") != {"inferred": "Map()"}
            or map_surface.get("indexing") != {"read": "map[key]", "write": "map[key] = value"}
            or map_surface.get("methods") != ["get(key, default)", "keys()", "values()"]
            or map_surface.get("iteration_note") != "keys() and values() return eager owned Vector snapshots"
            or map_surface.get("deletion_supported") is not False):
        fail("bootstrap collection surface no longer exposes Map operations")
    queue = collections.get("Queue", {})
    if queue.get("construction") != {"inferred": "Queue()"} or queue.get("methods") != ["push(item)", "pop()"]:
        fail("bootstrap collection surface no longer exposes Queue operations")
    tests_surface = surface.get("tests", {})
    if (tests_surface.get("syntax") != 'test "name":'
            or tests_surface.get("assertions") != ["assert(condition)", "assertEqual(actual, expected)"]
            or tests_surface.get("domain_topology") != {
                "test_blocks_are_composition_roots": False,
                "composition_root": "main initial composition prefix",
            }):
        fail("bootstrap source surface no longer exposes test/domain topology")
    composition = surface.get("domains", {}).get("composition", {})
    if composition.get("domain_instances") != "constructed statically in main's initial composition prefix":
        fail("bootstrap source surface no longer exposes domain composition location")
    if "side-effect-free" not in composition.get("initializer_rule", ""):
        fail("bootstrap source surface no longer exposes composition initializer restriction")
    if bootstrap.get("canonical_docs") != {
        "practical_language_guide": "docs/GENTLE_INTRODUCTION_TO_MOSS.md",
        "formal_language_design": "docs/MOSS_V0_1_LANGUAGE_DESIGN.md",
        "project_workflow": "docs/PROJECT_WORKFLOW.md",
        "testing": "docs/TESTING.md",
    }:
        fail("bootstrap canonical documentation routing drifted")
    if bootstrap.get("project_surface") != {
        "manifest": "Moss.toml",
        "minimal_manifest": '[project]\nname = "app"\nversion = "0.1.0"\n\n[build]\nsource = "src"\n',
        "source_directory": "src",
        "project_driver": "./margo",
    }:
        fail("bootstrap project surface drifted")
    debug_features = {item["name"]: item for item in bootstrap["debugging_features"]}
    if "for traversal is not currently supported" not in debug_features.get("fast_debug", {}).get("limitations", []):
        fail("Fast Debug discovery omitted its verified for-traversal limitation")
    if "container methods reached through object fields are not currently supported" in debug_features.get("fast_debug", {}).get("limitations", []):
        fail("Fast Debug discovery retained a repaired container-field limitation")
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
    if "tool_invocation" not in schema_names:
        fail("agent schema does not describe repository-local tool invocation")
    if "moss agent session-report-template --json" not in " ".join(workflow.split()):
        fail("moss-agent-workflow no longer advertises the session-report command")
    session = agent_document(compiler, "session-report-template")["result"]
    if not session.get("session_report_questions"):
        fail("live session-report-template omitted its structured questions")
    if "Before declaring a language/compiler gap" not in workflow:
        fail("moss-agent-workflow no longer requires gap classification")
    if "Every `fn` declared directly inside a\n`domain` is a **handler**" not in language:
        fail("moss-language no longer explains that domain fn is a handler")
    for marker in (
        "`canonical_docs.practical_language_guide`",
        "`Vector`: `vec.push(item)`, `vec.pop()`, indexed `vec[i]`, and `vec[i] = item`",
        "`Map`: strict indexing `map[key]`, indexed assignment `map[key] = value`, and defaulted lookup",
        "`Queue`: `queue.push(item)` and `queue.pop()`",
        "A test block is not a separate domain\ncomposition root",
        "state initializer must be side-effect-free",
        "accept a pure ordinary helper call",
    ):
        if marker not in language:
            fail(f"moss-language no longer agrees with bootstrap discovery: {marker}")
    for marker in (
        "bootstrap/source_surface",
        "canonical_docs.practical_language_guide",
        "bootstrap/project_surface",
        "canonical_docs.project_workflow",
        "Do not filesystem-search arbitrary examples",
    ):
        if marker not in workflow:
            fail(f"moss-agent-workflow no longer routes discovery through: {marker}")

    lower_language = language.lower()
    for obsolete in ("sender fifo", "total commit order", "worker queue", "mailbox"):
        if obsolete in lower_language:
            fail(f"moss-language teaches obsolete runtime wording: {obsolete}")

    if extract_valid_example(language) != EXAMPLE.read_text(encoding="utf-8"):
        fail("moss-language's advertised valid example differs from its checked fixture")

    check = run([str(compiler), "--check", str(EXAMPLE)], ROOT)
    if check.returncode != 0:
        fail(f"skill example failed Moss checking:\n{check.stdout}{check.stderr}")
    source_surface = run([str(compiler), "check", str(SOURCE_SURFACE), "--json"], ROOT)
    if source_surface.returncode != 0:
        fail(f"advertised source surface failed Moss checking:\n{source_surface.stdout}{source_surface.stderr}")
    discovery_collections = run([str(compiler), "check", str(DISCOVERY_COLLECTIONS), "--json"], ROOT)
    if discovery_collections.returncode != 0:
        fail(f"advertised collection operations failed Moss checking:\n{discovery_collections.stdout}{discovery_collections.stderr}")
    composition_pure_helper = run([str(compiler), "check", str(COMPOSITION_PURE_HELPER), "--json"], ROOT)
    if composition_pure_helper.returncode != 0:
        fail(f"documented direct/pure-helper composition initialization failed Moss checking:\n{composition_pure_helper.stdout}{composition_pure_helper.stderr}")
    test_composition_root = run([str(compiler), "check", str(TEST_COMPOSITION_ROOT_REJECTED), "--json"], ROOT)
    if (test_composition_root.returncode == 0
            or "domain construction is allowed only in the main composition prefix" not in test_composition_root.stdout):
        fail("test/domain topology discovery no longer matches the compiler")
    impure_composition_initializer = run([str(compiler), "check", str(IMPURE_COMPOSITION_INITIALIZER), "--json"], ROOT)
    if (impure_composition_initializer.returncode == 0
            or "domain state initializers must be side-effect-free" not in impure_composition_initializer.stdout):
        fail("composition initializer discovery no longer matches the compiler")
    immutable_let = run([str(compiler), "check", str(IMMUTABLE_LET), "--json"], ROOT)
    if immutable_let.returncode == 0 or "IMMUTABLE_LOCAL_MUTATION" not in immutable_let.stdout:
        fail("explicit let mutation was not rejected by the advertised source surface")
    local_annotation = run([str(compiler), "check", str(LOCAL_ANNOTATION), "--json"], ROOT)
    if local_annotation.returncode == 0 or "LOCAL_TYPE_ANNOTATION_UNSUPPORTED" not in local_annotation.stdout:
        fail("unsupported local annotation did not receive its advertised diagnostic")
    fast_debug_field = run([str(compiler), "run", "--interp", str(FAST_DEBUG_CONTAINER_FIELD)], ROOT)
    if fast_debug_field.returncode != 0 or fast_debug_field.stdout != "1\n":
        fail("Fast Debug container-field support does not match checked execution")

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
