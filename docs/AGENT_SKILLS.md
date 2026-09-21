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
live moss agent bootstrap --json
```

`AGENTS.md` tells a fresh agent how to build/use `./moss`, load both skills, and run
bootstrap. The skills give compact current guidance; bootstrap, capabilities, and
schema discovery are the live capability contract. Margo owns packages and projects
(`margo build|run|test|bench|clean`); Moss owns modules, `.mossi` interfaces, checking,
semantic queries, and lowering. The skills are agent guidance, not a second language
specification. The checked compiler, its diagnostics, and current regression suite
remain authoritative. Run `moss agent bootstrap --json` for live discovery; use the
skills to avoid learning retired syntax or backend details by accident. The valid
language-skill example is mirrored in a fixture and compiled with strict generated-Rust
warnings by `tests/tooling/check_agent_skills.py`.

This is a small dogfooding aid pulled forward from future Phase 22 work. It does not
complete Phase 22: measured skill effectiveness, systematic rewrite-bearing
diagnostics, richer trace slicing, and advanced agent repair workflows remain future
work. During a real agent session, use `moss agent session-report-template --json` to
record whether the skills reduced ambiguity; no telemetry is collected.
