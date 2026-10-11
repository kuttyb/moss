# Phase 21 standard-error and Root boundary conformance

This is the engineering test contract for the L7-approved compiler-defined
standard errors. The final normative surface belongs in the Phase 21 language
specification and the ABI v9 specification.

## Standard namespace and migration rule

The compiler owns three stable nominal identities: `FileError`, `ParseError`,
and `ConversionError`. Their variants are the exact tag-only sets already
approved in `MOSS_PHASE_21_ERROR_HANDLING.md`: six FileIO causes,
`Invalid|Overflow`, and `NonFinite|Overflow`, respectively. Unqualified use
resolves to the standard identity when there is no conflicting local binding.
The implementation may expose a compiler-defined qualified standard namespace;
its spelling must be documented and covered by a source test once chosen.

An exact legacy local declaration of one of these enums is an alias for the
corresponding standard identity. This keeps existing Phase 20/21 programs and
interfaces source-compatible without creating another nominal error type.
A malformed legacy local declaration used as a substitute for a standard
operation receives `BUILTIN_ERROR_TYPE_SHAPE` (or a new equally precise,
documented diagnostic) at Moss checking. An unrelated imported enum with the
same short name stays provider-owned and must not change the meaning of the
unqualified standard error in the consuming module. Its qualified selector
remains distinct even when its variant spelling is identical. This separation
must survive source removal and `.mossi` loading.

`check_phase21_standard_error_namespace.py` exercises direct construction,
numeric and FileIO origins, native O0/O3, Fast Debug where FileIO is absent,
legacy migration, malformed substitutes, three imported namesakes with
identical variant spellings, and source-removed provider execution. The
`7775049` baseline fails the first declaration-free numeric case with
`UNKNOWN_ERROR_TYPE`; this is the expected pre-implementation failure.

## Root totality

`check_phase21_absolute_root_boundary.py` establishes that lexical `recover`
in `main` cannot serve as an ingress-specific error channel, Executor Roots
are total even with generic helper effects, a nested handler's `on_fail` is
not invoked by a nested message, and a Root's own trailer has no escaping
raise. It checks native O0/O3 and Fast Debug for the positive source-backed
cases. It rebuilds and runs source-free provider Roots, then checks a
consumer Root around an imported generic helper. The entire gate passes at
`7775049`; retain it after ABI v9 and standard-error changes.

The native Rust `runtime_invoke` boundary has additional existing coverage in
`check_phase21_lock_fileio_adversarial.py` and
`check_phase21c_branch_errors.py`. The source-free gate is native-only because
Fast Debug requires provider Moss source by contract.
