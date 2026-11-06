# ECT Check Benchmark Suite

**Phase 28.5, Team 1 (harness and measurement plumbing).** Version 0.1.0.

Records today's cost of the *check* part of edit-compile-test and is built to
prove or disprove the Phase 28.5 fixes (`.mossi` firewall, content-addressed
fact store, resident daemon) with **no fixture changes**: every benchmark
goes through command templates, so a future compiler or warm client is swapped
in from the command line. Background: `docs/phase28.5/moss-ect-check-principles.md`.

`benchmarks/compiler_torture` is a separate, frozen suite and is never touched
by this one.

## Layout

```text
benchmarks/ect_check/
  README.md
  generate.py      generator framework, frozen manifest, --validate-only
  run.py           runner: bench | gate | metrics | validate
  ect_common.py    hashing, command templates, measured execution, verdicts
  manifest.json    frozen: id, kind, entry, expected_status, SHA-256 per fixture
  fixtures/        generated, committed, never regenerated implicitly
```

Outputs go to `tmp/ect_check/<run-id>/` (disposable): `results.csv`,
`results.json`, `LOCAL_REPORT.md`, plus `gate.json` / `metrics.json` for the
single-purpose subcommands, and raw `counters/*.jsonl`.

## How to run

Build the optimized compiler first (`make moss-opt`). All timing is refused for
anything but a `moss-opt`-flavoured binary (see Build flavor).

```bash
# Everything: timing, counters, correctness gate, M1/M2 -> report
python3 benchmarks/ect_check/run.py bench            # or: make ect-check-bench

# Only some fixtures / more reps (>= 3 enforced)
python3 benchmarks/ect_check/run.py bench --ids trivial_pkg_pass --reps 7

# Frozen-corpus integrity + expected status (either binary is fine)
python3 benchmarks/ect_check/generate.py --validate-only        # make ect-check-validate
python3 benchmarks/ect_check/generate.py --validate-only --no-compile   # hashes only
python3 benchmarks/ect_check/generate.py --verify-determinism   # regenerate == manifest

# Correctness gate on its own
python3 benchmarks/ect_check/run.py gate --candidate-moss /path/to/other/moss
python3 benchmarks/ect_check/run.py gate --candidate-cmd 'mossc check --json {path}'

# M1 / M2 only
python3 benchmarks/ect_check/run.py metrics
```

Compiler selection: `--moss` or `$MOSS`, default `build/compiler/moss-opt`.
`--margo` defaults to `./margo` (which receives the same compiler via `$MOSS`).

### Pluggable warm client

A resident client is measured by giving a command template; nothing else
changes:

```bash
ECT_WARM_CLIENT_CMD='mossc --socket /run/user/1000/moss.sock check --json {path}' \
  python3 benchmarks/ect_check/run.py bench --warm-primes 2
# or --warm-client-cmd '...'
```

Placeholders (formatted per token after shell-splitting, so spaces are safe):
`{moss}` `{margo}` compiler / driver paths, `{path}` the entry file (file
fixtures) or root package directory (package fixtures), `{root}` the staged
fixture directory, `{id}` the fixture id. The command must print a
`moss-agent-1` (`moss check --json`) or `margo-1` (`margo --json`) document and
exit non-zero on failure; anything else is compared as raw text so a mismatch
is visible. Warm mode stages each fixture once, runs `--warm-primes`
unmeasured requests, then the timed reps against the same staged tree, and
adds a `warm` series next to `cold`. The same template is also used as the
gate candidate (`--candidate-cmd` / `$ECT_CANDIDATE_CMD` overrides it for the
gate only). Warm mode reports no compiler counters.

## What the outputs mean

- **Mode `cold`**: fresh process per rep on a fresh copy of the fixture
  (package fixtures: `margo --check build` from an empty build dir).
- **Wall / CPU / RSS**: median of the timed reps (range shown for wall). CPU is
  user+system of the whole process tree, RSS is the largest single process in
  the tree (not a sum), both from `wait4` rusage.
- **Counters** come from one separate counter run with
  `MOSS_PROFILE_COMPILER=1` and `MOSS_ECT_COUNTERS=<file>` so they never perturb
  timing. A command may start several checker runs; counters are summed
  (`checker_runs`). `functions_analyzed`; specialization *evals*
  (`specialization_requests`), *hits* (`reused_specialization_results`, the
  in-process memo), *keys* (`distinct_specialization_keys`); plus specialization
  effect evaluations / cache hits in the CSV/JSON.
- **Correctness gate**: runs reference and candidate on every fixture and
  requires identical `{exit_code, ok, diagnostics}` (paths normalized, volatile
  timing keys dropped), and that the reference agrees with the manifest's
  `expected_status`. Today both sides are the same compiler, so a pass proves
  only that the gate is wired and deterministic.
- **M1**: share of package-local exported generics (`export fn` with an untyped
  parameter) in `sdk/` and `packages/` whose open summary is `scheme_complete`.
- **M2**: share of package-local non-exported functions whose local summary is
  `complete`. Reported two ways: over all internal functions, and restricted
  to those that carry an open summary (only inferred-signature callables do).
  Both are reported for `margo --check` and full `margo build`, because the
  flags are read at the end of the package's source check and depend on that
  stage. Packages whose margo run fails are listed, not counted.

## Compiler support (read-only)

`src/semantic_checker.cpp` appends one JSON object per checker run to
`$MOSS_ECT_COUNTERS` (only when `MOSS_PROFILE_COMPILER`/`MOSS_PROFILE_TRACE`
is also set) and prints `ect_m1_*` / `ect_m2_*` lines under
`MOSS_PROFILE_COMPILER`. It reads checked facts only and is compiled into every
build flavor (not under `MOSS_TEST_BUILD`). Pre-existing counters were reused
for functions and specialization.

## Build flavor

Flavor comes from the binary name (`moss-opt` = `-O2`, `moss-dev` = `-O0` with
`-DMOSS_TEST_BUILD=1`; override with `$ECT_COMPILER_FLAVOR`). `bench` refuses to
time a non-`opt` binary unless `--allow-non-opt`, which puts a warning in the
report header. Path, SHA-256 and flavor are in the report header and in every
CSV row. `validate` and `gate` accept either flavor.

## Frozen contract and extending

Fixtures are generated by seeded generators (`GENERATORS` in `generate.py`; each
generator derives its own RNG stream from the suite seed and its name, so adding
one never perturbs another). `generate.py` refuses to overwrite an existing
manifest; `--regenerate` is an intentional re-freeze. Later teams add a
`Generator` subclass for generic DAGs, edit sequences, worktree layouts and
replay logs, then re-freeze once. Manifest rows carry `kind` (`file` or
`package`), `entry`, `expected_status`, `sha256` (files: content; directories:
sorted relpath+hash rows, build dirs excluded) and free-form `metadata`.

## What improvement looks like (stubs for later teams)

- **Mossi Leak (generic DAGs)**: TBD. Expect checker runs / functions analyzed
  for a consumer to stop scaling with the depth of packages beneath an
  exported generic.
- **Inference Ripple (edit sequences)**: TBD. Expect functions re-inferred per
  body edit to drop from "callers up to roots" to the first consistent level.
- **Cold Tax (warm client)**: TBD. Expect warm wall time to approach checker
  work only, with startup/parse/.mossi loading amortized.
- **Worktrees**: TBD. Expect N worktrees to cost base plus divergence in
  CPU and RSS.
- **M1 / M2**: TBD. M1 should stay at 100% (hardening must not lose schemes);
  M2 is the reference for how much of the internal graph a fact store could
  skip.

## Known quirks recorded while building the harness

- `fn twice(x): return bump(bump(x))` with `bump(x): return x + 1` makes
  `moss check` fail with `MOSS_INTERNAL_OR_IO_ERROR: internal synchronization
  invariant: unresolved state layout`; the trivial fixture uses an intermediate
  `var` instead. Not investigated further here.
- `packages/linalg`, `packages/sparse` and `sdk/tables` fail their test-target
  semantic check under `margo --check` today (`TYPE_MISMATCH` on binding
  `ignored`); they are excluded from the metrics and listed in the report.
