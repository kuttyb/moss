# Phase 15.15 indexing proposal and agent handoff

Status: proposed for implementation. Language decisions recorded from the 2026-09-26 discussion. Scope: SWARM-044 and a separate user-defined indexing workstream, EXPRESS-008. SWARM-043 (field access) is adjacent but is not silently closed by this work. SWARM-040 is deferred to Phase 21.

## Agreed semantic direction

An untyped parameter used as `container[key]` retains a static indexing requirement. Every concrete invocation is checked independently against the concrete argument types, and Moss specializes the function for that call. The first call does not freeze the parameter type or determine whether later calls are accepted. The selected container determines the admissible key type, result type, read/write effect, and existing failure behavior. There is no runtime dispatch, dynamic trait object, or implicit conversion.

For reads, a concrete call is accepted only when its container has a unique applicable read-index operation for the key type. The expression has that operation's result type. For writes, a concrete call additionally requires a unique applicable write-index operation, a compatible assigned value, and ordinary ownership/effect checks. A type mismatch or absent/ambiguous operation is a Moss diagnostic at the concrete call, with source provenance for the indexing expression and call.

Indexing does not prove that a key exists or that an index is in bounds. Existing runtime behavior of built-in containers remains authoritative. Failure/precondition and recovery design is outside this phase (SWARM-040, Phase 21).

## SWARM-044: built-in indexing inference

Implement the rule for each currently supported built-in indexing form using the compiler's existing collection semantics. Do not introduce a generic runtime index operation or change built-in key, bounds, or missing-key behavior.

Representative positive cases:
- `fn first(xs): return xs[0]` called with `Vector[Int]` and `Vector[String]` in either order: distinct static specializations and corresponding result types.
- `fn at(xs, key): return xs[key]` called with supported concrete container/key pairs: the container constrains the key; incompatible pairs fail.
- Indexed assignment through an untyped parameter retains WRITE effects and value-type and ownership checks.

Representative negative cases:
- A non-indexable concrete type, incompatible key, incompatible assigned value, or read-only target fails in Moss checking, not in generated Rust.
- A valid but empty vector type-checks; bounds behavior remains the current runtime contract.
- Generic helpers called through another generic helper, explicit modules, and static specializations remain order-independent.
- A function whose indexing requirement cannot be resolved at a concrete use must receive a precise diagnostic rather than infer from the first caller or assert in lowering.

Native lowering and Fast Debug must agree on accepted cases and diagnostics where Fast Debug supports the operation. Structured semantic queries should report the specialized result/effects consistently.

## EXPRESS-008: user-defined indexing contract (separate workstream)

Extend the same per-call rule to user-defined types that explicitly opt into a compiler-recognized *static* indexing contract. Merely defining an ordinary method named `get` or `set` must not make `[]` work. Read and write capabilities are distinct. A type may have multiple key types only if applicability for each concrete key is unique; ambiguity is a compile-time error. No operator overloading framework, runtime lookup, or dynamic dispatch is implied.

Before implementation, the agent must inspect Moss's existing structural trait declaration and method-resolution rules and propose the smallest source spelling that fits them (including key and result types, indexed assignment, and effect inference). Do not guess syntax in the compiler. Record concrete examples and counterexamples, and flag any choice that changes existing syntax or semantics for review before coding this workstream. The semantic direction above is settled; the exact opt-in surface and any missing-key behavior for user-defined types remain a design checkpoint. After that checkpoint, implement and test the user-defined path separately from the SWARM-044 repair.

## Agent execution instructions

1. Read `AGENTS.md`, repository-local `moss-language` and `moss-agent-workflow` skills, and `.codex/CURRENT_STATUS.md`; run `./moss agent bootstrap --json` and verify `moss-0.1`. Follow `examples/swarm/SWARM.md` and the canonical issue/feedback templates.
2. Reduce SWARM-044 to fresh native and Fast Debug probes before modifying the checker. Record current diagnostics and the existing Vector/Map indexing and assignment rules.
3. Write positive and negative tests for call-order independence, return type, key constraints, assignment effects/ownership, nested specialization, and modules. Include a check that native and Fast Debug agree where supported.
4. Implement SWARM-044 in the checker/specialization path; ensure lowering and semantic tooling consume the same resolved operation. Avoid opportunistic changes to general operator overloading.
5. Independently prepare EXPRESS-008's concrete opt-in syntax proposal and edge-case matrix. Seek a language decision on the opt-in surface before implementing it; preserve the separately testable SWARM-044 result.
6. Validate focused cases and repository gates (`make check`, `make examples`, `make all`, issue/feedback validators, and `git diff --check`). Record exact passes, failures, commits, and outstanding design choices in `.codex/CURRENT_STATUS.md`. Keep tracker statuses accurate; a proposal is not a fixed implementation.

## Exit criteria

SWARM-044 is complete only when accepted built-in indexing helpers specialize independently at all concrete calls and invalid calls receive Moss diagnostics, with regression coverage for both read and write paths. EXPRESS-008 is complete only after its opt-in syntax is explicitly settled and user-defined read/write indexing follows the same static rule with native/Fast Debug parity. Neither closes SWARM-043 by implication.
