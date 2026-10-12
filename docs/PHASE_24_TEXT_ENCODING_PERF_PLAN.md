# Phase 24 Team T performance sprint plan

Base: `phase24-text` at
`4a9b9be12fd230dcbd0b176d539937c29248d2f4`.
Delivery branch: `phase24-text-perf`.

## Task DAG and ownership

1. P1 profiles and optimizes substring search and code-point slicing. It owns
   `moss.text` search/slice design and requests targeted String built-ins from
   P4 rather than editing the compiler.
2. P3 profiles and optimizes UTF-8 validation/encode/decode independently of
   String work. It requests any lowering change from P4.
3. P4 is the single compiler/runtime owner. It compares generated and
   hand-written Rust, then implements only the targeted String/lowering
   primitives required by measured Team T operations, including native and
   Fast Debug behavior plus source-surface documentation.
4. After a slot opens, P2 profiles ASCII case conversion and builders against
   P4's proposed primitive surface. P2 owns SDK changes but not compiler code.
5. V independently reconstructs fair idiomatic and equivalent Rust baselines,
   verifies optimized linked providers, checks workloads and allocation
   evidence, and runs semantic/correctness regressions after integration.
6. The Coordinator integrates each exact workstream commit into
   `phase24-text-perf`, resolves only cross-workstream API dependencies, and
   runs the complete gates.
7. The independent L6 Architect reviews a frozen exact SHA adversarially. It
   does not inherit implementation ownership and must reproduce profiles,
   benchmarks, Unicode behavior, Fast Debug, and source-free ABI-v9 evidence.

P1, P3, and P4 can execute concurrently. P2 depends on P4's built-in proposal.
V depends on an integrated benchmark candidate but authors fresh cases. The
Architect is the final gate. Team I alone integrates the delivered branch into
`phase24-integration`; no Team T agent merges into `main`.

## Measurement contract

Every comparison keeps identical contents, code-point lengths, byte lengths,
search locations, slice ranges, operation counts, assertions, and anti-elision
barriers. Complete-operation and preconstructed-input cases remain separate.
Moss benchmarks use `margo bench --release --json`; the verifier binds every
benchmark-linked provider rlib to its release artifact. Results report median,
p25, and p75. Idiomatic Rust establishes the target; equivalent Rust isolates
algorithm/representation overhead.

The environment currently provides neither `perf`, `valgrind`, nor
`heaptrack`. Where system allocation sampling is unavailable, the sprint uses
generated-Rust inspection, an instrumented Rust global allocator in maintained
reference harnesses, operation decomposition, and repeated-input benchmarks.
No allocation count is inferred from elapsed time.
