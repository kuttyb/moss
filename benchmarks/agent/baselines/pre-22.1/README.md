# Phase 22.2C pre-22.1 baseline

This directory is the canonical raw fresh-agent baseline captured before Phase
22.1 diagnostic improvements. It uses protocol `phase-22.2c-v1`, corpus commit
`b187009f6ef06738c52216d34490342d76d7ab4c`, compiler commit
`22fdb024e4f284d17fb9abfe887856e17127aae7`, and orchestration commit
`532dc4610d156946df5014573e5d69947f4d3546`.

Each AB001–AB030 directory records one canonical, independent OpenAI Codex CLI
0.156.0 session using `gpt-6-sol` with medium reasoning and a 900-second wall
limit. Sessions were ephemeral, had no retry or resume, and could see only the
task starter, fixed prompt, normal Moss documentation, skills, and tools. The
agent-visible mount contained neither benchmark reference solutions nor usable
Git history, and external web and shell network access were disabled. The exact
configuration and metric definitions are in `protocol.json`.

The canonical results are:

- 30 tasks attempted, 28 passed, and 2 failed (`AB014`, `AB017`).
- 17 first meaningful validation commands succeeded.
- All 30 tasks reached a successful Moss/Margo correctness command; attempts to
  green had mean 1.433 and median 1.
- 407 agent tool calls and 157 Moss/Margo invocations were recorded.
- No task timed out, suffered an infrastructure failure, or changed a path
  outside its allowance.

Both final failures were exact-output mistakes: the agents printed two requested
values on separate lines where the tasks required one space-separated line. They
are retained as benchmark failures even though their programs compiled and their
requested Moss structures were present.

Actual Moss/Margo capability use comprised 30 bootstrap calls, 48 checks, 25
interpreter runs, 4 trace runs, 9 effects queries, 2 ownership queries, 2 inspect
queries, 1 calls query, 6 Margo build/test/run calls, and 27 formatting calls,
plus a small number of help, clean, and direct run commands. The diagnostic-code
counts are preserved in `summary.json`; detailed order and producing commands are
stored in each task manifest and tool log.

The initial AB020 session exposed corpus revision 2 and is retained below
`noncanonical/`; it is excluded from every summary metric. The earlier full
provisional series invalidated by the sanitized rustup environment is represented
separately in `../invalidated-pre-22.1-rustup/` and is also excluded. These records
must not be treated as retries of an agent failure.

Each canonical task directory contains the exact prompt, compact manifest,
objective Moss/Margo log, authoritative validator result, final relevant files,
and final agent message. Generated native artifacts, caches, reference solutions,
and large conversation transcripts are intentionally absent. Phase 22.2D's
machine-readable `aggregate.json` and human-readable `REPORT.md` are generated
from these raw artifacts by `../../analyze_baseline.py`; the raw inputs remain
unchanged.
