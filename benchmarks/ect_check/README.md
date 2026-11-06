# ECT Check Benchmark Suite

**Phase 28.5, Team 1 (harness and measurement plumbing) and Team 2 (generic-DAG
fixtures).** Suite version 0.2.0 (0.1.0 plus the Team 2 generators; the five
Team 1 `trivial_*` fixtures are byte-identical).

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
  (`ect_common.VOLATILE_KEYS`). A margo failure embeds the compiler's JSON in
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
type. (How many functions an *edit* re-analyzes cannot be measured here; that
needs the Team 3 edit sequences.)

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

## What improvement looks like (other stubs for later teams)

- **Inference Ripple (edit sequences)**: TBD. Expect functions re-inferred per
  body edit to drop from "callers up to roots" to the first consistent level.
- **Cold Tax (warm client)**: TBD. Expect warm wall time to approach checker
  work only, with startup/parse/.mossi loading amortized.
- **Worktrees**: TBD. Expect N worktrees to cost base plus divergence in
  CPU and RSS.
- **M1 / M2**: TBD. M1 should stay at 100% (hardening must not lose schemes);
  M2 is the reference for how much of the internal graph a fact store could
  skip.

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
