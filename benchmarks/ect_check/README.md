# ECT Check Benchmark Suite

**Phase 28.5, Team 1 (harness and measurement plumbing), Team 2 (generic-DAG
fixtures), Team 3 (edit sequences B3, cold/warm B4, worktrees B5) and Team 4
(agent-loop replay B6 and summary churn).** Suite version 0.4.0 (0.3.0 plus the
Team 4 replay specs; every earlier fixture and scenario is byte-identical,
hashes unchanged).

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
  (`trivial_pkg_fail` exercises this). The reference must also agree with the
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
list as a whole has a range spec (`manifest["replay"]`, SHA-256 `f0f3b6bc...`). `bench` and
`gate` verify, before running anything: every selected step's SHA-256, the range spec,
that the range selected from git today equals the frozen list, and that every commit,
parent and blob id exists with the recorded content (history is immutable; a shallow
clone or a tampered spec is refused).

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

**Entry points (defined per commit in the spec, derived from git alone).**

- for every package (nearest ancestor directory with a `Moss.toml`) that owns a changed
  tracked file: `margo --root <package> --check --json build` (through the same command
  template as every other package fixture). margo first checks the package's dependencies
  (all cold in `pre`; in `post` its package fingerprint cache skips the unchanged ones),
  then the package, then `moss test --check` on the root package's test sources. Entry
  points of one commit run one after the other in sorted order in the same tree, so a
  later package's dependencies are already built by an earlier entry point;
- for every changed `.moss` file that no package owns: `moss check <file> --json`
  (only the 4 probes of commit #19);
- packages that merely DEPEND on a changed package are NOT checked. That is a scope choice
  (an agent normally checks the package it edits; checking dependents multiplies the cost
  and depends on which dependents one picks), but it means cross-package ripple is not
  measured here;
- a package that does not exist in the parent tree (a commit that adds one) has no `pre`
  phase (the `post` check is then a first, cold check, not an edit); a package deleted by the
  commit has no `post` phase.

Included in the 50 commits: 89 entry points (85 package checks, 4 file checks), 179 tracked
changes; 15 commits add at least one package, 8 change no file under any package's source
directory (tests, benches, fixtures or manifests only), 17 touch a `Moss.toml`.

**What a latency means.** Per rep, a phase's latency is the sum of its entry points' wall
times; the reported value is the median over reps (5 reps after one discarded sequence; each
rep re-materializes the parent tree, so every rep starts from empty build dirs). Percentiles
over the 50 commits use linear interpolation between order statistics (`statistics.quantiles`
'inclusive'). Counters come from one separate run with the profiling environment (never in a
timed rep) and are summed over the checker runs of a phase; margo skips unchanged packages in
`post`, so `post` counters cover only what was rechecked.

**Expected status, not silent skips.** Old trees are checked with TODAY's compiler and many
legitimately fail. Every entry point's `pre` and `post` verdict is frozen in the spec as
`{status, exit_code, diagnostic}` (the first diagnostic's code and message; for a margo failure
the embedded compiler diagnostic); `bench` flags any deviation, `gate` requires reference ==
frozen verdict, and `validate` re-observes it. A failing check ends at the first diagnostic,
so its latency is NOT representative of a check that completes, which is why the results
below are split by population. Of the 149 checks (60 `pre`, 89 `post`), 57 pass and 92 fail;
the failing ones are dominated by the SDK/compiler drift described under Limits.

### B6 results (final quiet run, `results/b6-final/`)

Run `20261106-072632`, `moss-opt` (SHA-256 `e9a22cf0...`), 16 CPUs, 5 reps, loadavg 0.25 at start,
calibration drift 1.3% (quiet by the runner's probe). Everything below is read off
`results/b6-final/LOCAL_REPORT.md` / `results_replay.csv`; repeat runs of the same code on the
same machine moved p50 by a few percent and the small populations' p95 by up to 20%.
Startup floors in the same run: `moss check` of a trivial file 1.90 ms, `margo --check build`
of a trivial package 47.8 ms.

| Population | Commits | post p50 ms | post p95 ms | post max ms | pre p50 ms | pre p95 ms |
|---|---:|---:|---:|---:|---:|---:|
| all commits (incl. failing checks) | 50 | 308.1 | 2209.6 | 2934.7 | 424.0 | 1952.9 |
| post-edit check passes | 15 | 697.3 | 2436.9 | 2934.7 | 355.9 | 1910.1 |
| post passes, package existed (pre may fail) | 9 | 382.3 | 2637.8 | 2934.7 | 258.9 | 1765.1 |
| clean edits: pre and post pass, package existed | 5 | 231.2 | 893.4 | 942.4 | 452.9 | 1907.7 |

Distribution of the post-edit latency over all 50 commits (commits per bucket): <50 ms 0;
50-100 ms 7; 100-250 ms 15; 250-500 ms 7; 0.5-1 s 9; 1-2 s 8; 2-5 s 4; >=5 s 0. Median compiler
counters of the post-edit check, all 50 commits: 134 functions analyzed, 98 source function
checks, 314 specialization evaluations, 0 specialization keys, 2 checker runs (sum of functions
analyzed over the 50 commits: 7902); for the 5 clean edits: 118 / 54 / 236 / 0 / 2.
One commit (#49) has a passing post check in which margo reported every package skipped and no
checker ran (its only tracked change is a file under `fixtures/`); the post check then cost
margo's own bookkeeping, 0.13 s.

How to read it. Only 9 of the 50 post-edit checks are checks of an existing package that
pass and only 5 of them also passed before the edit, so the "clean edit" row is the one closest to an
agent's steady-state loop and it is small (n=5; p95 of 5 values is within 10% of the max). The
all-commit row mixes in failures (fast: they stop at the first diagnostic), first checks of new
packages (slow: cold, whole package) and multi-entry-point commits (up to 8 entry points). It is
reported because it is the whole frozen range, not because it is a clean latency.

**What an IMPROVEMENT looks like.** B6 is the benchmark where a resident daemon or fact store
must show up as a collapse of the right-hand tail toward the left: p95 of the per-edit
latency approaching the p50 of an incremental check, and that p50 approaching the startup floor
(about 2 ms for a file request and 48 ms for the margo path today). With a content-addressed
fact store, a post-edit check after an edit whose summaries do not change should analyze
only the edited functions: the `post` "functions analyzed" counter should fall from today's
whole-package count (median 118 for the clean edits, 134 over all commits) to roughly the number
of edited functions, with `checker_runs` per package at most one.
The gate must stay green throughout (identical verdicts and diagnostics per step, `pre` and
`post`, between the reference compiler and the candidate or warm client), and a warm client is
measured by the same specs: `--warm-client-cmd` adds a `warm` series (one stable work path,
`--warm-primes` full unmeasured sequences first). The warm series replays the history in the
same order every rep, so a content-addressed client may answer later reps from its store; read
`wall_ms_first` next to the median.

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
(whole-function hashes that include transitive effects: Team 3 found they move for every caller in a
cone, so they are not the summary), and the synchronization plan (its graph identity changes with any
program change). Output is deterministic: sorted entity ids, normalized fields, `sort_keys` JSON.

**Edited vs not edited.** A function is *edited* when its source text slice differs between the two trees:
from its declaration line (given by the `symbols` query) to the line before the next non-blank line indented
no deeper than the declaration, trailing blanks trimmed, line ends stripped. That attributes the diff to
functions; it is a location fact, and a comment-only edit inside a body counts as an edit. The classifier then
reports separately: functions added or removed (membership), *the edited function's own* summary changed, and
*some other, not edited function's* summary changed (ripple: a caller's inferred access, effects or raises moved
because a callee's did).

**Availability.** A commit is analysable only when every touched unit answered the queries on both sides: the
tree must check far enough for the compiler to answer. 21 of 50 do; 29 do not (the same drift as above; each
reason is in `churn.json` and the report). Of the 21, 7 add a package (all functions are added; no summary can
change), leaving **14 edits of existing packages: the denominator**. That is a small, non-random sample (the trees
that happen to check today); read the intervals, not the point values.

| Question (14 edit commits) | Commits | Share | 95% Wilson |
|---|---:|---:|---|
| any function summary changed | 4 | 29% | 12-55% |
| an edited function's own summary changed | 4 | 29% | 12-55% |
| some other (not edited) function's summary changed (ripple) | 2 | 14% | 4-40% |
| no summary changed at all | 10 | 71% | |
| ... of which only functions were added/removed | 6 | 43% | |

By category (commits with at least one changed function in it; any / edited / other): types 1 / 1 / 0, access
1 / 1 / 0, raises 4 / 4 / 2, effects 1 / 1 / 0. Of the 14, only 4 changed the text of an existing function; for
those 4: any summary changed 4/4, own 4/4, ripple 2/4 (95% Wilson 15-85%); 57 functions edited, 37 of them changed
summary, and 4 functions that were not edited changed summary (both ripples were `raises`: commit #41's callers
`cbor.array_push`/`cbor.map_set`, and #10's `linalg.get`/`linalg.set`). Functions whose summary changed, per edit
commit: p50 0, p95 14.5, max 36, mean 2.9 (one commit, #10, a numerics review pass, accounts for 36 of the 41).

**What an IMPROVEMENT means here.** Churn is a property of the history, not of the compiler, and does not
"improve". It tells which fix can pay off. In this sample most edits (10 of 14) move no summary at all and all
of those would be answered by a fact store that reuses an unchanged summary and rechecks only the edited functions;
where an edited function's summary did change, the change reached a caller in 2 of 4 cases, so a summary-aware store
must still follow changed summaries to their callers (it cannot stop at the edited function) but would touch a handful,
not the whole package. Low churn means a summary-aware store pays off; high churn (here, concentrated in one big edit)
means it cannot. n=14 (4 with an edited existing function) cannot distinguish "rare" from "common".

### Determinism, gate and self-tests

- `generate.py --verify-determinism` regenerates the whole suite, including the 50 steps from git plus the recorded
  expectations, and compares it with the manifest; `generate.py --validate-only` re-observes every step with the
  reference compiler (blobs, applied tree equals the commit's tracked tree, every frozen verdict).
- `run.py replay-check --with-churn` ran every step twice on moss-opt: the classification (status, exit code,
  diagnostic of every check) and the per-phase counters were identical on the repeat for all 50 steps, and the
  churn records of all 50 commits (availability, function sets, per-function changes) were identical across two
  complete churn passes; each step also met its frozen expectation.
- The gate treats every entry point of every step, `pre` and `post`, as a check step compared on the whole JSON
  verdict (volatile keys and margo's `checked`/`skipped` bookkeeping excluded, shown as information) and requires
  reference == frozen expectation. `selftest` proves it fails a mutating or diagnostic-changing client and
  passes an identity client and, for the plumbing only, a stand-in memoizing client.
- `selftest` also checks the churn classifier on synthetic input, end to end through the real queries (a
  comment-only edit is an edit with no summary change; a deliberately injected body change that makes a function
  write its parameter is detected as an own change in access and effects and as a ripple on its caller, while an
  unrelated function is untouched), spec forgery (a re-hashed forged expectation is refused by the frozen
  range; a wrong blob id; a drifted pre-image), cold rows and counters, and that no worktree is left registered.

### Limits of the method

- **Old commits, today's compiler.** The compiler and SDK evolved together; checking an old tree with today's
  compiler measures neither the old nor the new world. 39 of the 50 commits have at least one failing check
  today; 46 of the 92 failing checks are one diagnostic (`TYPE_MISMATCH` "push argument conflicts with the element
  type of 'parts'" in `sdk/text` and everything depending on it), and the post-edit checks of the last four
  commits (#47-#50) all pass. That pattern is consistent with the Char/String SDK repair in commit #47
  (`dd45e307`, "Fix SDK Char and String boundaries"), but the cause of each failure is not diagnosed here. Failing
  checks stop at the first diagnostic and are fast; they are in the "all" row and excluded from the others.
- **Entry points.** Package checks of the packages that own a changed file; dependents are not checked (above), and
  `moss test --check` runs only for the root package of an entry point. Only `.moss` and `Moss.toml` changes are
  replayed. Commits that change only tests, benches or fixtures mostly produce a `post` equal in cost to `pre`
  minus the dependency builds, or (#49) no checker run at all.
- **Cold, per step.** Every step starts from empty build dirs and ends one edit later; the real loop (many edits in
  a row in one tree) is what the warm series is for. The steps are independent, not cumulative.
- **Sample.** 50 commits by many authors in a few weeks, mostly large feature commits (new packages, bulk
  additions), not the one-function edits of an agent loop; B3 is the controlled version of that. The churn sample is
  small (14 edits, 4 with an edited existing function).
- **Textual "edited".** A function whose text did not change but whose callee in another file did is not edited; a
  reformatted function is. Entity ids are per module name; two functions with the same id in one commit would be
  disambiguated by order.
- **Mechanisms not established.** Why individual post checks cost what they do (for example #50: 2.9 s and 628 functions
  analyzed over 5 checker runs for a cbor package whose pre check failed after 1.1 s, so the two are not
  comparable) was not investigated; the counters show what was analyzed, not where the time went.


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
