
## D5. Specialization identity for an identical concrete call (Team A, 2026-10-17)

**Status:** resolved 2026-10-17 by user decision as L7: **option (a)**.

A generic function called from two handlers with the same concrete argument
type registers **two** specializations that differ only in
`open_result_bindings`: the call whose result is bound to a local carries a
binding with ordinal 1, the call that replies directly carries none. Argument
types and the concrete result are identical.

`concrete_specialization` accepts a parameter-only lookup only when it names
one checked instance, so the call site that does not supply an exact key
fails with `missing static specialization for function 'generic_score' at its
checked call site`. The fork point `5b29352` emits exactly one specialization
for the same program.

- Reproducer: [`repro/duplicate_open_result_specialization.moss`](repro/duplicate_open_result_specialization.moss) (20 lines).
- In-repo case: `tests/phase106d1_read_projection.moss`, which blocks
  `tests/tooling/check_borrowed_reads.py`.
- Observed keys: `specialization-v2:sha256:0caedaf2…` (no bindings) and
  `…88aecb9c…` (one ordinal-1 binding) for one `callable_id`.

**Decision required.** May two call sites of the same generic, with identical
argument types and an identical concrete result, carry different
specialization identities (§§3.3, 4.2)? Candidate directions:

- **(a)** Do not record an open-result binding when the result closes to a
  concrete type the argument types already determine, so one specialization
  is registered.
- **(b)** Keep both identities and require every call site to supply its
  exact key, extending the `open_call_keys_by_context` path to cover a
  result-bound call in a handler.

**Resolution (option (a)).** A result variable that is already resolved when
a call is checked -- by the arguments, or by the member requirements those
arguments close -- is excluded from `open_result_bindings` and so from the
specialization key. A variable still open at that point is the caller's
choice, as in `fresh() -> []`, and remains part of the identity. The rule is
applied at all three sites that record bindings: the body prepass, the
open-result closure, and the returned-call path.

Evidence: the reproducer and `tests/phase106d1_read_projection.moss` emit one
specialization again and lower. Across the 1862 tracked sources no program
changed between accepted and rejected; 13 lower differently, of which
`implicit_domain_specialization` drops one duplicate specialization and the
rest differ only in the specialization keys embedded in their tooling
markers. Fast Debug output is byte-identical for every affected program.
