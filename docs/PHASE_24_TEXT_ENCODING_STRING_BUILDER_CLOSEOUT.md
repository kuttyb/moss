# Phase 24 Team T focused String and builder closeout

## Scope

This follow-up closes the two remaining Text performance exceptions without
reopening the wider sprint. It changes nonempty String concatenation lowering,
adds two ownership-checked String mutation primitives, and introduces an
additive capacity-aware `moss.text` builder. The existing public
`Vector[String]` builder and all of its segment-count and reusable-build
behavior remain unchanged.

## Concatenation lowering

Nonempty `String + String` no longer lowers through Rust formatting. The
compiler flattens a concatenation chain, evaluates every operand exactly once
in source order, computes the required UTF-8 byte capacity, and appends into
one result buffer. String literals are borrowed as static `str` values rather
than materialized as temporary owned Strings.

For the common accumulator form `value = value + part + ...`, the compiler
reserves and extends the existing local buffer only when the checked target is
a simple writable local and no later operand mentions that target. Aliasing
forms such as `value = value + value` retain independent-result construction.
This keeps Moss value semantics and avoids an implicit unsafe or observable
capacity rule.

The identical asserted 128-segment release workload measured 189 ns median in
the final Coordinator run versus 270 ns for the maintained optimized Rust
repeated-add baseline (0.70x). The implementation workstream run measured
155 ns versus 267 ns (0.58x). Both use optimized builds and retain complete
result assertions.

## Capacity-aware builder

The additive API is:

```text
new_capacity_builder(capacity_bytes) -> String
capacity_builder_append(builder, part)
capacity_builder_append_line(builder, line)
capacity_builder_build(builder) -> String
capacity_builder_finish(builder) -> String
```

Capacity is an initial UTF-8 byte hint; nonpositive values are safe no-ops.
Append infers `WRITE` for the builder and `READ` for the part. Reusable build
infers `READ` and returns an independent snapshot. Finish infers `CONSUME` and
returns the backing String without a final clone. Source-free ABI-v9 metadata
preserves those effects and rejects using the same String as both writable
builder and readable part.

The compiler-owned `String.reserve(additional_bytes)` and
`String.append(part)` operations have matching native and Fast Debug behavior.
The benchmark harness also now omits synthetic post-body borrows of checked
consumed locals, with a permanent consuming-benchmark regression.

## Performance and allocation evidence

The focused Margo cases use `margo bench --release --json`, five warmups, 31
samples, and 1,000 iterations. The validator SHA-256 binds all six linked
providers to release artifacts. Strict Rust uses rustc 1.98.1 with
`--edition=2021 -D warnings -O -C debuginfo=1`; contents, capacities, operation
counts, Unicode scalar results, and boundary checks are identical.

The workstream's final sequential p25 / median / p75 results were:

| Operation | Moss ns | Idiomatic reserved Rust ns | Ratio |
| --- | ---: | ---: | ---: |
| Finish 8 segments | 101 / 102 / 105 | 69 / 69 / 69 | 1.48x |
| Finish 128 segments | 381 / 392 / 409 | 276 / 278 / 283 | 1.41x |
| Finish 1,024 segments | 2,340 / 2,401 / 2,675 | 1,758 / 1,857 / 2,121 | 1.29x |
| Append preconstructed part 128 times | 252 / 273 / 386 | 195 / 200 / 205 | 1.36x |
| Reusable 1,024-byte snapshot x16 | 878 / 888 / 1,031 | 720 / 727 / 827 | 1.22x |

A three-run median-of-medians check kept every ratio at or below 1.49x. A
fresh integrated Coordinator run measured 128-segment finish at 419 ns in both
Moss and Rust, append-only at 271/255 ns, and reusable snapshots at 898/779 ns.

Rust allocation instrumentation measured one exactly reserved output buffer
plus one preconstructed owned part for finish, and sixteen output allocations
for sixteen reusable snapshots. For 128 segments this was 2 allocations /
1,032 requested bytes for finish and 16 allocations / 16,384 bytes for the
snapshots. Moss allocation counts are not claimed because the environment has
no supported Moss native allocation counter; generated Rust inspection is
structural evidence only.

## Validation

- Focused String concatenation and benchmark-ownership regression: passed.
- `moss.text`: 12/12 native and 12/12 Fast Debug tests, release build, and
  compiled-only ABI-v9 positive/negative ownership cases passed.
- Full Team T acceptance: Text, Encoding, cross-package native debug/release,
  Fast Debug, compiled-only providers, and 66-valid/15-invalid UTF-8 oracle in
  all three modes passed.
- Release benchmark validator: six optimized linked provider hashes passed.
- Strict C++17 warnings-as-errors, strict Rust references, developer-feedback
  validator, agent-skill validator, and `git diff --check` passed.
- `TMPDIR="$PWD/tmp" make check`: passed, including the new String regression,
  54/54 Executor tests, and `all Moss v0.1 tests passed`.
- `make examples`: all positive examples built; the intentional negative
  example was skipped.

## Engineering disposition

Engineering L6: **GO** for the focused assignment. Nonempty concatenation and
the additive capacity-aware builder meet the requested performance target in
the reproduced workloads without changing the existing builder contract.
Final acceptance remains subject to the separately attributable exact-SHA
Architect review.
