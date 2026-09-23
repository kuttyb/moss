# Moss agent onboarding skills

Moss keeps two compact, repository-owned skills under `.agents/skills/`. This is the
repository-local discovery location used by current Codex tooling: when Codex starts
in this repository or a subdirectory, it scans `.agents/skills` up to the Git root and
can select a skill by its `name` and `description`. A restart may be needed if a local
Codex session does not notice a newly checked-out skill.

The canonical checked-in bodies are:

- [`moss-language`](../.agents/skills/moss-language/SKILL.md) — current Moss v0.1
  source rules, domain/routing model, ownership/effects, traits, functional code,
  modules, and the most important rejected patterns.
- [`moss-agent-workflow`](../.agents/skills/moss-agent-workflow/SKILL.md) — the
  compiler-first `moss-agent-1` loop, structured diagnostics and queries, formatting,
  impact/affected tests, Fast Debug, and traces.

Use `$moss-language` explicitly when an agent is editing or generating Moss, or let
Codex select it from its description. Use `$moss-agent-workflow` for compiler errors,
Moss debugging, or agent-driven edits. The project `AGENTS.md` points agents to both,
but deliberately does not duplicate their contents.

The discovery hierarchy is intentionally small:

```text
AGENTS.md
    ↓
repository-local skills
    ↓
live ./moss agent bootstrap --json
```

`AGENTS.md` tells a fresh agent how to build/use `./moss`, load both skills, and run
bootstrap. The skills give compact current guidance; bootstrap, capabilities, and
schema discovery are the live capability contract. Margo owns packages and projects
(`./margo build|run|test|bench|clean`); Moss owns modules, `.mossi` interfaces, checking,
semantic queries, and lowering. The skills are agent guidance, not a second language
specification. The checked compiler, its diagnostics, and current regression suite
remain authoritative. In a repository checkout use `./moss`; bare `moss` and `margo`
are valid when already available on `PATH`. Run `./moss agent bootstrap --json` for live discovery; use the
skills to avoid learning retired syntax or backend details by accident. The valid
language-skill example is mirrored in a fixture and compiled with strict generated-Rust
warnings by `tests/tooling/check_agent_skills.py`.

Bootstrap also advertises a compact `source_surface`: high-frequency local,
control-flow, range, `not`, collection, and domain-handler spellings. Before calling
an unfamiliar construct a language gap, agents should use that surface, structured
diagnostics, a minimal Moss reproducer, and native verification when lowering is
involved. Generated Rust can reveal a backend defect; it is not language-design
evidence by itself.

The fixed [agent benchmark](../benchmarks/agent/README.md) now supplies Phase 22.2A's
30-task corpus and Phase 22.2B's isolated validation/result tooling. The actual
fresh-agent baseline and aggregate report remain Phase 22.2C and 22.2D work;
systematic rewrite-bearing diagnostics, richer trace slicing, and advanced repair
workflows also remain future work. During a real agent session, use
`moss agent session-report-template --json` to record whether the skills reduced
ambiguity; no telemetry is collected.
