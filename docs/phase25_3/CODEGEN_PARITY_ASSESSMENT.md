# Phase 25.3 bounded generated-Rust parity assessment

## Scope and baselines

This is a bounded ABI-v9 comparison, not a parity acceptance claim. The 20-file
smoke pass was expanded to 52 standalone Moss files because the harness ran
reliably and the first pass exposed Java-only rejections and consistent Rust
output differences.

| Role | Exact source commit | Checkout state | Compiler evidence |
|---|---|---|---|
| Accepted C++ baseline | `683beccf54a68f4eaa09f0db13ff8443f3396c92` (`origin/main`) | Clean detached worktree | Reused `moss`, SHA-256 `9f64b32052df93bc4fd9d72dcd792440702f61e55eec0063b1e2814ae36df4f4`; its C++ sources match this commit. Bootstrap reports `moss-0.1`. |
| Java baseline | `7afd0cbb22e5b3903449ba902185f3d7d1464cd4` (`phase25-3-java-j2`) | Clean detached worktree | Clean `javac` build completed in 1,655 ms with OpenJDK 25.0.4.1. |
| Differential harness | `6ca7190927d6592aaee3355e2ddfcf8bd5266c5c` (`tooling/codegen-differential`) | Used without merging | `tests/differential/moss_codegen_diff.py` |

The shared checkout on `phase25-3-java-j2` already had seven modified Java source
files. Those Java files were not edited or built by this assessment; the Java
comparison uses the committed `7afd0cbb` snapshot in its own clean detached
worktree. Thus these results do not characterize those uncommitted changes.

The 52 inputs were 33 top-level examples and 19 standalone test fixtures
covering functions, pipelines, domains/messages, collections, traits, strings,
enums, integer operations, and expected failures. Project dependency closure
was not tested. The six differential-corpus fixtures were copied under the
ignored `tmp/` directory of the clean C++ worktree so both compilers saw files
inside the same source root.

## Results

| Classification | Files |
|---|---:|
| Byte-identical generated Rust | 0 |
| Both compiled, generated Rust differed | 39 |
| C++ accepted, Java rejected | 8 |
| Java accepted, C++ rejected | 0 |
| Both rejected with the same diagnostic code | 5 |

The five matching rejections are `examples/use_after_transfer.moss` and the four
negative differential fixtures. Their shared rejection is expected; they do
not count as generated-code parity.

The 39 successful pairs group as follows; the first two findings overlap and
describe the shared mismatch shape, rather than disjoint subsets:

| Root-cause group | Files | Evidence |
|---|---:|---|
| Runtime/prelude and compiler metadata emission differs | 39 | C++ includes a broad runtime and backend-plan comments; Java emits a much smaller helper set and additional lint attributes. |
| Generated main-body code differs beyond comments | 39 | After stripping comment-only lines from each generated main section, all 39 pairs still differ. |
| Formatting/comment/attribute-only changes | 0 pairs | These differences occur, but none of the pairs becomes identical by treating them as the only cause. |

In `examples/hi.moss`, the C++ output is 3,382 lines and the Java output is 534
lines: the C++ compiler includes a large runtime block (including lock hooks
and the pinned Ryū implementation), while the Java compiler emits a much
smaller helper set. The source-level echo also differs: C++ emits
`println!("{}", MossStr::from("hi"));`; Java emits
`println!("{}", (&(MossStr::from("hi"))).__moss_display());` and duplicates the
source-line comment. The comment and lint-attribute differences are
non-semantic; runtime and lowering equivalence was not established because Rust
compilation and execution were outside this pass.

`examples/domains_and_messages.moss` shows a more substantial lowering
difference. C++ emits message calls through `runtime_invoke` and shared handler
methods such as `Run_shared`; Java emits direct root calls such as
`worker.Run_root(10_i64)`. This needs focused semantic review before either
lowering is treated as equivalent.

## Java-only rejection clusters

All eight Java-only failures are accepted by C++:

| Likely root cause | Count | Representative cases and Java diagnostic |
|---|---:|---|
| Open-result inference through functional pipelines | 4 | `examples/dogfood_pipeline.moss`, `functional_basics.moss`, `functional_objects.moss`, and `functional_static_callables.moss`: `TYPE_AMBIGUOUS_OPEN_RESULT` |
| Enum payload/match and return typing | 2 | `tests/phase15_15_enum_message.moss`: `UNKNOWN_SYMBOL_OR_TYPE` for `code`; `phase15_15_enum_positive.moss`: `MOSS_COMPILE_ERROR` for `inspect` returning `Outcome` while annotated `Int` |
| String API coverage | 2 | `tests/phase15_15_string_map.moss`: `TYPE_MISMATCH`, missing `chars`; `phase24_string_concat.moss`: `TYPE_MISMATCH`, missing `append` |

There were no C++-only failures. A matching error code on the five
both-rejected cases does not establish equivalent diagnostics or generated Rust.

## Wall times

The harness starts a separate Java process for every file. Every Java per-file
measurement therefore includes fresh JVM startup; these are not warm-JVM
compiler timings. Values below use all 52 attempts, including rejections:

| Compiler | Median | p95 |
|---|---:|---:|
| C++ | 21.67 ms | 116.59 ms |
| Java (fresh JVM per invocation) | 255.94 ms | 276.54 ms |

For the 39 pairs where both compiled, C++ median/p95 were 22.44/177.74 ms and
Java median/p95 were 258.97/280.28 ms. The clean Java `javac` build time is
listed separately above and is not included in per-file timings. These figures
describe this host and this corpus only.

## Recommended bounded follow-up

1. Make a minimal source probe for open-result constraints flowing from
   `sum`, `map`, and generic callable specialization; align Java acceptance with
   C++ before touching code generation.
2. Check Java's enum payload binding and return-type inference on the two
   accepted fixtures, reducing each to a small standalone case.
3. Add Java support for the existing `String.chars()` and mutable `String.append`
   cases if the active Java work has not already addressed them.
4. Review the domain message lowering with one small fixture, preserving the
   static route and synchronous handler contract. Do not infer behavior from
   generated Rust alone.
5. Treat the broad runtime/prelude, echo, comments, and lint differences as
   separate codegen reconciliation work. First distinguish required support
   semantics from harmless emission policy; do not normalize away output and
   claim parity.

The Java-only failures may overlap the seven uncommitted Java files on the
active `phase25-3-java-j2` checkout. Coordinate with that work before making
corrections. This assessment ran no `make check`, Rust compilation, native
execution, SDK integration, multi-crate matrix, or Phase 25.4 work. It reports
parity evidence for these 52 files only; full or semantic parity remains
unverified.

The differential harness outputs are retained for this session under
`tmp/phase25-3-cpp-baseline/tmp/phase25_3_codegen_diff_final/`; the summary above
is the durable record and does not depend on those disposable artifacts.
