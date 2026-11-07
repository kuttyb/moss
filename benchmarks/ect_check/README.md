# ECT Check Benchmark Suite

**Phase 28.5, Team 1 (harness and measurement plumbing), Team 2 (generic-DAG
fixtures), Team 3 (edit sequences B3, cold/warm B4, worktrees B5) and Team 4
(agent-loop replay B6 and summary churn) and Team 5 (Rust comparison and the B3 error-path
edit).** Suite version 0.5.0 (0.4.1 plus the Team 5 Rust fixtures/sequences and B3 edit `f`; every
earlier fixture and scenario row is byte-identical, hashes unchanged).

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
  run.py           runner: bench | gate | metrics | validate | selftest
  selftest.py      RSS-independence, gate-sensitivity, timeout-kill, stack tests
  ect_common.py    hashing, command templates, measured execution, verdicts
  dag_generators.py  Team 2 generators: b1_leak, b2_equiv, torture_ext, dag_diamond, dag_fanin
  edit_generators.py Team 3 generators: b3_edits, b4_cold_warm, b5_worktrees (+ frozen scenario specs)
  replay.py        Team 4: B6 replay (git-derived step specs, materialize/apply, run, gate, churn driver)
  churn.py         Team 4: summary-churn queries, comparison and classifier
  replay_generators.py / replay_expectations.json   B6 specs (frozen into the manifest) and recorded reference verdicts
  rust_generators.py / rust_cmp.py / report_rust.py  Team 5: Rust fixtures + sequences, cargo runner/gate, report section
  results/team5-final/  the committed Team 5 quiet run (LOCAL_REPORT.md, results.csv/json)
  report_replay.py B6 and churn report section, statistics, results_replay.csv
  results/b6-final/  the committed final quiet run (LOCAL_REPORT.md, CSVs, churn.json)
  scenarios.py     scenario engine: edit sequences, frozen-patch application, worktree groups
  edit_evidence.py B3 query evidence (`moss inspect` before/after each edit)
  report_scenarios.py  report sections for B3/B4/B5
  probes/          minimal compiler-behavior reproducers (gen_probe.py + samples)
  report_dag.py    report sections + slope fits for those families
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
# Phase 28.6 (L8): a candidate that anchors diagnostics to declarations must supply a position mapping back to the
# reference layout, which the gate applies before comparing; see "Correctness gate" below. Until that hook
# exists the gate compares positions raw.
# Rust (Team 5): the 19 cargo sequences run in `bench` and `gate` too; --require-rust fails instead of skipping
# when cargo is missing (default: a loud FAIL-OPEN line, recorded in the gate result and the report).

# M1 / M2 only (default roots: sdk/ and packages/)
python3 benchmarks/ect_check/run.py metrics
python3 benchmarks/ect_check/run.py metrics --root benchmarks/ect_check/fixtures/trivial/pkg_generic_types

# Harness self-tests (RSS independent of runner size; gate catches mutations)
python3 benchmarks/ect_check/run.py selftest
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
fixture directory, `{id}` the fixture id, `{kind}` `file` or `package`. The command must print a
`moss-agent-1` (`moss check --json`) or `margo-1` (`margo --json`) document and
exit non-zero on failure; anything else is compared as raw text so a mismatch
is visible. Warm mode stages each fixture once, runs `--warm-primes`
unmeasured requests, then the timed reps against the same staged tree, and
adds a `warm` series next to `cold`. The same template is also used as the
gate candidate (`--candidate-cmd` / `$ECT_CANDIDATE_CMD` overrides it for the
gate only). Warm mode reports no compiler counters.

## What the outputs mean

- **Mode `cold`**: fresh process per rep on a fresh copy of the fixture
  (package fixtures: `margo --check build` from an empty build dir). `cold`
  means fresh process + empty build dir, **not** a cold page cache. One
  discarded warm-up run per fixture precedes the timed reps (binary page-in);
  disable with `--no-warmup`. `margo` is a Python script, so package fixtures
  include roughly 15-20 ms of Python startup that a single-file check does not.
  Timed and gate runs get a clean environment: `MOSS_PROFILE*`, `MOSS_ECT_*` and
  `MOSS_*_TRACE` variables from the caller are stripped; only the separate
  counter run sets them.
- **Wall / CPU / RSS**: median of the timed reps (range shown for wall). Wall is
  the runner's monotonic clock around the command (includes ~1 ms of the
  `/usr/bin/time` wrapper). CPU is user+system of the whole process tree from
  `wait4`. **Peak RSS comes from `/usr/bin/time -f %M`**, the largest single
  process in the tree (not a sum). It is deliberately not taken from the
  runner's `wait4` rusage: on Linux a forked+exec'd child's `ru_maxrss` includes
  the parent's pre-exec high-water mark, so a large Python runner inflates every
  child. `run.py selftest` proves RSS does not move when the runner is inflated
  by 400 MiB.
- **Counters** come from one separate counter run with
  `MOSS_PROFILE_COMPILER=1` and `MOSS_ECT_COUNTERS=<file>` so they never perturb
  timing. A command may start several checker runs; counters are summed
  (`checker_runs`). A fixture that fails before the checker finishes (e.g. a
  failing file fixture) reports `checker_runs` 0 and null counters. `functions_analyzed`; specialization *evals*
  (`specialization_requests`), *hits* (`reused_specialization_results`, the
  in-process memo), *keys* (`distinct_specialization_keys`); plus specialization
  effect evaluations / cache hits in the CSV/JSON.
- **Correctness gate**: runs reference and candidate on every fixture and
  requires identical `exit_code`, `ok` and the **whole JSON document** (an extra
  or changed field anywhere fails). Only paths (fixture dir, compiler and margo
  executables) are normalized and these keys are ignored at any depth:
  `duration_ms elapsed_ms time_ms timings pid scheduling_events compiler_version`
  (`ect_common.VOLATILE_KEYS`). The edit-sequence (scenario) gate additionally treats margo's
  `checked` and `skipped` package lists as volatile (`scenarios.SCENARIO_VOLATILE`: a daemon or
  content-addressed client may answer a repeat or a revert from its store and report different
  bookkeeping); they are shown in the report as information, while verdicts, diagnostics, exit
  codes and every other field stay strict. `bench` and `gate` also re-verify every selected
  fixture's frozen tree hash and every scenario's canonical-JSON SHA-256 before running anything. A margo failure embeds the compiler's JSON in
  its `error` string; it is parsed and compared structurally
  (`trivial_pkg_fail` exercises this). **L8 requirement (Phase 28.6): position mapping before
  comparison.** Once Phase 28.6 Team 1 anchors diagnostics to declarations (positions relative to a
  declaration instead of file line/column), a candidate's positions no longer equal the
  reference's, and a gate that compares them raw reports false mismatches. The gate must then
  compare **after mapping the candidate's positions back to the previous (reference) layout**. This
  applies to every position-bearing field of the Moss verdict (diagnostic `line`/`column`/span
  fields, `source.line`, `direct_calls[].line`, `semantic_identity` and call-site ids, which embed
  `@line`) and, in the Rust gate, to the primary-span `line`/`column` of each diagnostic. Who owns
  what: **the candidate's author supplies the mapping** (a tool or table that turns the candidate's
  anchored positions into reference positions for the frozen fixture and edit sequences, delivered
  with the Phase 28.6 change); **the gate applies it** to the candidate verdict before the
  comparison (a hook in `ect_common.extract_verdict`/`scenarios.gate_view` that today does not
  exist: today the gate compares positions exactly as printed). Until the hook exists, an
  "identical" in the gate for a candidate that changes the position scheme means nothing, and a
  "differs" may be only the scheme. The reference must also agree with the
  manifest: `expected_status`, the optional `expected_exit_code` (so a crash,
  abort or clean diagnostic cannot stand in for each other) and no timeout
  (`ect_common.fixture_ok`). The gate passes only when candidate verdict ==
  reference verdict; a candidate that no longer matches the manifest is
  annotated IMPROVED (passes a fixture expected to fail) or REGRESSED in the
  report, and a warm-mode row that differs from the manifest is a note, not a
  `bench` failure (only cold/reference rows are). A broken template (unknown placeholder, missing
  binary) fails with a one-line error. `selftest` checks that an identity client
  passes and a mutating client fails. Today both sides are the same compiler, so a pass proves
  only that the gate is wired and deterministic.
- **M1**: share of package-local exported generics (`export fn` with an untyped
  parameter) in `sdk/` and `packages/` whose open summary is `scheme_complete`.
- **M2**: of package-local non-exported functions that are *eligible* (only
  callables with an untyped parameter carry an open summary), the share whose
  local summary is `complete`. Fully annotated internal functions can never be
  eligible; their count is reported separately and is not part of the share.
  Today in `sdk/` and `packages/` only two internal functions are eligible
  (`sdk/text` `matches_at`, `sdk/path` `starts_with_at`), so M2 is
  `0/2`, not a statement about hundreds of functions. `complete` is also
  leaf-biased (callers of inferred callees are not complete; a body with open
  arithmetic such as `i = i + a` is incomplete), so M2 is a weak proxy for how
  much a fact store could skip. Both M1 and M2 are reported for `margo --check`
  and full `margo build` because the flags are read at the end of the package's
  source check and depend on that stage. Packages whose margo run fails are
  dropped from that mode only, so denominators differ between modes. `metrics
  --root DIR...` measures any package tree (e.g. a fixture) instead of
  `sdk/` and `packages/`.

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
CSV row. `validate` and `gate` accept either flavor for the Team 1 fixtures and
the DAG families. The `torture_ext` deep chains run with a 4 GiB stack limit
(`stack_kib`), which removes the stack dependence of the checker on moss-opt;
they were validated on moss-opt only (moss-dev -O0 has much larger frames and
is far slower, so it is not claimed to work for them).

## Frozen contract and extending

Fixtures are generated by seeded generators (`GENERATORS` in `generate.py`; each
generator derives its own RNG stream from the suite seed and its name, so adding
one never perturbs another). `generate.py` refuses to overwrite an existing
manifest; `--regenerate` is an intentional re-freeze. Later teams add a
`Generator` subclass for generic DAGs, edit sequences, worktree layouts and
replay logs, then re-freeze once. Manifest rows carry `kind` (`file` or
`package`), `entry`, `expected_status`, `sha256` (files: content; directories:
sorted relpath+hash rows; only a package's own top-level `build/`, i.e. a
`build` directory next to a `Moss.toml`, and `__pycache__` are excluded, and
generators reject fixture paths named `build`, `target` or `__pycache__`) and free-form `metadata`.

## Generic-DAG fixtures (Team 2)

75 fixtures in total, 70 of them in five generators in `dag_generators.py`
(plus the 5 Team 1 smoke fixtures), all deterministic, all in the manifest, all
run through `run.py bench`, the gate and `generate.py --validate-only`.
Manifest rows may carry these optional fields:

- `timeout_s`: per-fixture timeout for every run of it (bench, gate, validate).
  On timeout the whole process group is killed (no orphaned compiler), the rep is
  flagged `timed_out`, excluded from the medians and the row's `status_ok` is
  false (`selftest` checks the kill).
- `expected_exit_code`: exact exit code required (e.g. 139 for a SIGSEGV), so a
  crash, an abort, an OOM kill or a clean diagnostic cannot satisfy each other.
- `stack_kib`: RLIMIT_STACK applied to the child before exec in bench, gate,
  validate and the counter run. **A warm client/daemon being benchmarked against
  the `torture_ext` fixtures must provide an equally large stack.**
- `provider`: a package directory inside the fixture that is built first (B1).

### DAG node shape

Moss v0.1 limits shape every node: no recursion, no field access on untyped
parameters (methods only), no nested calls of inferred generics (the known
"unresolved state layout" bug below), so each node is one non-nested call per
child inside a binary expression:

```text
leaf   fn n<D-1>_<i>(x): return x.score()            # method on the argument
inner  fn n<l>_<i>(x):   return n<l+1>_<i>(x) + 1
root   fn root(x):       return n0_0(x) + n0_1(x) + ... + n0_<W-1>(x)
```

That is **W parallel chains of depth D under one root** (D*W+1 callables; the
root's argument type flows through every one), called with K user types
`T000..` that each define `score()`.

**The spine has no shared callees, so B1 and B2 cannot measure shared-callee
leaks or re-walks** (a firewall that helps shared helpers would not show here).
Shared-callee shapes are covered by two separate families:

- `dag_fanin` (feasible, linear today): W=4 entry nodes `e_i(x) = c000(x) + i`
  all call one shared D=50 chain; per type the chain head is requested W times
  (1 miss, W-1 memo hits). 6 fixtures: a package-boundary variant (`fanin_pkg_k*`,
  like B1) and a single-file variant (`fanin_file_k*`, like B2), K = 1, 8, 32.
- `dag_diamond` (exponential today, kept on purpose): node (l,i) calls (l+1,i)
  and (l+1,(i+1) % W). Wall time doubles per level (D=6/8/10/12/14 =
  0.16/0.67/2.3/9.1/36 s) although keys and functions analyzed stay linear and
  the memo reports hits.

Two shape rules were found while building this (pre-existing compiler
behavior, recorded with reproducers in `probes/` and in
`agent-feedback/phase28-5/ect-bench/generic-dag/`): binding a child result
(`var c = child(x)`) makes the checker exponential in depth (x4 per level;
`probes/gen_probe.py`), so fixtures write `child(x) + 1`.

### Shape check (counters prove the fixture is what it says)

Section 1b of the report recomputes, from each fixture's SOURCE (not the
generator), the callables (top-level `fn` except `main`) and types (top-level
`type`), predicts functions analyzed = callables and distinct keys =
callables x K, and prints measured vs predicted with **both the functions and
the keys delta**, flags any disagreement with the manifest metadata, and (fan-in)
checks specialization requests and hits too. It also prints `Local complete`:
how many of the package's own internal functions have a `complete` local summary
(`ect_m2_internal_complete`). **That number is small: 4 of 80 internal callables
in `b1_d020_w4_*`, 1 of 101 in a 100-deep chain, 1 of 54 in the fan-in provider.**
A fact store or firewall that trusts only `complete` summaries may therefore
show little improvement on these fixtures; one that can reuse open or inferred
summaries should. `functions_analyzed` does not count `main`.

### Noise probe

`bench` times a small calibration fixture (`b2_k04`, 5 cold runs) before and
after the timed loop and records both plus the 1/5/15-minute load averages in
`results.json` (`meta.noise`) and the report header. The report prints a visible
**WARNING** if the calibration drifts by more than 10% or the 1-minute load
average at start exceeds 1.0. Run nothing else while `bench` runs, and let the
load average settle first.

### B1 - Mossi Leak (`b1_leak`, 18 fixtures: D {5,20,50} x W {1,4} x K {1,8,32})

Package `lib` (`dag_lib`) exports the root of the untyped generic DAG (all
other nodes are internal); package `app` (`dag_app`) depends on it via
`path = "../lib"`, defines K user types and calls `dag_lib.root(Tk(..))` for
each. Two series per fixture, never mixed:

- **Consumer** (the result): `margo --check build` of `app`, with `lib` already
  built (the runner builds it first), so the consumer's checker run is the only
  one measured. Counters come from a separate counter run.
- **Provider**: `margo --check build` of `lib` alone, its own wall/CPU/RSS
  medians and counters (`provider_*` columns).

Boundary evidence: `app` defines **zero** DAG functions, yet the consumer's
single checker run analyzes D*W+1 functions and creates (D*W+1)*K
specializations, i.e. bodies in `lib` are walked through its `.mossi`.

*What an improvement looks like*: with a `.mossi` firewall (instantiation
answered from the exported summary, bodies not re-walked), consumer
`functions_analyzed` and spec evaluations stop depending on D (and W), and
consumer wall/CPU stop growing with D and become nearly independent of K (about
one summary lookup per type, independent of D). The provider series absorbs the
cost: it may grow modestly (it now produces the firewall facts) but must not grow
by more than the consumer saves, summed over the workflow.

### B2 - equivalence inside one package (`b2_equiv`, 32 fixtures, K = 1..32)

The same DAG with D=20, W=4 (81 callables) in one file, no package boundary,
one fixture per K. `run.py` computes the **marginal cost per new type** as the
least-squares slope (with intercept and R2) of wall, CPU, functions analyzed,
spec keys and spec evaluations against K (also stored as `dag_analysis` in
`results.json`; the same fit over K = 1, 8, 32 is reported per B1 cell).

*What an improvement looks like*: today every new type re-walks all 81 nodes
(81 spec keys/evals per type and a wall slope proportional to that). With
stored facts keyed by content, a type whose facts are already stored should cost
about one cache hit: spec evaluations per type approaching 1 (not 81), wall/CPU
slope per type collapsing toward one lookup, intercept (first-type cost)
unchanged or lower. Comparing B1 with B2 at the same D, W, K shows what the
package boundary itself adds today (a small per-type cost and no large constant);
a firewall should make B1's slope fall toward one lookup per type independent of
D, below B2's slope while B2 has no fact store.

### Torture extension 1.1.0-ext (`torture_ext`, 10 fixtures)

Extends but **never alters** the frozen `benchmarks/compiler_torture` (generator
1.0.0): its generator, manifest and fixtures are untouched and nothing is
imported from it; the W4 chain pattern is copied. The extension has its own
version string `1.1.0-ext` in this manifest.

- `tx_w4_d0100`, `_d0200`, `_d0500`, `_d1000`: W4 chain
  `step_i(x) = step_{i-1}(x) + 1`, one Int instantiation.
- `tx_cross_d*_k*` (W4 x W2): the same chain with leaf `x.score() + 1`, the root
  instantiated with K distinct user types (d100 with K=2/8/32, d200 K=4, d500 K=4).
- `tx_w4_d0300_default_stack_crash`: the documented crash.

**Stack.** The checker recurses natively, so deep chains exhaust the default
8 MiB stack: on moss-opt the cliff is about depth 258 (Int) / 250 (user type),
and moss-dev (-O0) crashes near depth 40. With no compiler change, a 4 GiB
`stack_kib` makes depth 500 and 1000 pass. Every `torture_ext` fixture therefore
carries `stack_kib` (independent of the machine's default stack and build
flavor) except the crash fixture, which pins 8 MiB and
`expected_exit_code: 139`. That crash is environment dependent by nature (flavor
and stack size); if a compiler change moves the cliff, re-freeze that one
fixture deliberately. A resident client must provide an equally large stack.

*What an improvement looks like*: these chains grow superlinearly with depth
today (Int chain: 0.7 s at D=100, 2.9 s at D=200, 19.9 s at D=500, 90.6 s at
D=1000, RSS up to 543 MB). With stored per-callable facts, re-checking an
unchanged chain should be close to constant in depth and RSS should not scale
with depth. The cross fixtures should show wall/CPU per extra type falling
toward one cache hit per type, as for B2, instead of a full chain re-walk per
type. (How many functions an *edit* re-analyzes is measured by the B3 edit
sequences below.)

### Shared-callee DAGs (`dag_fanin`, 6 fixtures; `dag_diamond`, 4 fixtures)

See above. *Improvement*: fan-in wall and spec evaluations per type fall toward
one lookup; diamond wall becomes linear in nodes (today it doubles per level).

### Baseline recorded on today's compiler (moss-opt, 16-CPU WSL2, median of 3, quiet machine)

Run `tmp/ect_check/t2r2` of 2026-11-06: loadavg 0.39 at start, calibration
fixture 521 ms at start and 514 ms at the end (drift 1.4%, no warning). Local,
disposable numbers; rerun on your machine and check the noise probe.

- **Shape**: for the 69 DAG/torture fixtures that have a prediction (all but the crash fixture), functions analyzed == callables and
  distinct keys == callables x K exactly (delta 0), e.g. B1 D=50 W=4 K=32: 201
  fns, 6432 keys; B2 K=32: 81 fns, 2592 keys; fan-in K=8: 55 fns, 440 keys, 464
  requests, 24 hits, as predicted.
- **B1 crosses the boundary**: see above (0 DAG functions in `app`).
- **B1 growth (wall ms; consumer, K=1 / 8 / 32)**:

| D, W | K=1 | K=8 | K=32 | ms per type | provider ms |
|---|---:|---:|---:|---:|---:|
| 5, 1 | 53 | 81 | 183 | 4.2 | ~50 |
| 5, 4 | 68 | 187 | 590 | 16.8 | ~61 |
| 20, 1 | 82 | 291 | 1017 | 30.2 | ~70 |
| 20, 4 | 195 | 1078 | 4293 | 132.6 | ~149 |
| 50, 1 | 218 | 1317 | 5233 | 162.1 | ~163 |
| 50, 4 | 763 | 5490 | 22646 | 708.2 | ~577 |

  Provider cost is independent of K. Per key, consumer cost rises with depth
  (about 0.9 ms/key at D=5, W=4, K=32 up to about 3.5 ms/key at D=50).
- **B2 (D=20, W=4)**: slope 117.9 ms/type (intercept 45 ms, R2 0.9998), exactly
  81 spec keys/evals per type: linear in K on a quiet machine (K=1 190 ms, K=8
  974 ms, K=32 3852 ms). The same cell through a package boundary (B1) has
  slope 132.6 ms/type (K=1 195, K=8 1078, K=32 4293 ms), so the boundary adds
  about 15 ms per type (~12%) and no large constant.
- **Fan-in (D=50, W=4), K=1 / 8 / 32**: package consumer 296 / 1968 / 7921 ms
  (provider ~189 ms), single file 317 / 1836 / 7176 ms; 55 fns, 55 / 440 / 1760
  keys, 58 / 464 / 1856 requests, 3 / 24 / 96 hits: linear.
- **Torture extension (4 GiB stack)**: W4 depth 100 / 200 / 500 / 1000 =
  0.72 s / 2.9 s / 19.9 s / 90.6 s (RSS 22 / 43 / 161 / 543 MB); cross d500 K=4
  70.0 s (385 MB); cross d100 K=2/8/32 = 1.4 / 4.9 / 19.9 s. The default-stack
  crash fixture (depth 300, 8 MiB) exits 139 after 4.2 s.
- **Diamond (K=1)**: D=6/8/10/12 = 0.16/0.61/2.35/9.36 s.

## Team 3 benchmarks: edits (B3), cold vs warm (B4), worktrees (B5)

Everything below is generated deterministically by `edit_generators.py`, frozen
in `manifest.json` (key `scenarios`, plus five ordinary fixtures), run by
`run.py bench`/`gate`/`generate.py --validate-only`, and replayable against a
future compiler or warm client **without any fixture or spec change**: a request is
always the fixture's command template (`{moss} check` for files, `margo --check
build` for packages, or `--warm-client-cmd`).

```bash
# the Team 3 pass (quiet machine, moss-opt): fixtures + all scenarios + gate + query evidence
python3 benchmarks/ect_check/run.py bench --reps 5 --no-metrics \
  --ids b3_dag_d20_w4_k02 b3_fanin_d50_w4_k02 b4_floor_file b4_floor_pkg b4_pkgset_p12 \
  --scenario-ids all
python3 benchmarks/ect_check/run.py gate --scenario-ids all      # whole suite + every edit sequence
python3 benchmarks/ect_check/run.py evidence                     # B3 `moss inspect` evidence only
```

`--ids` restricts fixtures; scenarios then run only with `--scenario-ids`
(`all` or a list). With neither flag everything runs. Outputs additionally
include `results_worktrees.csv` (B5) and `evidence.json` (B3).

### Scenario specs (frozen)

An `edit_sequence` is a list of steps over one staged fixture: `check <label>`
(with the expected status), `apply <patch>`, `revert <patch>`. Patches are frozen
`{file, old, new}` edits with the SHA-256 of the whole file before and after.
`scenarios.py` applies them: each `old` must match **exactly once**, the pre- and
post-image hashes must equal the frozen ones, and after `revert` the whole staged
tree hash must equal the fixture's frozen hash (byte-identical or the run aborts).
Edits are never applied by ad-hoc scripts. A scenario row carries the SHA-256 of its
canonical JSON; `generate.py --validate-only` rechecks it, replays every sequence
on the reference compiler and requires each step's expected status, and
`--verify-determinism` regenerates fixtures and scenarios and compares them.

Cold mode runs one discarded full sequence, then `--reps` sequences, each on a
freshly staged tree and an empty build dir; a step sees the state the earlier steps
left (a real edit loop). Warm mode stages once, runs `--warm-primes` full
sequences unmeasured, then the timed ones in place (the tree is back to the base
after every `revert`). **Warm reps repeat the same edits**, so a content-addressed
client may answer reps 2+ from its store; rows therefore also carry
`wall_ms_first` (the first timed sequence). B5 salts its edits per rep for the same
reason (below).

### Stand-in warm client (selftest only)

`selftest.py` contains a tiny memoizing client (`STANDIN`, a verdict cache keyed by
the project tree hash, header "SELFTEST-ONLY STAND-IN") used only to prove that warm
mode, template plumbing, edit invalidation and the sequence gate work (it makes the
warm `base/edit/revert` rows about 20x faster and passes the gate; a mutating client
fails it). It is never used for a baseline claim: **today no daemon exists, the
baseline output says `Warm client: not configured`, and there are no warm rows.**

### B3. Inference Ripple (`b3_edits`, 2 fixtures, 12 sequences)

*Fixtures* (one package each, Moss.toml + `src/main.moss`, K=2 user types,
`main` sums into a `Float` so both an `Int` and a `Float` root result are legal):

- `b3_dag_d20_w4_k02`: the Team 2 spine DAG, D=20, W=4 (81 callables), single package.
- `b3_fanin_d50_w4_k02`: the Team 2 fan-in shape, D=50, W=4 (55 callables); every entry
  reaches one shared chain, so a leaf edit reaches **every** callable.

Leaves have a three-statement body (all other nodes are the Team 2 shapes):

```text
fn n019_0(x):
  # leaf body
  k = 1
  r = x.score()
  return r + k
```

Six frozen patches at leaf `n019_0` (fan-in: `c049`):

| Edit | Change | Class (ideal recheck from the source call graph) |
|---|---|---|
| `a` | `k = 1` -> `k = 2` | local: the leaf only (1) |
| `b` | `k = 1` -> `k = 0.5`: the return type becomes `Float` | cone: the leaf and all transitive callers (DAG 21, fan-in 55) |
| `b2` | `return r + k` -> `return r / k`: `may_panic` now propagates | cone (21 / 55) |
| `c` | the two first statements swapped: only the call's source line moves | local (1) |
| `d` | comment text only | none (0) |
| `e` | one comment line inserted ABOVE the leaf, so every later line shifts by one | identity class: hash-keyed store 0 callables, identity-keyed store (keys built from `semantic_identity` / call-site ids, which embed `@line`) all callables |

*Counters are a proxy for nodes rechecked*: `functions_analyzed`,
`source_function_checks`, specialization evals/keys, `checker_runs`. **Today every
edit rechecks the whole package** (`margo` skips only a package whose sources are
unchanged; there is no per-callable incrementality), so every edit step shows all
81 (55) callables and 162 (110) specialization keys, exactly like the unedited
baseline. The "Ideal" column is the size of the set a perfect incremental checker
would recheck; it is computed by `edit_evidence.py` from the **source** call graph
(reverse reachability from the edited callable) and the declared edit class, never from
generator metadata. "Observed" is what the compiler itself says moved.

*Query evidence* (`run.py evidence`, also part of `bench`): for each edit the fixture
source before and after the patch is queried with `moss inspect fn:<name>` for every
callable (type, ownership, transitive effects, `implementation_hash`,
`semantic_interface_hash`, and the typed specialization records: result/parameter
type keys, observable effects, leaf effects). Result of the final run:

| Edit | Callables whose own body hash changed | Callables whose queried summary facts changed | Provenance (source lines) only |
|---|---|---|---|
| `a` | leaf | none | none |
| `b` (Int -> Float) | leaf | the leaf and all callers (DAG 21 = source cone; fan-in 55 = every callable) | none |
| `b2` (`may_panic`) | the whole cone (see below) | the whole cone (21 / 55): `may_panic` false -> true in `moss effects` of every caller, new `semantic_interface_hash` | none |
| `c` | leaf | none | leaf only (its own `direct_calls[]` line and call-site identity) |
| `d` | none | none | none |
| `e` | none | none | every callable (81 / 55): `semantic_identity`, `source.line`, `direct_calls[].line`, call-site ids all move; the leaves' member-requirement origins too; `implementation_hash`, `semantic_interface_hash` and specialization keys do not |

Observed summary-change sets equal the source cone exactly for `b`/`b2`
(`summary_changed_matches_cone`), so (b) is a consumed fact by every caller up to
the root, demonstrated with the queries, not assumed. Two things surfaced here:
`implementation_hash` of a *caller* changes when a callee's effect set changes (`b2`:
21 hashes move although only the leaf's source changed). By design: the hash covers the
callable's interface material including its own transitive effects, its body and its call
edges (`src/moss.cpp` ~7676-7690), not the callee's text; it is therefore not a function of the
callable's own text alone; and for `b` the callers' `semantic_interface_hash`
and `moss type` do not move (the untyped generic's result is symbolic) while the
**specialization** result type keys do. A store keyed on either hash alone would
miss one of these.

**Edit (c), and the finding for requirement (c).** *No semantic summary fact unread by
callers was found in today's Moss.* Every semantic field of a leaf's inferred summary that
was tried reaches the callers: the return type (`b`), effects (`b2`; also a dead `7/0` or
`[1][5]` statement turns `may_panic` on in every caller), a written parameter
(`probes/literal_param_write.moss`: `ownership` WRITE is visible only on the leaf, but
`local_mutation` still propagates to every caller). Everything that changes only the leaf is
**a-class**: the leaf's own `implementation_hash` moves and no caller-side query output
differs on the DAG or the fan-in. Candidates tried (Agent 2's review, all with zero
caller-side differences): rename `k` or `r`; a dead local (`z = 5`, `[1,2,3]`, a float, a string,
a constructor) in place of the comment line, or changing/renaming one; swapping two non-call
statements; `return k + r`; `k = 3 - 2`; a dead pipeline `.map().filter()`; a dead `z = x.score()`
(which also changes the leaf's `direct_calls` edge fields). Whitespace-only edits and a
trailing-comment change move nothing, not even the leaf hash; a parameter rename changes only
the leaf's own ownership name, `semantic_interface_hash` and member-requirement subject.

Edit `c` (swap the first two statements, same line count) is therefore reported honestly as a
**leaf-local call-site-identity change, a-equivalent for callers**: it changes the leaf's
`implementation_hash` and its own `direct_calls[].line` / `call_site_semantic_identity` and
nothing else in any callable (evidence row `c`; `moss type/effects/ownership/calls/why/cost`
of every caller identical; diagnostics through the leaf cite only `main:<line>`). It is
**not** claimed to be a summary fact that callers ignore, and the line is *not* part of
`provenance_id` (`abi_v10_checked_provenance` hashes declaration and logical-source ids only;
`member_requirements[].origin` is the caller's call site). `moss effects++` per-edge records
should make `a` and `c` recheck the leaf only.

**Edit (e), the line-shift ripple.** One comment line inserted above the leaf changes no hash:
`implementation_hash`, `semantic_interface_hash` and the specialization keys of all 81 (55)
callables are unchanged. But every callable below the edit changes `semantic_identity`,
`source_identity`, `source.line`, `direct_calls[].line`, `callers[].line` and the `@line`-based
call-site ids (81 of 81 DAG, 55 of 55 fan-in; the four leaves also change
`member_requirements[].origin.line`, `call_expression_id` and `requirement_id`). Ideal recheck
class, stated explicitly: **hash-keyed store: 0 callers; identity-keyed store: all.** Today
every `@line`-keyed fact is lost below any edit; a store must key on hashes (or strip line
data) to survive a line shift. Edit `d` (comment text on an existing line) is the control: no
line moves, no query-visible fact moves.

**Fixture disclosure.** The Team 3 leaves have a three-statement body with open arithmetic
(`k = 1; r = x.score(); return r + k`). Complete local summaries are 0/81 (DAG) and 0/55
(fan-in): `moss inspect` reports `open_summaries_complete: false`, 81 `unavailable_open_summaries`,
all `unresolved_effects`; the Team 2 B2 DAG with leaf `return x.score()` has 4/80. The B3 DAG
is therefore **not byte-comparable to B2**, and a store that trusts only `complete` summaries
would never hit on these fixtures. Ideal rechecks are given in callables and in
(callable, spec key) units (x K=2): DAG `b`/`b2` 21 callables = 42 of 162 keys, fan-in 55 = 110 of
110. `main` (the `Float` sum) is also rechecked in `b`/`b2`, but the counters do not count `main`.

*What an improvement should look like.* `a`: recheck about the edited node only
(functions analyzed ~1 of 81, not 81). `b`/`b2`: exactly the consumers' cone (21 of 81
in the DAG; in the fan-in every node, 55 of 55, so no gain is possible there and it
bounds the cost of the slow path). `c`: the leaf only. `d`: nothing rechecked, wall near the
no-op request. `e`: nothing rechecked by a hash-keyed store (identity-keyed: everything). The Analyzed/ideal column should fall from 81x/3.9x/81x/n.a. toward
1.0x on the DAG. The `revert` check returns to the baseline cost (or to a hit).

### B4. Cold vs warm (`b4_cold_warm`, 3 fixtures + the B3 DAG, 4 sequences)

- `b4_floor_file` (`moss check`) and `b4_floor_pkg` (`margo --check build`): `main` that
  only echoes a literal. The startup floor. `margo` is a Python script, so the package
  floor includes interpreter startup and a `.mossi` write.
- `b4_pkgset_p12`: sdk-style set: 12 provider packages x (40 typed exports + 1 exported
  generic) and an app that imports all 12 and calls 2 functions from each (13 packages).
- `b3_dag_d20_w4_k02`: the work-dominated contrast.

Sequence: `first` (empty build dir), `second` (identical tree), one-line edit
(`echo 0` -> `echo 1`, `seed = 7` -> `seed = 8`, leaf `k = 1` -> `k = 2`), `edit`, revert.

*What an improvement should look like* (warm client): startup, parsing and `.mossi`
loading vanish, so the floors approach the cost of a socket round trip; `second`
approaches zero for every project (today `moss check <file>` has no cache at all, and
`margo` skips an unchanged package that has a `.mossi`, but still pays Python startup and
hashing: 44 ms for the DAG package, 114 ms for 13 packages); `edit` becomes proportional to the edit (the
one-line edit in the package set re-walks nothing from the 12 providers; for the DAG,
the edited node only).

### B5. Many worktrees (`b5_worktrees`, 2 scenarios)

One deterministic fixture repository per scenario is built under `tmp/` (never inside
the Moss repository): the fixture tree plus `.gitignore`, one commit with fixed
author, date and message (so the commit id is deterministic: `b5_wt_dag` `11817bbe3d43`,
`b5_wt_pkgset` `177f2f3ccd37`). For N = 1, 4, 16 the runner runs `git worktree add --detach`
N times (real checkouts of that one commit), applies a distinct small uncommitted edit
per worktree from the frozen slot specs (DAG: leaf constant `k` of leaf `n019_{i mod 4}`;
package set: the app's `seed`; values distinct, offset by `rep_stride` x rep so every rep
presents new divergence to a content-addressed client while the base stays shared), then
checks each worktree from an empty build dir, **concurrently** (all N started together)
and **sequentially**, `git worktree remove`s them and prunes. Nothing runs in the main
repository's worktree list or branches (selftest asserts both unchanged, and the repo is
deleted afterwards). Reported per (N, schedule): group wall, **total CPU** (sum of
per-process user+system), the `/usr/bin/time %M` figure summed over the N trees (the
largest single process of each tree, which is margo's Python, **not** moss; **not** a footprint
and not an upper bound), **sampled peak simultaneous RSS of the whole process trees** (a
/proc scanner every ~3 ms, summing the RSS of every process in each worktree's session; run in
separate untimed passes, 3 reps, because the scanner perturbs the machine), every worktree's
first-check time, and counters summed over the N worktrees from a separate sequential pass.

*Contention caveat.* The machine is an i9-13900K exposed to WSL2 as 16 logical CPUs; WSL2
virtualizes the topology, so which logical CPUs share a core is not verifiable here. Concurrent
runs are slower per worktree already at N=4 on 16 CPUs (CPU 1.4x, wall 1.4x the single check),
so CPU saturation is not the explanation; all-core frequency or VM scheduling would fit but
**the cause is not established**. Concurrent CPU totals are inflated for the DAG (12.0 s against
6.7 s sequential) and *lower* for the package set (10.4 s against 13.3 s), so the sign is not
stable either. Use the **sequential** figures for total CPU and for per-worktree first-check
cost; use the concurrent wall and the sampled simultaneous RSS as the multi-agent experience.
Nothing else ran during the measurements.

*What an improvement should look like.* Total CPU and simultaneous RSS grow with divergence,
not with N: the sequential N=16 rows should approach (one full check) + 15 x (cost of
a distinct small edit), not 16 x a full check; the first-check time of worktrees 2..N
should approach lookup cost; the functions-analyzed sum should stop scaling as N x 81.

### Baseline recorded on today's compiler (moss-opt, 16 logical CPUs, 5 reps, quiet machine)

Run `tmp/ect_check/t3final2` of 2026-11-06 (`bench --reps 5`, gate PASS over the five fixtures
and the 16 edit sequences; whole-suite gate PASS over 80 fixtures and 16 sequences in a separate
run): loadavg at start 0.14, calibration fixture 530.7 ms at start and 523.5 ms at the end (drift
1.4%, no warning). Local, disposable numbers; rerun on your machine and read the noise probe.
Warm client: **not configured**.

**B3 (cold process per request; wall ms, identical counters on every row of a shape)**

| Shape | base | `a` | `b` | `b2` | `c` | `d` | `e` | Fns analyzed / source fn checks / spec keys / checker runs |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| DAG D20 W4 K2 edit step | 422.6 | 417.2 | 419.9 | 417.5 | 422.6 | 416.9 | 419.1 | 81 / 81 / 162 / 1 every row |
| DAG revert step | | 417.2 | 416.4 | 417.1 | 424.6 | 419.6 | 421.8 | same |
| fan-in D50 W4 K2 edit step | 642.0 | 645.6 | 645.4 | 641.3 | 643.3 | 635.7 | 636.3 | 55 / 55 / 110 / 1 every row |
| fan-in revert step | | 635.8 | 629.3 | 630.8 | 638.8 | 637.2 | 633.6 | same |

| DAG edit | Ideal recheck (callables / spec keys of 162) | Observed summary change (queries) | Counters (fns analyzed) | Analyzed / ideal |
|---|---:|---:|---:|---:|
| `a` | 1 / 2 | 0 | 81 | 81x |
| `b` | 21 / 42 | 21 | 81 | 3.9x |
| `b2` | 21 / 42 | 21 | 81 | 3.9x |
| `c` | 1 / 2 | 0 (leaf-local call-site identity) | 81 | 81x |
| `d` | 0 / 0 | 0 | 81 | n/a |
| `e` | 0 / 0 hash-keyed; 81 identity-keyed | 0 summary; @line identities of 81 | 81 | n/a |

Fan-in: ideal 1 / 55 / 55 / 1 / 0 / 0 (callables; 2 / 110 / 110 / 2 / 0 / 0 keys of 110) for
`a`/`b`/`b2`/`c`/`d`/`e`; analyzed 55 on all (55x, 1.0x, 1.0x, 55x, n/a, n/a). Shape check:
functions analyzed == callables and keys == callables x K (81 / 162 and 55 / 110, delta 0);
fan-in has 6 hits. Wall time is flat across all edits (the whole package is rechecked every
time) and equals the unedited baseline.

**B4 (wall ms, cold process per request)**

| Project | first | second (no edit) | edit | second: checker runs | edit: fns analyzed |
|---|---:|---:|---:|---|---:|
| `b4_floor_file` | 1.9 | 1.3 | 1.9 | 1 (no cache) | 0 |
| `b4_floor_pkg` | 46.2 | 45.5 | 46.4 | 1: **never skipped** (below) | 0 |
| `b4_pkgset_p12` | 277.3 | 113.6 | 160.3 | 0 (margo skips all 13) | 492 |
| `b3_dag_d20_w4_k02` | 421.4 | 44.2 | 420.1 | 0 (margo skips) | 81 |

*Why `b4_floor_pkg` is not skipped on `second`* (`checked: ['b4_floor_pkg'], skipped: []`):
`margo` (`build_one`, ~line 1091) skips a package in `--check` mode only when the previous
state has a `.mossi` check interface (`has_check_interface`). A package whose only declaration is
`main` emits no `.mossi` (verified: its `build/check-debug/` holds only `.margo-fingerprint.json`,
while a package with exports also holds `<name>.mossi` and `.locations`), so it is rechecked
every time. The DAG package (types and functions) and the package set skip.

*Margo overhead* (median of 11 bare starts): `python3 -c pass` 8.9 ms; `margo --help`
(interpreter start, imports, argparse) 37.2 ms; `moss check` of the floor file, no margo,
1.3 ms; `margo --check build` of the floor package 46.1 ms. About 45 ms of every package row,
including the "second" rows, is margo's Python/driver overhead, which a resident client also
removes.

The package set's `first` costs 881 ms CPU (13 checker runs, 984 functions) in 277 ms wall; its
`edit` re-checks only the app (`source_function_checks` 0 for the app's own run; 492 functions
analyzed = 12 x 41, the provider callable count, so the app's run re-walks the provider
interfaces; the mechanism was not investigated) in 160 ms: 58% of the full build, 3.5x the
package floor. For the DAG the no-edit second check is 9x cheaper than first only because `margo`
skips the package; the one-line edit costs the full check again.

**B5 (cold, median of 5; sequential rows give the clean CPU figure)**

| Scenario | N | Schedule | Group wall ms | Total CPU ms | `time %M` summed (largest process per tree) MiB | **Sampled peak simultaneous RSS, whole trees, MiB** | wt0 / later / slowest first-check ms | Fns analyzed (sum) |
|---|---:|---|---:|---:|---:|---:|---|---:|
| DAG | 1 | seq | 416.5 | 416.3 | 24.4 | 40.1 | 416.5 / - / 416.5 | 81 |
| DAG | 4 | seq | 1696.7 | 1695.1 | 97.5 | 40.7 (one tree at a time) | 433.0 / 419.3 / 433.0 | 324 |
| DAG | 4 | conc | 638.8 | 2390.2 | 97.4 | 159.8 | 588.2 / 603.3 / 605.6 | 324 |
| DAG | 16 | seq | 6732.3 | 6726.8 | 389.7 | 40.8 (one tree at a time) | 416.2 / 421.4 / 428.1 | 1296 |
| DAG | 16 | conc | 809.8 | 11981.6 | 389.6 | **640.2** | 762.4 / 758.0 / 768.9 | 1296 |
| pkgset | 1 | seq | 282.5 | 756.9 | 24.5 | 146.4 | 282.5 / - / 282.5 | 984 |
| pkgset | 4 | seq | 1171.6 | 3381.7 | 98.3 | 154.4 (one tree at a time) | 286.7 / 295.2 / 299.4 | 3936 |
| pkgset | 4 | conc | 411.9 | 3236.9 | 98.3 | 562.8 | 408.6 / 406.6 / 409.3 | 3936 |
| pkgset | 16 | seq | 4671.6 | 13275.8 | 393.4 | 182.4 (one tree at a time) | 289.9 / 292.2 / 299.4 | 15744 |
| pkgset | 16 | conc | 684.7 | 10354.1 | 399.5 | **2109.2** | 665.9 / 658.4 / 672.3 | 15744 |

The `%M` column reports only the largest process of each tree (margo's Python, ~24 MiB), so it
understates the footprint: a sampled DAG tree peaks at about 40 MiB (Python 23 + moss 17), so 16
concurrent worktrees hold about 640 MiB at once, not 390; the package set's tree peaks at
about 150 MiB (margo builds the 12 providers in parallel), so 16 concurrent worktrees hold
about 2.1 GiB. Today nothing is shared: total CPU, simultaneous RSS and functions analyzed grow
linearly with N (DAG N=16: sequential CPU 16.2x one worktree, simultaneous RSS 15.7x, 16x the
functions) and the first check of worktrees 2..N costs the same as the first. N=1 concurrent and
sequential agree (no contention).

### Findings recorded while building B3-B5

- `implementation_hash` of a caller depends on callee effects (`b2` above), so it cannot be
  the only content key for a fact store; and `moss type` of an untyped generic does not show
  a consumed return-type change (only the specialization records do).
- A one-comment-line insertion (`e`) changes every callable's `@line` identity and call-site ids
  (81 of 81, 55 of 55) but no hash and no specialization key; a hash-keyed store is unaffected, an
  identity-keyed one loses everything below the edit.
- Editing a leaf of the spine shape (21 of 81 callables are its cone) gains up to 3.9x
  in recheck set; the fan-in shape gains nothing for a consumed fact because the shared
  chain is reached from every entry.
- The package-level skip in `margo` is the only incrementality today; a no-edit second
  check of a 13-package set still costs 114 ms (Python startup, hashing, graph walk), and a
  main-only package is never skipped.
- A depth-20 DAG program in which a type without `score()` reaches the leaf did not finish
  `moss check` within 130 s (`probes/no_score_deep_d20_w4.moss`), while the 2-level version
  (`probes/no_score_shallow_d2_w1.moss`) fails immediately with `TYPE_AMBIGUOUS_OPEN_RESULT`;
  recorded unclassified in `agent-feedback`, not diagnosed.

## Team 4: agent-loop replay (B6) and summary churn

Moss wants the check part of edit-compile-test near zero per edit. B1-B5 use synthetic
programs; B6 asks the same question of the repository's own history: how long is the
check after each real edit that an agent team made to `sdk/` and `packages/`, and how
often does such an edit move any function summary (the Inference Ripple question)?
Run it with `run.py bench --scenario-ids b6` (the ids `b6_01_a25a6fe1` ... `b6_50_4601a468`,
or the family `b6`, work like every other scenario id); `run.py replay-check [--with-churn]`
is the determinism check, `run.py churn` the churn pass alone, `run.py replay-record` re-records
the reference verdicts (a deliberate re-freeze: then `generate.py --regenerate`).

### What B6 replays

**Range (frozen).** The last 50 non-merge commits, in `main`'s history at
`9b840e8ae8cc99e37915fb15367ca31c47b160f8` (the merge-base of this branch and `main`),
that touched a `.moss` file under `sdk/` or `packages/`
(`git log --no-merges -n 50 <anchor> -- ':(glob)sdk/**/*.moss' ':(glob)packages/**/*.moss'`),
replayed oldest first: `a25a6fe195a7` (2026-10-12) ... `4601a468f89b` (2026-10-28). The exact
SHAs, parents, blob ids and reference verdicts are in `manifest.json` (`scenarios`,
family `b6_replay`, one `replay_step` scenario per commit, each with its own SHA-256) and the
list as a whole has a range spec (`manifest["replay"]`). `bench` and `gate` verify, before running
anything: every selected step's SHA-256, the range spec, that the range selected from git today
equals the frozen list, and that every commit, parent and blob id exists with the recorded content
(history is immutable; a shallow clone or a tampered spec is refused).

**One step = one edit.** For each commit (never a working-tree checkout; nothing is ever
created inside the repository, no `git worktree`):

1. materialize the commit's PARENT tree: `git archive <parent> sdk packages` into a scratch
   directory under `tmp/`;
2. check the entry points: phase `pre` (the baseline; a cold tree, empty build dirs);
3. apply the commit's tracked changes: added, modified and deleted `*.moss` files and
   `Moss.toml` manifests under `sdk/` and `packages/`; every pre-image and every written
   post-image is verified against the frozen git blob id (a drifted tree is refused, never
   silently patched). Other files the commit touched (READMEs, Python/Rust helpers;
   listed per step as `untracked_changes`) and anything outside `sdk/`/`packages/` (the
   compiler itself) are NOT replayed: the compiler is today's;
4. check the same entry points again: phase `post`, **the per-edit latency**.

**Entry points (defined per commit in the spec, derived from git alone).** In execution order:

- standalone `.moss` files that no package owns: `moss check <file> --json` (only the 4 probes of commit #19);
- for every package (nearest ancestor directory with a `Moss.toml`) that owns a changed
  tracked file: `margo --root <package> --check --json build` (through the same command
  template as every other package fixture). margo first checks the package's dependencies
  (all cold in `pre`; in `post` its package fingerprint cache skips the unchanged ones),
  then the package, then `moss test --check` on the root package's test sources (so changed
  `tests/*.moss` files ARE type-checked; Agent 2 confirmed an injected error is found);
- **for every changed `.moss` file under a package's `benches/` or `fixtures/`** (27 of the 179
  tracked changes: 23 benches, 4 fixtures): `env MOSS_MODULE_PATH={modpath} moss check <file> --json`,
  with the package's own and its dependencies' `build/check-debug` interface directories, run
  right after the package's own entry point. **margo `--check` never type-checks these files**
  (an injected `fn f() -> Int: return "x"` in a bench was not detected by margo, and is detected
  by this entry point; `selftest` proves both). A warm client must honour `{modpath}`, or the command
  template for `auxfile` entries, which are the one place the default command is not a plain
  `moss`/`margo` invocation;
- packages that merely DEPEND on a changed package are NOT checked. That is a scope choice
  (an agent normally checks the package it edits), but it means cross-package ripple is not
  measured here;
- a package that does not exist in the parent tree (a commit that adds one) has no `pre`
  phase; a package deleted by the commit has no `post` phase. A **scaffold** (a `Moss.toml`
  without any `.moss` source in the parent: #27 hash, #28 compress, #34 regex, #35
  data_utils_integration) is flagged `effectively_new` together with the 15 commits that add a
  package: 19 commits whose `post` is a first check of a package, not an edit. Reports and
  populations below separate them.

Included in the 50 commits: 116 entry points (85 package checks, 27 bench/fixture file checks,
4 standalone file checks), 179 tracked changes (68 under a package `src`, 53 tests, 27
benches/fixtures, the rest manifests).

**What a latency means.** Per rep, a phase's latency is the sum of its entry points' wall
times; the reported value is the median over reps (5 reps after one discarded sequence; each
rep re-materializes the parent tree, so every rep starts from empty build dirs). Percentiles
over commits use linear interpolation between order statistics (`statistics.quantiles`
'inclusive'); **a population of fewer than 10 commits is reported as a min-max range, never as a
p95.** Counters come from one separate run with the profiling environment (never in a timed
rep) and are summed over the checker runs of a phase; margo skips unchanged packages in `post`,
so `post` counters cover only what was rechecked, and a failing step that stops before the checker
finishes reports none (every counter statistic states its denominator).

**Measurement guards (added after review; all in the runner).**
- *Per-step spread guard.* A phase's rep max/min wall ratio above 1.3 re-measures the whole step, at most 3
  attempts; the kept attempt is the one with the lowest worst-phase spread (ties: earliest), never chosen
  by median. Rows record `attempts`, `attempt_spreads`, `spread_flagged`; the report counts them.
- *Mid-run noise probe.* Steps run in blocks of 5; after each block a combined probe (the single-process
  calibration `b2_k04` plus the multi-process `b4_floor_pkg` through margo, which catches interference in
  process trees that a single-process probe missed) is compared with the same probe taken before the loop.
  A block whose probe drifted more than 10% is discarded and re-measured (up to 2 more times; still noisy
  blocks are flagged `block_noisy`). Probes and drifts are in `results.json` (`meta.block_probes`).
- *Reference pin.* The frozen verdicts were recorded with one `moss-opt` (sha256 `e9a22cf0...`). The report and
  every replay gate result state the reference binary's SHA-256 and say plainly when it is not that binary: a step
  that then differs from its frozen expectation means **re-freeze deliberately** (`run.py replay-record`, then
  `generate.py --regenerate`), not "regression".

**Expected status, not silent skips.** Old trees are checked with TODAY's compiler and many
legitimately fail. Every entry point's `pre` and `post` verdict is frozen in the spec as
`{status, exit_code, diagnostic}`; `bench` flags any deviation, `gate` requires reference == frozen
verdict, and `validate` re-observes it. A failing check ends at the first diagnostic, so its latency is
NOT representative of a check that completes, which is why the results below are split by population. Of
the 194 checks (77 `pre`, 117 `post`), 77 pass and 117 fail; 40 of the 50 commits have at least one failing check.

### B6 results (final quiet run, `results/b6-final/`)

Run `20261106-112733` is the committed one (`results/b6-final/`): `moss-opt`
(sha256 `e9a22cf0...`, the pinned reference), 16 CPUs, 5 reps, suite 0.4.1, loadavg 0.12 at start, calibration
drift 4.8% start-to-end (quiet by the runner's probe). The guards fired: 13 mid-run probes, 3 blocks (steps
26-40) drifted 17-26% because a second session was building on the machine and were re-measured clean; 2 steps
(#43 3 attempts, #49 2) were re-measured for spread; none is still flagged. An earlier run of the same code was
discarded entirely because its last block stayed noisy through all retries and its end-of-run probe drifted 30%.
Startup floors in the same run: `moss check` of a trivial file 1.89 ms, `margo --check build` of a trivial
package 46.8 ms. Run-to-run variation on this shared machine is real: three runs of essentially the same
steps gave all-commits post p50/p95 of 308/2210, 329/2200 and 343/2470 ms (the latter is reported), so differences
under about 10% between two runs are noise.

**All commits (the whole frozen range, failing checks included) next to the clean-edit range:**

| Population | Commits | post p50 ms | post p95 ms | post min-max ms | pre p50 ms | pre p95 ms |
|---|---:|---:|---:|---:|---:|---:|
| **whole frozen range** | 50 | 343.0 | 2470.1 | 57.4-2986.4 | 388.6 | 1887.3 |
| post-edit check passes | 14 | 801.8 | 2734.4 | 63.6-2986.4 | 265.8 | 1365.3 |
| post passes, not new/scaffold (pre may fail) | 7 | 806.5 | n/a (n<10) | 63.6-2986.4 | 265.8 | n/a (n<10) |
| **clean edits**: pre and post pass, not new/scaffold, a checker ran | 4 | 519.3 | n/a (n<10) | 63.6-835.4 | 330.5 | n/a (n<10) |

The clean-edit row is commits #2, #20, #25 and #26 (post 232, 64, 806, 835 ms). Its p95 is not a
result (n=4); read it as the range 64-835 ms. (Commit #49, whose only change is a fixtures file, is not a
clean edit: margo skips it and the fixture entry point fails identically before and after, so it measures nothing.) The whole-range row mixes failures (fast:
they stop at the first diagnostic), first checks of new packages and scaffolds (slow: cold, whole package) and
multi-entry-point commits, and is reported because it is the whole frozen range, not because it is a clean latency.

Distribution of the post-edit latency over all 50 commits (commits per bucket): <50 ms 0; 50-100 ms 8; 100-250 ms 16;
250-500 ms 5; 0.5-1 s 8; 1-2 s 9; 2-5 s 4; >=5 s 0.

Compiler counters of the post-edit check (denominators matter: 14 of the 50 post steps have no counters, all of them
failing steps that stop before the checker finishes, 0 checker runs; medians are over the commits that have counters):
over the 36 commits with counters, median 142.0 functions analyzed, 98.0 source function checks, 314.0
specialization evaluations, 0 specialization keys, 1.5 checker runs (sum of functions analyzed 8530); over the 4 clean
edits, 118.0 / 54.5 / 236.5 / 0 / 2.0.

**Which comparison to make later.** Population percentiles mix failing and passing steps and change composition when
the compiler improves (a step that fails today may pass tomorrow and move from the fast to the slow bucket). The
best future-comparison unit is the **per-step latency of the candidate against the reference on identical steps**
(`results_replay.csv`, one row per commit, `pre_ms`/`post_ms` with the same `status`), judged only on steps whose
status is the same on both sides and whose spread guard passed.

**What an IMPROVEMENT looks like.** B6 is the benchmark where a resident daemon or fact store must show up as the
right-hand tail collapsing toward the left: p95 of the per-edit latency approaching the p50 of an incremental check,
and that p50 approaching the startup floor (about 2 ms for a file request and 47 ms for the margo path today). With
a content-addressed fact store, a post-edit check after an edit whose summaries do not change should analyze only
the edited functions: the `post` "functions analyzed" counter should fall from about 118 per post-edit check (the
clean-edit median over 4 steps; whole packages every time) toward the number of edited functions, with
`checker_runs` per package at most one. The gate must stay green throughout (identical verdicts and diagnostics per
step, `pre` and `post`, between the reference compiler and the candidate or warm client), and a warm client is
measured by the same specs: `--warm-client-cmd` adds a `warm` series (one stable work path, `--warm-primes` full
unmeasured sequences first). The warm series replays the history in the same order every rep, so a content-addressed
client may answer later reps from its store; read `wall_ms_first` next to the median.

### Which commits measure nothing for the edit (disclosure)

- **8 commits change no file under any package's `src`**: #6, #9, #39, #42, #43, #44, #49, #50 (tests, benches or
  fixtures only). With bench/fixture entry points their post checks do run, but: #6, #9, #39, #42, #43, #44 and #49
  fail both before and after for a reason independent of the edit (Char/String drift, a cascading missing dependency
  interface, cbor test-support names, or - #39, #44, #49 - `fixtures/source_free_consumer.moss`, which imports
  `ndarray` although that package is not in the dependency list and so fails identically before and after), so
  their `post` latency is the cost of re-reporting an existing failure, not a check of the edit. Only #50 (cbor
  tests) passes, with `pre` failing, so it is not an edit comparison either.
- **Churn for these commits is vacuous by construction**: the churn pass reads package `src` files only.
- 19 commits are first checks of a package (new or scaffold): their `post` is not an edit.

### Summary churn

For each of the same 50 commits, `churn.py` diffs every function's summary before and after the
edit using only the existing Phase 22 queries (`moss symbols` to enumerate, then `moss type`,
`moss ownership --json` and `moss effects --json` per function by durable entity id; `moss inspect`
carries the same facts but the three focused queries are what is compared). Nothing is computed from
source or parsed from compiler internals; the only text-level step is attributing the diff to
functions (below). The universe is every function in every source file of every package an entry
point checks (all of them, not just the edited ones, otherwise ripple could not be seen), taken after
the entry point's `margo --check` has written the dependency interfaces, with
`MOSS_MODULE_PATH` set as margo sets it.

**Fields compared** (identical function = same durable entity id in both trees):

| Category | Fields |
|---|---|
| types | `type`.`resolved_type`, `type`.`type_facts` (status, resolved, category, origin), each parameter's (name, type) |
| access | each parameter's (name, READ/WRITE/CONSUME) and the `value_boundary` from `ownership`.`ownership_facts` |
| raises | `may_panic` and `raise_set` in `effects`.`observable_effects` and in `effects`.`effect_summary`.`transitive` |
| effects | the other observable-effect flags of those two objects (local_capture_read, local_mutation, domain_read, domain_write, message, external_io, may_diverge, unresolved) plus the summary's status, certainty, transitive_scope and direct_status |

Not compared, on purpose: source line/column and `semantic_identity` (positional; `@<line>`
suffixes inside compared strings are stripped), `implementation_hash` and `semantic_interface_hash`
(whole-function hashes; they move for a caller only when the callee's SUMMARY changes - verified by
Agent 2 on commits #41, #2, #16 and on a synthetic file - so a hash-keyed store sees essentially the same
ripple as this classifier), and the synchronization plan (its graph identity changes with any program
change). Output is deterministic: sorted entity ids, normalized fields, `sort_keys` JSON.

**Edited vs not edited.** A function is *edited* when its source text slice differs between the two trees:
from its declaration line (given by the `symbols` query) to the line before the next non-blank line indented
no deeper than the declaration, trailing blanks trimmed, line ends stripped. That attributes the diff to
functions; it is a location fact, and a comment-only edit inside a body counts as an edit. A function
**rename shows as one function removed and one added** (membership only) and so hides any summary change made
by the rename. The classifier reports separately: functions added or removed (membership), *the edited
function's own* summary changed, and *some other, not edited function's* summary changed (ripple).

**Availability and what the sample is.** A commit is analysable only when every touched unit answered the
queries on both sides, i.e. the tree must check far enough for the compiler to answer: 21 of 50 do; 29 do not
(the same drift as below; each reason is in `churn.json` and the report). Of the 21: 8 add a package or give a
scaffold its first source (nothing can change), 4 are tests/benches/fixtures only (#42, #43, #49, #50: vacuous),
5 only add functions (#20, #25, #26, #30, #32), and **4 change the text of an existing function (#2, #10, #16,
#41): the informative edits.** Of the 22 commits that modify an existing source file of an existing package only 9
are analysable; the missing 13 are the text/csv/fmt/path/hash/regex cone (interface-missing cascades for text,
csv, fmt, path and hash, and the Char/String and `push` type errors), so churn is silent on exactly the
string-heavy packages.

**Headline (informative edits only):** any function summary changed in **4 of 4** (95% Wilson 51-100%); an edited
function's own summary changed in 4 of 4; another, unedited function's summary changed (ripple) in **2 of 4** (95%
Wilson 15-85%, #10: `linalg.get`/`linalg.set`, #41: `cbor.array_push`/`cbor.map_set`, both `raises`). 57 functions
were edited, 37 of them changed summary (34 of the 37 in commit #10, a numerics review pass), and 4 functions that
were not edited changed summary. n=4 cannot distinguish "rare" from "common"; the other 9 analysable non-new
commits are additions or vacuous and carry no information about ripple.

Secondary figure only (diluted by the vacuous and addition-only commits), over the 13 analysable commits that are
not new packages: any change 4 / 13 (31%, Wilson 13-58%), own 4 / 13, ripple 2 / 13 (15%, 4-42%); no summary changed
at all 9 / 13. By category (any / edited / other), over the 13: types 1/1/0, access 1/1/0, raises 4/4/2,
effects 1/1/0. Functions whose summary changed per analysable non-new commit: p50 0, p95 16.2, max 36, mean 3.2.

**What this means for a summary-aware store (not an "improvement" metric: churn is a property of history).** The
only informative evidence is the 4 edits: every one moved some summary and half reached an unedited function, so a
store must follow a changed summary to its callers rather than stop at the edited function, but in these 4 cases it
would touch a handful of functions (2-36 changed per commit, 34 of them in one commit), not the whole package. The
other 9 analysable commits (additions and tests-only) say nothing about the ripple; they only show that an addition
or a tests-only edit moves no existing summary. Low churn would mean a summary-aware store pays off; this sample is
too small, and silent on the string-heavy cone, to say whether churn is low.

### Determinism, gate and self-tests

- `generate.py --verify-determinism` regenerates the whole suite, including the 50 steps from git plus the recorded
  expectations, and compares it with the manifest; `generate.py --validate-only` re-observes every step with the
  reference compiler (blobs, applied tree equals the commit's tracked tree, every frozen verdict).
- `run.py replay-check --with-churn` runs every step twice on moss-opt: the classification (status, exit code,
  diagnostic of every check) and the per-phase counters must be identical on the repeat for all 50 steps, and the
  churn records of all 50 commits identical across two complete churn passes; each step must also meet its frozen
  expectation.
- The gate treats every entry point of every step, `pre` and `post`, as a check step compared on the whole JSON
  verdict (volatile keys and margo's `checked`/`skipped` bookkeeping excluded, shown as information) and requires
  reference == frozen expectation, and reports the reference binary's SHA-256 (see the reference pin above).
  `selftest` proves it fails a mutating or diagnostic-changing client and passes an identity client and, for the
  plumbing only, a stand-in memoizing client.
- `selftest` also checks the churn classifier on synthetic input, end to end through the real queries (a
  comment-only edit is an edit with no summary change; a deliberately injected body change is detected as an own
  change and as a ripple on its caller while an unrelated function is untouched), the spread guard, the mid-run noise
  blocks and their bounded retries, the reference-pin message, `effectively_new`, the bench-file entry point finding
  an injected type error, spec forgery (a re-hashed forged expectation is refused by the frozen range; a wrong blob
  id; a drifted pre-image), cold rows and counters, and that no worktree is left registered.

### Limits of the method

- **Old commits, today's compiler.** The compiler and SDK evolved together; checking an old tree with today's
  compiler measures neither the old nor the new world. 40 of the 50 commits have at least one failing check today
  (117 of 194 checks). Diagnosed causes (Agent 2's review reproduced them by hand through `git archive` and a full
  git worktree): the dominant one is commit `983acb2`, which made `String.chars()` / `char_at` return `Char`; trees
  before commit #45 (`7bf962d2`, whose message fixes the SDK for it) fail with `TYPE_MISMATCH` "push argument
  conflicts with the element type of 'parts'/'pieces'" (49 checks, at `sdk/text` `text.moss:78`
  `parts.push(chars[start+i])`) in text and everything that depends on it: fmt, json, parse, path, tables. The same
  kind: `packages/time` (5, `pad_digit` returns `char`), `packages/regex` (9, `add_tok` argument `esc_ch`), `Char`
  vs `String` comparisons and `assertEqual` operand types (5), and `String find expects string` in hash/regex (7).
  Other classes: cbor test-support names not found (8, `cbor_test__same_ints`), a `TYPE_MISMATCH` on binding
  `ignored` in the numerics tests of linalg/sparse/tables (11; a genuine repository defect also present at the
  anchor), "Moss.toml with no source" scaffolds (3), dependency interface missing because a dependency failed (10),
  and in commit #19 the host-I/O feasibility probes (`env_probe`/`process_probe`: unknown `Env`/`Process`;
  `unbounded_range_write`: a deliberate `FILEIO_UNBOUNDED_REQUEST` negative), plus
  `fixtures/source_free_consumer.moss` (unknown `ndarray`, 7). That the older compiler accepted these trees rests on
  commit messages, **not on a rebuilt old compiler**. Failing checks stop at the first diagnostic and are fast; they
  are in the whole-range row and excluded from the others.
- **Entry points.** Package checks of the packages that own a changed file plus the bench/fixture file checks;
  dependents are not checked, and `moss test --check` runs only for the root package of an entry point. Only
  `.moss` and `Moss.toml` changes are replayed. The disclosure above lists the commits that measure nothing for the
  edit.
- **Cold, per step.** Every step starts from empty build dirs and ends one edit later; the real loop (many edits in
  a row in one tree) is what the warm series is for. The steps are independent, not cumulative.
- **Sample.** 50 commits by many authors in a few weeks, mostly large feature commits (new packages, bulk
  additions), not the one-function edits of an agent loop; B3 is the controlled version of that. The churn sample is
  4 informative edits.
- **Textual "edited".** A function whose text did not change but whose callee in another file did is not edited; a
  reformatted function is; a rename hides a summary change (above). Entity ids are per module name; two functions with
  the same id in one commit would be disambiguated by order.
- **Mechanisms not established.** Why individual post checks cost what they do (for example #50: 3.0 s and 628
  functions analyzed over 5 checker runs for a cbor package whose pre check failed after 1.1 s, so the two are not
  comparable) was not investigated; the counters show what was analyzed, not where the time went.

## Team 5: Rust comparison and the B3 error-path edit

```bash
# the Team 5 pass (quiet machine, moss-opt): Moss B3 a-f + the B4 DAG sequence + all Rust sequences + gate
python3 benchmarks/ect_check/run.py bench --reps 5 --no-metrics --ids b3_dag_d20_w4_k02 b3_fanin_d50_w4_k02 \
  --scenario-ids b3 b4_seq_dag_d20_w4_k02 rust_edits --out <dir>
python3 benchmarks/ect_check/run.py gate --scenario-ids all      # whole suite incl. Rust (cargo reference vs candidate)
python3 benchmarks/ect_check/run.py gate --scenario-ids rust_edits --rust-candidate-cmd '<template with {cargo} {path}>'
```

If `cargo` is missing, every Rust step prints one `SKIP Rust comparison: ...` line and the Moss part runs
unchanged (selftest covers it). Rust runs offline and std-only: `cargo check --offline --locked
--message-format=json`, `CARGO_HOME` and `CARGO_TARGET_DIR` under `tmp/` (nothing in `~/.cargo`),
`CARGO_*`/`RUSTC*`/`RUSTFLAGS` scrubbed, `CARGO_INCREMENTAL` left at its default (cargo then passes `-C
incremental=` to rustc: verified with `cargo check -v`). The toolchain is resolved through `rustup which` so
the rustup proxy is not in the timed process tree. Toolchain of the committed run: cargo 1.98.1, rustc 1.98.1.
Rust fixtures live under the manifest key `rust_fixtures` (4 fixtures, `kind: cargo`), their 19 sequences are
`rust_edit_sequence` scenarios; Moss loops never see them.

### What was built

Generator `rust_generators.py` (`rust_dag`, seeded like every other generator, SHA-256 frozen in the manifest,
covered by `--validate-only`, `--verify-determinism` and selftest). Idiomatic: each node is a free function
generic over one trait bound, no macros, no `dyn`:

```rust
pub trait Score { fn score(&self) -> i64; }
pub fn n019_0<T: Score>(x: &T) -> i64 { /* leaf body */ let k = 1; let r = x.score(); r + k }
pub fn n018_0<T: Score>(x: &T) -> i64 { n019_0(x) + 1 }
pub fn root<T: Score>(x: &T) -> i64 { n000_0(x) + n000_1(x) + n000_2(x) + n000_3(x) }
```

K=2 plain structs implement `Score`; `main` sums `root(&T) as f64` over them (like the B3 `main`).

| Fixture | Layout | Moss counterpart |
|---|---|---|
| `rust_dag_single_d20_w4_k02` | one package, one crate (`src/main.rs` + `mod dag`) | `b3_dag_d20_w4_k02` (single package) |
| `rust_dag_ws_d20_w4_k02` | workspace: `dag_lib` (DAG, exports `root`) + `dag_app` (types, `main`), path dependency | B1 lib + app, same D/W/K |
| `rust_fanin_single_d50_w4_k02` | one crate, fan-in shape | `b3_fanin_d50_w4_k02` |
| `rust_floor_single` | `main` that prints a literal | `b4_floor_*` |

Node counts are 81 / 81 / 55 callables, equal to the Moss ones. The fixtures are warning-free. Fan-in in the
workspace layout was not built (scope).

### The edits (same positions as B3) and what translates

| Edit | Moss (B3) | Rust equivalent | Translates? |
|---|---|---|---|
| `a` | `k = 1` -> `k = 2` at the leaf | `let k = 1;` -> `let k = 2;` | yes, one-line body edit |
| `b` | `k = 0.5`: the leaf's inferred return type becomes Float, the cone follows by inference (21 / 55 callables) | leaf `-> f64`, `let k = 0.5;`, `r as f64 + k`; **and every function in the call cone is edited by hand** (`-> f64`, `+ 1.0`; non-cone root terms get `as f64`): 21 edits in the DAG, 55 in the fan-in | yes, as written: Rust has no return-type inference across function boundaries, so without changing the signatures' form the same semantic change needs the 21/55 explicit signature edits. A one-site variant exists (a `type Num = i64;` alias used in every signature, edit = the alias line plus the leaf body); it is a different fixture style (every signature spelled through an alias) and was **not built** |
| `b2` | `r / k`: `may_panic` propagates to every caller | none built | **not built, not impossible**: Rust has no inferred effects, so the closest analogue is a fallible leaf (`-> Result<i64, E>`, `?` at every call site), a cone edit of the same size as `b` (explicit signatures again). The literal text edit `r / k` is just another body edit, equivalent to `a` for `cargo check` |
| `c` | the first two statements swapped | the same two `let` statements swapped | yes |
| `d` | comment text only | `// leaf body` -> `// leaf body (reworded)` | yes |
| `e` | one comment line inserted above the leaf (line shift) | the same | yes (cargo/rustc use no line-keyed fact store, so the question is moot; measured anyway) |
| `f` | **new**: error path, see below | `+ 1` -> `+ "one"` in the top node (`i64 + &str`, E0277) | yes |

Every sequence is `first` (empty target), `second` (no change), apply, `edit`, revert (byte-identical tree
enforced), `revert`. The Moss sequences are the existing B3 ones (`base`, `edit`, `revert`) plus the B4 DAG
sequence for a Moss `second`.

### B3 edit `f`: the error path (Moss and Rust)

A type error in one DAG node; frozen expectations: `edit` fails (Moss `margo` exit 1, `cargo` exit 101), `revert`
passes. Scenarios `b3_dag_f`, `b3_fanin_f` (family `b3_error`; the frozen `b3_edits` rows are untouched) and
`rust_*_f`. The step spec gained an optional `expect_exit`; the existing gate compares the whole failing JSON
verdict (margo's embedded compiler error, parsed structurally) and the selftest checks that a wrong exit code
does not satisfy the expectation.

*What was chosen and what was ruled out* (`probes/f_edit_probe.py`, results `probes/f_edit_probe.txt`): a break
in the **leaf** (string constant, undefined name, unknown function, missing method, wrong return annotation,
returned string) got no answer from the Moss check within 20 s on both shapes (the unbroken check takes ~0.45 s /
~0.67 s); this is the known quirk, not diagnosed. A break in the **top node** (`n000_0` / `e0`) answers in 0.3-0.55 s,
and the time grows steeply with the break's distance from the root (DAG n005 2.2 s, n010 over 20 s; fan-in c001 2.2 s,
c010 over 20 s). So `f` breaks the top node, the least representative place for an edit-loop error. The Moss
diagnostic is `TYPE_AMBIGUOUS_OPEN_RESULT` on `main__root` ("unresolved result type"), not the string/Int mismatch;
cargo reports E0277 "cannot add `&str` to `i64`" at the expression.

Measured (Moss: `edit` fails, counters absent; the compiler prints none for a failing check): the failing Moss check
is cheaper than the passing one (DAG 321 ms against 432-456; fan-in 569 against 658-698), and the **`revert` after
the failure costs 48 ms with 0 checker runs**, i.e. (inferred, not traced) margo answers it from the last good interface (same figure as the
B4 no-change second, 46 ms); the Rust failing check is 26 ms, and the revert is a normal 20-31 ms recheck. For
Rust in the workspace layout the failing `lib` unit stops the build, so the `app` unit is not attempted
(1 unit rechecked, against 2 for a passing edit).

*What an improvement looks like (error path)*: the failing check should cost about what a passing check of the edited
cone costs and, more importantly, must not depend on how deep the broken node is: the leaf breaks that do not answer
today must answer in the same order of time, with a diagnostic that names the broken node. The revert after the
failure should stay at or below today's 48 ms. The gate must keep comparing the whole failing verdict.

### Results (committed run `results/team5-final/`)

Run `20261106-165237`, moss-opt sha256 `e9a22cf0...fe4d36` (the pinned reference), 5 reps, one discarded warm-up
sequence, loadavg 0.27 at start; calibration fixture 548.4 ms at start and 537.5 ms at the end (drift 2.0%, no
warning). Rust spread guard (max/min wall over reps <= 1.3, at most 3 attempts, mid-run noise blocks): no step flagged,
no block noisy; 8 rows needed a second or third attempt. The largest Moss step spread over its 5 reps is 1.19 (no
Moss step exceeds 1.3; the Moss B3 sequences have no guard of their own, this was computed from the samples). Cells
are wall ms / CPU ms / peak RSS MiB (`/usr/bin/time %M`, largest single process of the tree). **Moss rows read 24 MiB
everywhere, consistent with margo's Python being the largest process; moss's own peak RSS is not shown (not
established).** Rust %M is the largest single process, rustc, except where noted. The whole-tree sampled peaks (separate
untimed passes) are in `LOCAL_REPORT.md`: Rust 74-117 MiB.

**D20 W4 K2, step after each edit**

| Edit | Moss `margo --check build` | Rust single crate | Rust workspace (lib + app) |
|---|---|---|---|
| `a` | 455.7 / 454.9 / 24 | 21.5 / 18.2 / 79 | 32.5 / 26.3 / 77 |
| `b` | 435.8 / 435.8 / 24 | 22.9 / 20.4 / 85 | 35.4 / 29.4 / 81 |
| `c` | 448.2 / 447.7 / 24 | 21.6 / 19.1 / 79 | 31.6 / 27.4 / 77 |
| `d` | 439.1 / 438.7 / 24 | 21.9 / 18.6 / 80 | 32.4 / 27.4 / 77 |
| `e` | 447.1 / 446.6 / 24 | 22.5 / 19.3 / 78 | 33.7 / 27.6 / 81 |
| `f` (fails) | 321.3 / 320.9 / 24 | 26.2 / 24.5 / 96 | 26.4 / 23.9 / 96 |

**Fan-in D50 W4 K2, step after each edit**

| Edit | Moss | Rust single crate |
|---|---|---|
| `a` | 698.1 / 697.5 / 24 | 20.6 / 17.4 / 79 |
| `b` | 871.3 / 871.3 / 24 | 25.9 / 22.7 / 85 |
| `c` | 700.0 / 699.8 / 24 | 21.8 / 18.1 / 79 |
| `d` | 656.8 / 656.8 / 24 | 20.5 / 17.7 / 80 |
| `e` | 655.8 / 655.4 / 24 | 21.4 / 17.8 / 77 |
| `f` (fails) | 568.6 / 568.3 / 24 | 26.2 / 23.0 / 96 |

**First, second (no change) and revert**

| Project | Step | Moss | Rust single | Rust workspace |
|---|---|---|---|---|
| DAG | first | 451.6 / 451.1 / 24 | 39.4 / 34.1 / 82 | 50.7 / 42.2 / 80 |
| DAG | second | 46.0 / 45.6 / 24 | 7.7 / 7.1 / 23 | 7.8 / 7.0 / 24 |
| DAG | revert | 446.5 / 445.3 / 24 | 21.2 / 18.1 / 79 | 32.3 / 27.4 / 77 |
| Fan-in | first | 670.9 / 670.6 / 24 | 38.2 / 33.4 / 82 | n/a |
| Fan-in | second | not measured (no B4 fan-in sequence) | 7.5 / 6.9 / 23 | n/a |
| Fan-in | revert | 691.9 / 691.6 / 24 | 20.5 / 17.0 / 79 | n/a |

Startup floor: Rust floor crate first 31.8 / 27.5 / 71, second 7.6, after a one-line edit 17.9 ms; Moss floor in the
B4 table (46 ms through margo). Work done, from cargo's own bookkeeping: single crate `first` 1 unit checked;
`second` 0 checked, 1 reused; every edit and every revert 1 checked, 0 reused; workspace: `first` 2 checked, `second`
0 / 2 reused, every edit and revert 2 checked (editing `dag_lib` makes `dag_app` stale as well). Moss, same runs: every
passing step 1 checker run, 81 (DAG) / 55 (fan-in) functions analyzed, 162 / 110 specialization keys, except `second`
(0 runs, margo skipped the package). The Rust unit counters are `Checking` lines on stderr and `fresh` flags of
`compiler-artifact` messages: they are cargo's freshness bookkeeping, not an amount of type-checking work, and no
finer counter exists on stable.

*Reading the Rust numbers.* At this size `cargo check` is within ~2x of its own startup floor (floor crate edit step
17.9 ms, D20 edit 21-23 ms, `first` 31.8 against 39 ms), so **differences between Rust edits are small and mostly
noise-sized; the only large Rust effects are "nothing to do" (about 7.5 ms) against "one unit rechecked" (20-35 ms)**.
The number of user types K did not matter in a separate probe (`probes/rust_k_probe.txt`: K = 2 / 8 / 32 gives 39.7 /
40.9 / 43.0 ms for the first check; run at load 1.5, so approximate). Moss, in the same units, is 0.45 s DAG / 0.67 s
fan-in for every edit and `a`-`e` are indistinguishable (the whole package is rechecked every time, as in the Team 3 run).

*What an improvement looks like (this comparison)*: not that Moss matches a Rust figure. The question this section
answers is "what does an incremental checker with crate-level freshness do after these edits": it does no work after a
no-change request (Rust 7.5 ms, Moss 46 ms through margo), pays one unit per edit regardless of the edit class, and has no
content-based cutoff (below). A Moss with a fact store should make `a`, `c`, `d` and `e` cost about a lookup plus the
leaf, `b` the cone, and the failing edit `f` no more than a passing one; the Moss column should move toward the Rust
column on `d`/`e`/revert (cargo does not get those for free either) and beyond it where Rust has no answer: a content-keyed
store would make the `revert` and `d` steps cost a hit, which cargo's mtime freshness cannot (next section).

### Where Rust and Moss differ (and what a reader may conclude)

- **Separate compilation, crate-level freshness.** A crate is Rust's unit of checking and of reuse; cargo decides per
  unit whether to run rustc. For path packages that decision is **mtime-based**, measured by
  `probes/rust_freshness_probe.txt`: a no-change run reuses the unit; `touch` and rewriting identical bytes recheck it;
  a content change with the mtime reset to the old value is reported fresh (stale). So a revert, a comment edit and a
  line shift all cost one recheck of the whole crate and the only reuse is "nothing changed": there is no content
  hash and no per-function reuse in cargo's freshness. In a two-crate workspace an edit in `dag_lib` rechecked
  `dag_app` in every edit/revert step (2 units), although only a body changed: not checked whether the library's
  metadata changed; cargo reuses by the dependency's fingerprint. Within a rustc run, incremental compilation is on at
  the default (`-C incremental`), so rustc may reuse query results internally; that part is not measured separately.
  Moss reuses at package level too (margo skips an unchanged package with a `.mossi`) and nothing finer today.
- **Generic bodies.** Rust checks a generic function body once, against its declared trait bound, at its definition;
  `cargo check` does not instantiate generics (monomorphization belongs to code generation, which check skips), so K has
  no multiplier (K probe above). Moss's untyped generics have no declared bound, and the checker walks the body again for
  each concrete type: Team 2 measured functions analyzed = callables and specialization keys = callables x K, with wall
  linear in K (B2: 118 ms per type). That is the structural reason the K axis exists in the Moss benchmarks and is
  absent here. The measured Rust times do not show *why* Rust is fast at this size (the process startup floor dominates);
  the "once per definition" statement is a property of the language design, the K-independence is what was measured.
- **Explicit signatures.** Rust return types are written, so a cone-wide type change (edit `b`) is a 21/55-site edit and
  an error is reported at the offending expression; Moss infers them, so the same semantic change is one edit but every
  caller's summary changes (B3 evidence). Rust has no inferred effects (`b2`).
- **What `cargo check` skips and rechecks.** It runs rustc to metadata: parsing, name resolution, type checking with trait
  solving, and MIR borrow checking; it skips code generation, optimization and linking, and errors that only appear after
  monomorphization. `moss check` runs the Moss semantic checker (inference, specialization, ownership/effect inference);
  borrowck and trait solving are different analyses with different costs, so a per-function cost cannot be transferred.
- **NOT apples-to-apples.** Different languages, different checkers, different feature sets, different process models
  (cargo + rustc against margo's Python + moss), different workload (the Rust crates are the same *shape*, not the same
  program), 81 / 55 tiny functions. Moss rows include margo's Python startup (~45 ms, see B4); the Rust rows include cargo's.
- **A reader may conclude:** at this shape and size, with the compilers installed here, `cargo check` is roughly 20-35x
  faster than `margo --check build` after any of these edits (DAG `a`: 21.5 against 455.7 ms; fan-in `a`: 20.6 against
  698.1 ms) and about 6x faster when nothing changed (`second`: 7.7 against 46.0 ms), does no work when nothing changed, reuses nothing finer than a crate, and does not depend on K.
  **A reader may not conclude:** that Rust's type checking is faster per function, that Moss's checker algorithm is
  inherently slower, anything for larger or non-generic programs, anything about code generation or whole-program
  edit-compile-test time, or any cause of the gap beyond the K structure and startup floor stated above (the cause of the
  Moss per-package cost was measured by counters in Team 2/3, the Rust per-unit cost was not decomposed).

### Fixtures not expressed, and not built

- **Not expressible in Rust without explicit signature edits:** `b2` (`may_panic` propagation) and `b` (inferred return
  type change): Rust has no return-type or effect inference, so both need explicit signature changes through the cone
  (`b` built as such; `b2` as a `Result`/`?` cone edit and the `type Num = i64;` alias variant of `b` were not built); a leaf break for the Moss error path is expressible in Rust (E0277) but not
  measurable in Moss (no answer within 20 s), so `f` is at the top node for both.
- **Not expressible in Moss:** none found.
- **Not built (scope):** Rust counterparts of the B1/B2 K and D sweeps (one K probe only), the torture chains, the
  diamond shape, fan-in as a workspace, an edit on the app side of the workspace, B5 worktrees and B6 replay for Rust
  (Rust sources of the repository are not part of the benchmark), a Moss no-change `second` for the fan-in shape.

### Gate, selftest

- Rust gate: per check step, exit status + `build-finished` success + every diagnostic (level, code, message, primary
  span file/line/column; paths normalized to `<FIXTURE>`) must be identical between reference and candidate, and the
  reference must meet the frozen step expectation (status and exit code). `rendered` text and cargo's fresh/dirty
  counts are information. A candidate whose rustc prints positions differently needs the same position mapping as
  described for the Moss gate (L8 requirement above). Candidate command: `--rust-candidate-cmd` / `$ECT_RUST_CANDIDATE_CMD`
  (`{cargo}` `{path}` `{root}` `{id}`).
- `run.py selftest` (70/70) adds: Rust determinism and frozen manifest equality, a tampered sequence spec refused, a
  modified fixture tree detected, the `f` spec (fail + exit 1, revert pass) and exit-code strictness, verdict
  normalization (paths/rendered/fresh ignored, a changed error code not), missing cargo (one-line message, skip), an
  identity cargo candidate passing the gate (including the failing `f` with E0277) and a candidate that injects a type
  error being caught.

### What an improvement looks like (B1-B6 index)

Each benchmark states it where it is described: B1 (`.mossi` firewall: consumer cost independent of D and K), B2 (per-type
slope toward one lookup), torture extension (chains near constant for unchanged code), B3 (rechecked set equal to the
ideal cone; `f`: the error path above), B4 (floors approach a socket round trip; `second` about zero), B5 (CPU and RSS grow
with divergence, not with N), B6 ("What an IMPROVEMENT looks like" in the B6 results), Rust comparison (above).

### Behaviour changes in 0.5.0 (read before upgrading a runner)

- **Default `bench` and `gate` now also run the Team 5 scenarios**: the 2 Moss `f` sequences and the 19 Rust (cargo)
  sequences (`--ids` still disables scenarios unless `--scenario-ids` is given). A whole-suite gate therefore needs cargo
  (or prints the FAIL-OPEN line below); the gate's sequence counts are 68 Moss + 19 Rust.
- **Missing cargo is fail-open and loud**: one `*** FAIL-OPEN: Rust comparison SKIPPED (N sequences NOT RUN) ***` line on
  stderr, the same warning on the final `gate:` line, `rust_skipped` in `gate.json`/`results.json`, and a "FAIL-OPEN"
  paragraph in the report's Rust gate section. The exit status is unchanged (the Moss part still passes); use
  `--require-rust` to make a missing cargo an error.
- **Minimum toolchain**: the committed `Cargo.lock` files are lockfile version 4, which needs cargo >= 1.78; the committed
  run used cargo/rustc 1.98.1. An older cargo fails `--locked` with a lockfile error, not a check difference.
- **`results.csv` schema**: nine columns were added (`tree_rss_peak_kib_median`, `units_checked`, `units_fresh`, `spread`,
  `attempts`, `spread_flagged`, `block_attempts`, `block_noisy`, `diagnostic`); old columns are unchanged. Rust rows have
  `kind` = `cargo` and `units_*` filled, Moss rows leave them empty. B3/B4 Moss rows now also carry a sampled
  whole-tree peak RSS (separate untimed passes, 3 sequences per scenario; adds run time).
- **What the Rust gate does not compare**: rustc's `children` (notes/help sub-diagnostics), `suggestions`, span end
  line/column, span `label`, `rendered` text and cargo's fresh/dirty counts. It compares level, code, message and
  every span's file, start line, start column and primary flag. A candidate that changes only the dropped fields passes.
- **Step expectations** may carry `expect_exit` and `expect_code` (the error code must be among the verdict's
  diagnostics); both are frozen in the specs of the `f` sequences and enforced by bench, gate and validate.

### Limits (Team 5)

- One toolchain (cargo/rustc 1.98.1), one machine, one run of 5 reps (some rows 3 attempts under the guard); noise probe clean.
- Rust times are 20-50 ms: near the process-startup floor and the resolution where `/usr/bin/time` and the runner's polling
  matter; differences below ~2 ms are not meaningful.
- Moss RSS is margo's Python (probably); Moss counters are absent for failing checks; the Rust "units" are bookkeeping.
- The Rust edit `b` is a different-sized edit than the Moss one (by language design); `b2` has no Rust counterpart.
- Edit `f` is at the top node because leaf breaks gave no answer within 20 s on Moss; error-path conclusions about leaf errors are not
  available for Moss.
- The cause of the missing Moss answer on deep breaks and of the margo revert-after-failure skip was not investigated.

## Fixture-shape constraints and known quirks

- **Do not nest calls of inferred-generic functions.** A call of an inferred
  generic inside another inferred generic's body, e.g. `fn bump(x): return x + 1`
  / `fn twice(x): return bump(bump(x))`, is rejected by `moss check` with
  `MOSS_INTERNAL_OR_IO_ERROR: internal synchronization invariant: unresolved
  state layout` (pre-existing on main, moss-opt and moss-dev; reproduced by
  Agent 2; valid program rejected, compiler defect, see
  `agent-feedback/phase28-5/ect-bench/harness/`). `bump(bump(3))` from `main`,
  `bump(x) + bump(x)`, and a typed `bump` called from an untyped caller pass.
  Generated DAG nodes must therefore call their children as single,
  non-nested calls (`var y = bump(x)` then `return bump(y)`).
- Package-boundary generics with distinct user types (`export fn first_of(a, b)`,
  `show_id(x)`; two user types in the app) check and run today; see
  `trivial_pkg_generic_types`.
- `packages/linalg`, `packages/sparse` and `sdk/tables` fail their test-target
  semantic check under `margo --check` today (`TYPE_MISMATCH` on binding
  `ignored`); they are excluded from the metrics and listed in the report.
