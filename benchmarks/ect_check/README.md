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
  selftest.py      RSS-independence and gate-sensitivity self-tests
  ect_common.py    hashing, command templates, measured execution, verdicts
  dag_generators.py  Team 2 generators: b1_leak, b2_equiv, torture_ext, dag_diamond
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
  manifest's `expected_status`. A broken template (unknown placeholder, missing
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
CSV row. `validate` and `gate` accept either flavor.

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

63 fixtures in four generators (`dag_generators.py`), all deterministic,
all in the manifest, all run through `run.py bench`, the gate and
`generate.py --validate-only`. Manifest rows may carry two extra fields:
`timeout_s` (per-fixture timeout applied to every run of it, including gate and
validate) and `provider` (a package directory inside the fixture that is built
first, see B1).

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
root's argument type flows through every one; no shared callees), called with K
user types `T000..` that each define `score()`. Two things were found while
building this and shaped the fixtures (both are pre-existing compiler behavior,
recorded in `agent-feedback/phase28-5/ect-bench/generic-dag/`):

- Binding a child result with `var c = child(x)` makes today's checker
  exponential in depth (a 6-node chain: 0.3 s at D=6, 5 s at D=8). The
  fixtures use `child(x) + 1` instead.
- A **shared-callee** DAG (node (l,i) calls (l+1,i) and (l+1,(i+1) % W)) is also
  super-linear in depth even though the specialization memo hits and distinct
  keys stay linear (D=12, W=4, K=1: tens of seconds). Such a DAG at the B1
  grid sizes (D=20/50) does not finish in minutes, so the B1/B2 grids use the
  spine shape above, and the shared-callee shape is kept as its own small
  `dag_diamond` series (D=6..12) so that behavior is tracked too.

### B1 - Mossi Leak (`b1_leak`, 18 fixtures: D {5,20,50} x W {1,4} x K {1,8,32})

Package `lib` (`dag_lib`) exports the root of the untyped generic DAG (all
other nodes are internal); package `app` (`dag_app`) depends on it via
`path = "../lib"`, defines K user types and calls `dag_lib.root(Tk(..))` for
each. Two series per fixture, never mixed:

- **Consumer** (the result): `margo --check build` of `app`, with `lib` already
  built (the runner builds it first, unmeasured for the consumer sample), so
  the consumer's checker run is the only one measured. Counters come from a
  separate counter run.
- **Provider**: `margo --check build` of `lib` alone, its own wall/CPU/RSS
  medians and counters (`provider_*` columns).

The runner also reports boundary evidence: `app` defines **zero** DAG
functions, yet the consumer's checker analyzes D*W+1 functions and creates
(D*W+1)*K specializations, i.e. bodies in `lib` are walked through its
`.mossi`. `functions_analyzed` and `source_function_checks` in `lib`'s own run
match the same D*W+1.

*What an improvement looks like*: with a `.mossi` firewall (instantiation
answered from the exported summary, bodies not re-walked), consumer
`functions_analyzed` and spec evaluations stop depending on D (and W), and
consumer wall/CPU stop growing with D and become nearly independent of K (a
per-type cost of one summary lookup). The provider series absorbs the cost: it
must stay about equal to today's provider build (possibly slightly higher, since
it now produces the firewall facts). The honest reading is the pair: consumer
down, provider not up by more than it saved, summed over the workflow.

### B2 - equivalence inside one package (`b2_equiv`, 32 fixtures, K = 1..32)

The same DAG with D=20, W=4 (81 callables) in one file, no package boundary,
one fixture per K (one more type per step). `run.py` computes the **marginal
cost per new type** as the least-squares slope (with intercept and R2) of wall,
CPU, functions analyzed, spec keys and spec evaluations against K (also
stored as `dag_analysis` in `results.json`; the same fit over K = 1, 8, 32 is
reported per B1 cell).

*What an improvement looks like*: today every new type re-walks all 81 nodes
(slope ~ 81 spec keys per type and a proportional wall slope). With stored
facts keyed by content, a type whose facts are already stored should cost about
one cache hit: spec evaluations per type approaching 1 (not 81), and wall/CPU
slope per type collapsing toward the cost of one lookup, with the intercept
(first-type cost) unchanged or lower. Equivalence with B1: B1's
per-type slope should converge to B2's once the boundary stops adding cost.

### Torture extension 1.1.0-ext (`torture_ext`)

Extends but **never alters** the frozen `benchmarks/compiler_torture` (generator
1.0.0): its generator, manifest and fixtures are untouched and nothing is
imported from it; the W4 chain pattern is copied. The extension has its own
version string `1.1.0-ext` in this manifest.

- `tx_w4_d0100`, `tx_w4_d0200` (pass), `tx_w4_d0500`, `tx_w4_d1000` (expected
  `fail`): long dependent chain `step_i(x) = step_{i-1}(x) + 1` from W4,
  instantiated once with Int.
- `tx_cross_d*_k*` (W4 x W2): the same chain with leaf `x.score() + 1`, the root
  instantiated with K distinct user types (d100 with K=2/8/32, d200 K=4, and
  d500 K=4 expected `fail`).

**Infeasible sizes are kept, not dropped.** On today's compiler, depth >= ~275
crashes the checker with SIGSEGV (exit 139; stack exhaustion with the default
8 MiB stack; threshold between depth 250 and 275, for Int and user-type
arguments). `tx_w4_d0500`, `tx_w4_d1000` and `tx_cross_d0500_k04` therefore have
`expected_status: "fail"` and `metadata.expected_failure_reason`. The gate
compares exit code 139 reference vs candidate; the validate/status check
treats `fail` as expected. A fixed compiler flips them to `pass` and the
manifest must then be re-frozen deliberately (the unexpected-status flag in the
report is the signal). Per-fixture timeouts are generous (1800 s).

*What an improvement looks like*: these chains grow superlinearly with depth
today (Int chain: ~0.9 s at D=100, ~3 s at D=200). With stored per-callable
facts, re-checking an unchanged chain should be near-constant in depth, and
the crash fixtures should pass (a completed depth-1000 check is itself an
improvement). The cross fixtures should show wall/CPU per extra type falling
toward one cache hit per type, as for B2, instead of a full chain re-walk per
type. Functions analyzed for a chain should equal the nodes actually invalidated,
not all of them.

### Shared-callee DAG (`dag_diamond`, 4 fixtures, D 6..12)

Small, K = 1. Distinct keys stay equal to node count (memo hits grow), while
wall time grows super-linearly with D. *Improvement*: wall linear in nodes.

### Baseline recorded on today's compiler (moss-opt, 16-CPU WSL2, median of 3)

Local, disposable numbers (`tmp/ect_check/<run-id>/`); rerun for your machine.

- **Shape**: for the 60 of the 63 Team 2 fixtures that complete, `functions_analyzed` ==
  nodes and distinct specialization keys == nodes x K exactly (delta 0), e.g.
  B1 D=50 W=4 K=32: 201 fns, 6432 keys; B2 K=32: 81 fns, 2592 keys.
- **B1 crosses the boundary**: `app` defines zero DAG functions yet the
  consumer's single checker run analyzes all `D*W+1` functions of `lib` and
  creates all `(D*W+1)*K` specializations.
- **B1 consumer vs provider (W=4, K=32)**: consumer 830 ms (D=5), 5.5 s (D=20),
  25.7 s (D=50); provider (K-independent) 83 ms, 196 ms, ~0.6-0.8 s.
  Consumer cost per type: ~24 ms (D=5), ~167 ms (D=20), ~790 ms (D=50), so it
  grows with both node count and, per key, with depth (about 1.2 ms/key at D=5,
  about 4 ms/key at D=50).
- **B2 (D=20, W=4)**: 81 keys per additional type exactly; wall slope ~176 ms
  per type (R2 0.95: per-type cost rises with K, 225 ms at K=1, 5.3 s at K=32).
  B1's per-type slope for the same D, W is ~167 ms, so the package boundary adds
  a constant (~50 ms at K=1: margo startup and `.mossi` load) but no per-type cost.
- **Torture extension**: W4 depth 100 1.0 s, 200 3.9 s (superlinear in depth);
  depth 500 and 1000 and cross depth 500 crash with exit 139 (expected `fail`).

### Shape check (counters prove the fixture is what it says)

Every DAG fixture records `predicted_functions_analyzed` and
`predicted_spec_keys` (nodes x K) in its manifest metadata; the report prints
measured vs predicted per fixture with the delta (section 1b).
`functions_analyzed` does not count `main`.

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
