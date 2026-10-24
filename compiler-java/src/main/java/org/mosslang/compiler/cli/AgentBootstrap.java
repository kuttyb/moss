package org.mosslang.compiler.cli;

public class AgentBootstrap {
    public static String getBootstrapJson(String compilerVersion, String languageVersion) {
        return """
{
  "protocol_version": 1,
  "schema_version": "moss-agent-1",
  "compiler_version": "%s",
  "command": "agent bootstrap",
  "ok": true,
  "result": {
    "language_version": "%s",
    "agent_protocol_version": 1,
    "compiler_version": "%s",
    "project_root": null,
    "canonical_docs": {
      "practical_language_guide": "docs/GENTLE_INTRODUCTION_TO_MOSS.md",
      "formal_language_design": "docs/MOSS_V0_1_LANGUAGE_DESIGN.md",
      "project_workflow": "docs/PROJECT_WORKFLOW.md",
      "testing": "docs/TESTING.md"
    },
    "tool_invocation": {
      "compiler": {
        "repo_local": "./scripts/java/run.sh",
        "path_name": "moss",
        "build_if_missing": "./scripts/java/build.sh"
      },
      "project_driver": {
        "repo_local": "./margo",
        "path_name": "margo"
      },
      "preferred_in_checkout": "repo_local",
      "path_names_allowed": true,
      "path_fallback": "PATH",
      "bootstrap_command": "./scripts/java/run.sh agent bootstrap --json"
    },
    "capabilities": [
      "structured_diagnostics",
      "semantic_inspection",
      "static_call_graph",
      "ownership_effects",
      "observable_effects",
      "functional_optimization_explanations",
      "backend_lowering_explanations",
      "project_builds",
      "native_tests",
      "native_benchmarks",
      "benchmark_baselines",
      "durable_semantic_identities",
      "robust_query_targeting",
      "semantic_query_expansion",
      "semantic_editor_queries",
      "semantic_references",
      "workspace_symbols",
      "semantic_completion",
      "semantic_hashing",
      "impact_analysis",
      "incremental_verification",
      "modules",
      "qualified_imports",
      "module_interfaces",
      "generic_specialization_identity",
      "interpreted_domains",
      "fast_debug",
      "language_surface",
      "affected_tests",
      "formatter",
      "canonical_formatter",
      "semantic_edits",
      "repair_actions",
      "static_cost_facts",
      "source_provenance",
      "first_order_effect_graph",
      "structured_execution_trace",
      "structured_debug_query",
      "synchronization_schema",
      "synchronization_plan",
      "concrete_domain_graph",
      "domain_ranks",
      "phase20_fileio_executor",
      "package_project_driver",
      "package_dependencies",
      "package_lockfile",
      "tool_invocation",
      "agent_benchmark_suite"
    ],
    "source_surface": {
      "locals": {
        "implicit_binding": "x = expression",
        "immutable": "let x = expression",
        "mutable": "var x = expression"
      },
      "control_flow": {
        "if_else": true,
        "while": true,
        "for_in": true,
        "pass": true,
        "range_forms": [
          "range(start, end)",
          "range(start, end, step)"
        ],
        "exhaustive_enum_match": "match expression: / match consume <owned enum expression>:"
      },
      "synchronous_message": {
        "invocation": "message target.Handler(args...)",
        "reply": "reply expression",
        "await_retired": true,
        "spawn_retired": true
      }
    },
    "safety_rules": [
      "Do not edit generated Rust.",
      "Do not infer dynamic targets; Moss dispatch is statically closed.",
      "Treat synchronous message as an explicit domain value boundary; await is retired.",
      "Do not self-send or chain handlers on the same domain; use an ordinary helper.",
      "Domain handles are static routing capabilities and do not cross ordinary value boundaries.",
      "Do not write locks or dynamically create domains; the compiler owns synchronization and topology.",
      "Preserve Moss diagnostics and source provenance."
    ],
    "language_constraints": [
      "message is synchronous and reply terminates a handler",
      "await and spawn are retired source syntax",
      "incoming message payloads are immutable snapshots",
      "domain routes and concrete instances are statically composed",
      "traits are structural and dispatch/specialization are static",
      "ordinary recursion and general first-class closures are unsupported in v0.1"
    ]
  }
}
""".formatted(compilerVersion, languageVersion, compilerVersion).trim();
    }
}
