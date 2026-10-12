# Phase 24 Foundations SDK — independent initial architecture review

**Reviewer:** independent L6 Architect, Team F  
**Baseline:** `main` `4c07e31ea98a2ec9facb14c38cf3af8e56efe81d`  
**Scope:** critique of `docs/PHASE_24_FOUNDATIONS_PLAN.md` before API freeze. This is not an acceptance vote on future implementation.

## Recommendation

Proceed with four Margo packages named `moss.core`, `moss.bytes`, `moss.collections`, and `moss.algorithms`, each exporting a simple lowercase source module. The package name and source module name are separate: a path dependency named `moss.bytes` and `import bytes` built and ran in a current-main probe. Keep dependency edges only where code calls another package. A four-package consumer proves composition without artificial edges.

Freeze a small API whose ownership and error behavior can be tested. The suggested first slice is:

| Package | Initial public contract | Conditions |
| --- | --- | --- |
| `moss.core` | `min_int`, `max_int`, `clamp_int`, `valid_index`, and an overflow-safe range-validity predicate | Document `clamp_int` when lower bound exceeds upper bound; prefer a checked precondition to a surprising result. |
| `moss.bytes` | `Vector[Int]` byte validation, indexed byte read/write helpers, owned copy and slice, and explicit copy from live FileIO `Range` where possible | Bytes are integers in `0..255`. An ordinary vector or exported record cannot enforce this invariant at all times; every consuming operation must validate input or state its precondition. Invalid indices follow the documented strict-index behavior. |
| `moss.collections` | Useful searches and counts, plus Int-vector sequence mutation/copy that adds behavior beyond built-in pipelines | Make ownership and allocation explicit. Generic equality/callback variants require consumer specialization and native parity before publishing. |
| `moss.algorithms` | Int-vector sort, `lower_bound`, and `binary_search` | State ordering, duplicate policy, mutation/copy behavior, and measured complexity. A first-match binary search should derive from `lower_bound`. |

Do not advertise a general `Span[u8]`, a generic `Bytes<T>`, arbitrary element cloning, callback closure objects, or a total Float order. The live `moss-0.1` bootstrap exposes no general Span or user-defined generic type declaration. FileIO `Range` is a scoped read-only borrow, so an owned SDK buffer must copy its contents during that lifetime. Concrete cross-crate `Range` is already a documented Phase 20 fail-closed limitation; a cross-package Range adapter must be checked before publication. String indexing uses Unicode code points and must not serve as byte indexing.

The language already supplies eager `map`, `filter`, `reduce`, `sum`, `count`, `any`, and `all`. SDK functions should add semantics such as index/duplicate policy, bounds validation, or a specified allocation contract. Otherwise examples should teach the built-ins rather than add wrappers.

## Adversarial probes on current main

I loaded both repository Moss skills, ran `./moss agent bootstrap --json` and verified `result.language_version = moss-0.1`. All scratch files and build artifacts were inside this worktree's `tmp/architect-probes/`. The compiler was built with `make` because `./moss` was initially absent.

1. **Generic indexing improved since an old swarm report.** The historical `repro_untyped_index.moss` now passes `./moss check --json`, generated Rust compiles under `rustc --edition=2021 -D warnings`, and the executable prints `10`. This proves one `Vector[Int]` specialization, not all generic indexed cases.
2. **Non-Copy vector copy remains a native defect.** The historical `repro_push_loop_reference.moss` passes Moss checking, but native Rust fails E0308 at `result.push(x)`: `x` is `&String`, whereas `push` requires `String`. An SDK helper that copies `Vector[String]` with `for x in items: result.push(x)` cannot be accepted as implemented. Current native repro command: `./moss tmp/architect-probes/reproducers/repro_push_loop_reference.moss -o tmp/architect-probes/repro_push_loop_reference.rs && rustc --edition=2021 -D warnings tmp/architect-probes/repro_push_loop_reference.rs -o tmp/architect-probes/repro_push_loop_reference.bin`.
3. **A public record cannot enforce the byte invariant.** A provider with `export type Buffer: data: Vector[Int]`, validated `from_ints`, and validated `set` built as package `moss.bytes`. A separate consumer compiled and executed `var b = bytes.from_ints([65]); b.data[0] = 999; echo bytes.get(b, 0)`, printing `999`. Thus a public `Buffer` containing a mutable vector cannot claim all instances hold bytes. Making `Buffer` private did not yield an opaque public return type: the consumer build rejected the exported constructor interface with `UNKNOWN_SYMBOL_OR_TYPE` for `bytes__Buffer` in `bytes.mossi`.
4. **Cross-package typed effects work in the basic case.** With `export enum ByteError: InvalidValue`, a validating exported constructor and setter forced the consumer `main` to recover `bytes.ByteError.InvalidValue`; without recovery, Margo rejected escaping typed errors. A valid recovery program built and printed `2 65 67` natively. This verifies the direct case only, not all source-free/callback combinations.
5. **Fast Debug constrains the implementation shape.** The valid provider used `for value in values` for validation. Native execution passed, but `margo debug` reported `Fast Debug does not support for iteration yet`. Use an indexed `while` implementation for APIs whose Fast Debug parity is an acceptance requirement, or document a known interpreter limitation without claiming parity.
6. **A historical cross-package callable example builds now.** Copying `examples/swarm/polymorphism/collection_pipeline/{data_tools,log_analytics}` into `tmp/architect-probes/` and running `margo build --json` in the consumer succeeded. Named callbacks across a package boundary are possible; this does not verify source-free variants or non-Copy callback results.
7. **Filtering nontrivial elements is restricted.** The historical `repro_filter_nontrivial_type.moss` still fails `moss check --json` with an explicit deep-copy diagnostic. This matches the ownership model and forbids promising a generic `filter_copy` for arbitrary structs.

## Contract and test implications

- Prefer `Vector[Int]` as the public owned byte representation for this release. If a nominal `Bytes` convenience record is provided, document that its exposed vector can be modified by consumers, so all entry points validate or explicitly require valid byte values. An opaque invariant-bearing byte type requires a language/module capability not established by these probes.
- Use strict index APIs with either the existing panic behavior or an explicit success/result protocol. Do not silently turn invalid access into zero. For `slice(start, length)`, validate with subtraction after proving `start <= total` to avoid `start + length` overflow.
- Ensure source-free tests cover any exported untyped helper and its concrete consumer specialization. Test both native and Fast Debug with supported constructs. Do not infer generic support from a single `Vector[Int]` case.
- Benchmark the actual output construction and sort implementation. State allocation/copy counts or measured throughput; no performance claim follows from the API shape alone.
- Keep `moss.algorithms` independent of `moss.collections` if it does not call it. Avoid making users acquire all four packages for a binary search.

**Initial architecture verdict:** GO to a constrained implementation with the byte-invariant and non-Copy restrictions above. Final independent acceptance requires review of the integrated source and adversarial cross-package tests.
