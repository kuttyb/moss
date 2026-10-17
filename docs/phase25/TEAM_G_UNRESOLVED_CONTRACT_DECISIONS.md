
## D5. Specialization identity for an identical concrete call (Team A, 2026-10-17)

**Status:** open, and currently a `make check` blocker.

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

Team A has not chosen between these, because the answer sets Option B
identity rather than an implementation detail.
