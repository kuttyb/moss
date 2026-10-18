# Team G independent Architect review: Type IR milestone

**Verdict: NO-GO for the Type IR/solver milestone.** This is an independent,
read-only implementation review, not a verdict on a future complete G2 SHA.

- Milestone commit reviewed: `e6ef8370a712bb3b01cf72d29ad3e9545b602660`.
- Integrated commit reviewed: `070fd84a9b90f2f26df6f83a5f08f5f16fbc80d1`.
- Frozen authorities: `TYPE_IR_CONTRACT.md` and `ABI_V10_DESIGN.md` in this directory.
- Bootstrap: `./moss agent bootstrap --json` returned `moss-0.1` at the integrated commit.

## Blocking findings

1. **`TypeRef` is not the compiler's semantic source of truth.**
   `src/moss.cpp` constructs nominal instances as display strings in
   `builtin_call_semantics` (around line 4290). `TypeSolver` has no use in that
   file. Its new `TypeRef` uses occur only when reparsing strings in a few
   helpers, including `same_type` and `is_type_compatible` (around lines 2555
   and 4657). The latter retains `starts_with` and placeholder `_value` /
   `_generic:` compatibility decisions. This misses the contract's one-algebra,
   allocated-variable, and structured-closure requirements (§§1–2, 4–6).

2. **Specialization identity still depends on display strings.**
   `concrete_specialization` compares `vector<string>` parameter types (around
   line 539), `ensure_function_specialization` locates and deduplicates them by
   string-vector equality (around line 13085), and
   `specialization_projection_key` joins canonical type strings (around line
   30198). These are compiler-critical specialization decisions and violate
   `TYPE_IR_CONTRACT.md` §3.3 and the explicit G-A acceptance criterion. The
   isolated `Type::stable_type_key()` helper is never used there.

3. **The new IR is a useful parser/solver component but does not encode frozen
   nominal identities.** `Type::nominal` stores a template-name string and
   field-name substitutions; `NominalTemplate` stores string IDs and
   `ResolvedNominalInstance` stores string substitutions. There is no interned
   declaration ID table or checked binding to the required
   `module-v1:<package>@<version>::<module>` identity. Its stable type key is
   computed on those unbound names, so it cannot yet establish exact
   cross-package nominal or instance identity (§§2–3 and ABI-v10 §§3.1–3.2).

## Reproduction and scope

`python3 tests/tooling/check_phase25_type_ir.py` passed at the integrated SHA.
That test directly exercises the header parser/solver; it does not show compiler
use of the solver. `python3 tests/tooling/check_phase25_conformance.py ./moss`
passed gates 1, 2, 3, and 7 and failed gates 4, 5, 6, 8, 9, 10, and 11.
Those latter gates were already identified as incomplete before this review;
they are not attributed solely to the Type IR milestone. In particular gate 6
returns `TYPE_MISMATCH` for a known-open `Box` parameter, gate 8 returns the
generic `RECURSION_CYCLE` diagnostic without the changing-key witness, and
the source-free gates lack ABI-v10 proof.

The targeted milestone may proceed as a partial implementation, but it cannot
be signed off as authoritative Type IR. A re-review needs compiler paths for
construction, substitution, equality, specialization and closure to operate
on structured terms, plus evidence that string projections cannot determine
any of those decisions. A complete G2 review requires a later exact SHA and
the full conformance and ABI-v10 proof matrices.
