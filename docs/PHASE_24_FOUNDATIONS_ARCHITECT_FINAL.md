# Phase 24 Foundations SDK: independent architectural acceptance review

**Reviewer:** Architect L6, independent of implementation and validation ownership.  
**Initial implementation examined:** `05993abd4a3af2b275b36fc24497d1be1788125d` in the isolated `tmp/phase24-foundations/architect-review` worktree.  
**Second integrated candidate examined:** `20ccbcf0fab787a301deb1a85e48867f0c48f12f` (NO-GO for vector-literal indexing).  
**Final integrated implementation examined:** `bcebfaa27e57c8bfbc43ff124d9c0e5a953a9926` on `phase-24-foundations-integration`.  
**Attributable Architect verdict:** **GO for the approved Phase 24 Foundations scope at `bcebfaa`. L7 merge gate: HOLD.**

## Release-blocking counterexample at `05993ab`

**P1: Imported generic specialization is omitted beneath field projection.** A provider exports `Box` and generic `make(value) -> Box`; a consumer uses `probe.make(7).value`. `moss check --json` accepts it, but `margo build --json` fails with rustc `E0425`, missing `__moss_specialize_probe__make_0`. The compiler's `collect_local_call_sites` traverses vectors, unary and binary expressions, calls and indexing, but at this SHA does not descend through a terminal field projection. The checker and native lowering do traverse it, leaving the projected consumer crate without the required imported specialization. This is a compiler integration defect, not an SDK API defect, but it blocks the required path-dependency generic contract.

Minimal sources:

```moss
# provider/src/probe.moss
module probe
export type Box:
  value: Int
export fn make(value) -> Box:
  return Box(value: value)
```

```moss
# consumer/src/main.moss
module app
import probe
fn main():
  echo probe.make(7).value
```

The consumer has a path dependency on the provider. From the consumer directory, with the provider built:

```sh
MOSS_MODULE_PATH=../field_provider/build/debug ../../../moss check src/main.moss --json
../../../margo build --json
```

The first command reports `ok:true`; the second reports `BUILD_BACKEND_ERROR` and `E0425`. The full scratch fixture is `tmp/architect-p24/field_provider` and `tmp/architect-p24/field_consumer` in the review worktree. This case has been sent to L7. At `20ccbcf`, I independently rebuilt the provider and confirmed the same consumer checks and prints `7` in native debug, native release and Fast Debug. The compiled-only ABI-v9 provider also checks, builds and prints `7`; the original P1 is **fixed** at this exact candidate.

**P1: Direct indexing of a vector literal loses imported calls at `20ccbcf`.** A separate `moss.collections` path-dependency consumer with `echo [collections.index_of([1, 2], 2)][0]` passes `moss check --json`, but `margo run --json` fails `BUILD_BACKEND_ERROR` / rustc `E0425` for `collections__index_of`. `margo debug` fails with `unsupported expression 'collections__index_of([1, 2], 2)][0'`. The source-backed fixture is `tmp/architect-p24/index_wrapper` in the exact-candidate worktree. The same wrapper with `probe.make(3).value` fails for both source-backed and compiled-only ABI-v9 providers; splitting the vector literal into a local followed by `v[0]` succeeds. A smaller standalone `echo [1, 2][0]` checks and runs native, but Fast Debug reports `unsupported expression '2][0'`. This is a false acceptance and native/Fast Debug divergence in a value form that composes the SDK's public generic API. L7 was notified with the exact repro paths.

## SDK contract assessment at `05993ab`

| Area | Independent assessment |
| --- | --- |
| Package/API scope | Four dependency-light packages stay within the approved `core`, `bytes`, `collections`, and `algorithms` scope. No new public language semantics are required by the source APIs. |
| Bounds | `valid_slice` guards nonnegative inputs and `start <= total` before `total - start`; `slice_bytes` clips after checking start and length; binary search uses `low + (high - low) / 2`; heap child multiplication is guarded by `root < end / 2`. No arithmetic/bounds counterexample found. |
| Byte representation | `Vector[Int]` does not seal the byte invariant. The README says so, and copying APIs assert selected/all input bytes. `set_if_valid` and `append_if_valid` assume existing content is valid, also documented. |
| Ownership/effects | `moss effects` reports `sort_int(items)` as WRITE, search and `from_range(data)` as READ, byte mutators as WRITE. A cross-package handler sorting a domain vector runs in native and Fast Debug; its semantic synchronization plan assigns an exclusive class to `values`. An immutable-vector sort is rejected with `IMMUTABLE_LOCAL_MUTATION`. |
| Generic/source-free | Compiled-only algorithms and collections consumers check, build and run. A separate compiled-only bytes provider accepts a borrowed FileIO Range through `from_range`, copies it before close and returns five bytes. The remaining field-projection case shows imported generic specialization is not yet complete at this SHA. |
| Native/Fast Debug | All four package test suites and path-dependency basic examples pass in both modes where supported. FileIO/Range is native-only under the documented current Fast Debug boundary. |
| Performance | Source algorithms support their claimed O(n log n) sort and O(log n) searches structurally. Local release Margo benchmarks run, but end-to-end microbenchmarks alone do not establish comparative Rust parity; final validation report must state workload and toolchain before making a parity claim. |

## Independent validation performed at `05993ab`

- Repository startup: read both local skills, ran `make moss -j2`, `./moss agent bootstrap --json` and confirmed `language_version=moss-0.1`.
- Four package test suites: `../../../margo test --json` and `../../../margo test --interp` in each package: core 4, bytes 3, collections 4, algorithms 4; all passed in both modes.
- `python3 sdk/foundations/{algorithms,collections}/tests/check_source_free.py` passed; `python3 tests/tooling/check_phase24_imported_generic_expressions.py ./moss ./margo` passed its existing wrappers.
- `../../../../../margo run --json` in algorithms/basic, collections/basic and bytes/range_copy passed with expected output. A separate bytes compiled-only provider copied `.mossi` and `.rlib` without source; `moss check`, `moss build`, and executable output `5` passed.
- `python3 sdk/foundations/bytes/tests/check_negative.py` observed invalid-byte assertions in native and Fast Debug. `margo build --json` on algorithms/immutable_rejected returned expected `IMMUTABLE_LOCAL_MUTATION`.
- Deterministic 35-case cross-package randomized oracle at `tmp/architect-p24/random_oracle` compared sort, lower bound, first-match binary search, collection index/count, and core/byte boundaries against Python-generated expected values. `../../../margo run --json` and `../../../margo debug` both printed `oracle-ok`.
- Cross-package domain-state sort at `tmp/architect-p24/domain_sort`: `margo run` and `margo debug` both printed `1`; `moss effects entity-v1:handler:app__Sorter.Run --source src/main.moss --json` (with provider interface on `MOSS_MODULE_PATH`) reported `write_set=[values]` and an exclusive acquisition.
- `../../../margo bench --release --json` ran in algorithms and collections. Local medians were sort 256 descending Ints 1,580 ns; lower bound in 256 Ints 168 ns; index of last in 256 Ints 274 ns; reverse copy of 256 Ints 367 ns, using Rust 1.98.1. These are local measurements, not a Rust comparison.

## Exact-candidate retest at `20ccbcf`

I created a separate detached worktree at `20ccbcf0fab787a301deb1a85e48867f0c48f12f`, rebuilt `./moss`, reran bootstrap (`moss-0.1`) and copied only the source/manifests of my original probes; no previous build cache or provider artifact was reused. The original `probe.make(7).value` now passes `moss check`, `margo run`, `margo debug`, and `margo build --release` followed by execution, each printing `7`. I repeated it against a compiled-only provider containing only `probe.mossi` and `libprobe.rlib`, also printing `7`.

`python3 sdk/foundations/acceptance/run_all.py --full` passed. It exercised 44 generated sort/search cases and eight byte cases in native debug, native release and Fast Debug, plus source-removed ABI-v9 collections and FileIO Range providers. My separate 35-case random oracle again printed `oracle-ok` in native and Fast Debug. My domain-state sort printed `1` in both. A compiled-only collections consumer specialized Bool and Float element types and printed `1` and `2`.

I reran the acceptance package benchmarks with release providers and compiled `sdk/foundations/acceptance/bench_baseline.rs` using `rustc --edition=2021 -D warnings -O -C debuginfo=1`. The single shared-machine run gave these medians (ns):

| Workload | Moss SDK | Rust same algorithm | Rust standard library |
| --- | ---: | ---: | ---: |
| Sort 256 descending Ints | 1,854 | 1,667 | 196 |
| Sort 256 permuted Ints | 1,851 | 1,915 | 1,454 |
| Validate and copy 512 owned bytes | 754 | 728 | 548 (validation + clone) / 376 (raw clone) |

These data support a rough same-algorithm comparison only. The Rust harness passes the constructed input through `black_box`; the Moss benchmark does not make that identical intervention. The p25/p75 bands were broad, so a single run cannot support a strict speed ratio or equivalence claim. The SDK README correctly describes end-to-end costs rather than isolated call latency.

## Exact-candidate retest at `bcebfaa`

At exact implementation candidate `bcebfaa27e57c8bfbc43ff124d9c0e5a953a9926`, I retested the direct indexed vector literal counterexample (`[collections.index_of([1, 2], 2)][0]`, `[1, 2][0]`, and `collections.reversed_copy_int([1, 2])[0]`) in `tmp/architect-p24/index_wrapper`:
- `margo run --json` passed and output `1\n1\n2\n` natively.
- `margo debug` passed and output `1\n1\n2\n` in Fast Debug.
- Source-backed and compiled-only ABI-v9 provider generic expression specializations in `tests/tooling/check_phase24_imported_generic_expressions.py` passed across native and Fast Debug execution.
- `python3 sdk/foundations/acceptance/run_all.py --full` passed all 44 sort/search cases and 8 byte cases across native O0, native release, Fast Debug, and source-removed ABI-v9 providers.
- Bounded smoke acceptance `python3 sdk/foundations/acceptance/run_all.py` passed in `tests/run.sh`.
- The 35-case random oracle printed `oracle-ok` in native and Fast Debug; cross-package domain sort printed `1` in both engines.
- Full repository `TMPDIR="$PWD/tmp" make check` exited 0 with `all Moss v0.1 tests passed`.
- All 32 examples in `make examples` built successfully.
- Strict C++17 compilation (`g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -fsyntax-only src/moss.cpp`) and `git diff --check` passed cleanly.

Both release-blocking counterexamples (field projection and indexed vector literals) are verified resolved on this exact implementation source.

**Attributable Architect L6 vote: GO for Phase 24 Foundations SDK at `bcebfaa`. L7: HOLD merge into `main` pending project authorization.**
