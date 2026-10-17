# Team G: unresolved contract decisions for L7

**Status:** open. Team G is **not** ready for Phase 25 integration. The items
below are contract decisions, not completed work. Each needs an architectural
resolution before `G-Complete` can be declared.

- Branch: `phase25-generic-types`.
- Frozen authorities: `TYPE_IR_CONTRACT.md`, `ABI_V10_DESIGN.md`, and
  `TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md`.
- Review basis: independent Architect review `TEAM_G_G2_ARCHITECT_REVIEW.md`,
  which issued **NO-GO** for the Type IR milestone.
- The user deferred items D1 and D2 to L7 on 2026-10-16. L7 then approved
  both for full implementation (see below). D3 and D4 still need resolution
  before Team G is declared ready.

## Settled scope (for reference, not open)

- **Gate 10 remains assigned to joint integration**, as agreed (agreement §2,
  §5). The compiled-only custom Map key and same-layout key substitution close
  on the joint Team S/Team G worktree.
- Until then, record Map keys fail closed with `RECORD_MAP_KEY_UNSUPPORTED`.
  `check_phase25_conformance.py` reports gate 10 as **PENDING**, not PASS. The
  final evidence runs with `MOSS_G2_ACTIVATE_JOINT=1`.
- Production native ABI remains v9. ABI v10 is staged behind
  `MOSS_STAGED_ABI=v10` until the joint cutover.

## L7 resolution of D1 and D2 (2026-10-16)

L7 approved **full implementation** of D1 (G-A-1) and D2 (G-A-3) during
Phase 25, as P0 work required before G-Complete. Option (b) of D1, a narrower
string-based hybrid, is rejected. The order is G-A3 first (Checkpoint A), then
G-A1 (Checkpoints B–E). Each checkpoint is committed separately, and a fresh
independent Architect review closes the work. Neither item may be deferred
again without a new L7 decision. D3 and D4 remain open as written below.

**Checkpoint A (D2): implemented; CONDITIONAL GO from the independent
Architect (`TEAM_G_GA3_ARCHITECT_REVIEW.md`, Addendum 2 at `27fc2fb`).** The
only open condition is an L7 record that ARCH-004 carries into Checkpoints B/C
and the ARCH-007 instance-key work into the ABI-v10 record work.

- **Identities.** Records, enums, and domains are interned once per
  compilation session as `NominalDeclaration` records carrying their
  identities:
  - `nominal-template-v1:<module-v1 ...>:type:<name>`;
  - for enums, `nominal-enum-v1:...:enum:<name>`;
  - for domains, `nominal-domain-v1:...:domain:<name>`.

  Equality, closure, `type-key-v1`, and §3.2 instance keys use these identities
  and the ordered resolved fields. Keys stay terminating for recursive layouts.
- **Module identity.** Local modules take their owning `Moss.toml`, so
  dependency sources keep their own package under Fast Debug. Loaded providers
  take their interface identity: v10 `module_id`, or the v9 `package_version`
  line.
- **ABI v10.** Each exported nominal carries a `declaration_id` that the
  loader validates. Two differently sealed providers claiming one identity, or
  a provider whose imports do not match the selected providers, are rejected
  with `SOURCE_FREE_MODULE_ID_CONFLICT`.
- **Semantic queries** report the bound identity and instance key.
- **Evidence:** `tests/tooling/check_phase25_nominal_identity.py` (cases A–E)
  and `tests/tooling/check_phase25_type_ir.cpp`.
- **Limitation, carried into D1.** Outside the Type IR algebra, the checker
  still compares type spellings in many places. These spellings are unique per
  declaration within a session, but replacing them with `TypeRef` comparisons
  is Checkpoint B/C work.

## D1. Authoritative Type IR (Architect finding 1)

**Contract.** `TYPE_IR_CONTRACT.md` §§1–2 and §§4–6 define one type algebra
with allocated inference variables, unification, requirements, and structured
closure. Every decision runs on `TypeRef`, and canonical strings are only
projections. The L7 plan's G-A acceptance criterion: no compiler-critical type
equality, substitution, or specialization decision may depend on splitting or
matching display strings.

**Current state (evidence).**

- `TypeSolver` (`src/type_ir.hpp`) has unit tests but is never called from
  `src/moss.cpp`.
- The checker carries types as strings. `TypeEnv` maps names to strings, and
  `inferred_expr_type` returns a string. Nominal instances are constructed as
  display strings such as `Box[value=int]`.
- `is_type_compatible` still decides placeholder and open-collection
  compatibility by string prefix (`_generic:`, `"vector"` against `"vector["`).
- Parsed `TypeRef` now backs equality (`same_type`), closure (`is_closed`,
  including exact collection arity at `1e72261`), and every specialization
  identity (`specialization_stable_key`, `2a5314d`; this addresses Architect
  finding 2). These are projections parsed at each decision point. They are not
  a structured environment.

**Decision required.** Choose one:

- **(a) Full conversion, in reviewable milestones.** Structured `TypeRef`
  environments in the checker, then structured construction and substitution,
  then `TypeSolver`-driven Option B binding and closure. Each milestone gets
  full `make check` and an Architect review.
- **(b) Amend the acceptance criterion.** Accept canonical strings as the
  carried representation, provided every decision parses into `TypeRef`, and
  state that amendment in the contract.

The work in (a) is large: it changes the core of a single 35k-line checker.

## D2. Nominal identity (Architect finding 3)

**Contract.** Contract §§2–3 and ABI-v10 §§3.1–3.2 require interned nominal
template IDs bound to `module-v1:<package>@<version>::<module>`, with stable
instance keys that hold across packages.

**Current state.** `Type::nominal` stores a template-name string and
field-name substitutions. There is no declaration-ID table, and
`parse_canonical` is context-free. Nominal identity therefore relies on
module-qualified symbol names such as `Provider__Box`, not on package-qualified
declaration IDs.

**Decision required.** Decide whether the declaration table, and its
context-dependent parsing, is a prerequisite for the ABI-v10 cutover or may
follow it. This item depends on the choice made in D1.

## D3. Typed ABI-v10 payloads

**Contract.** ABI-v10 §3 requires canonical typed records: `NominalTemplate`,
`ConcreteInstance`, `OpenCallableSummary`, `Specialization`, method templates,
`MapKeyProof`, and provenance. It also requires a separate
`private_semantic_dependencies` section and the §5 validation order through
record level.

**Current state.** The ABI-v10 container is integrated behind
`MOSS_STAGED_ABI=v10` (commit `4e7f44b`, documented in
`TEAM_G_ABI_V10_STAGING.md`).

- Implemented: emission of a package-qualified `module_id`, import hash
  binding, SHA-256 section hashes, interface hash, provider digest, and the
  v10 rlib marker. Loading validates in this order: framing, digest, selected
  rlib, then hashes.
- Not implemented: the record **payloads** still carry v9 declaration lines.
  `private_semantic_dependencies` and `provenance` are emitted empty.
- Unused: the typed codecs in `src/abi_v10_records.hpp` exist but are not
  emitted or consumed.
- Not run: the full §11 proof matrix (for example cache invalidation, and
  evidence of unique template/instance/specialization counts).

**Decision required.** Confirm that typed payloads must use the D1 structured
IR (the staging note assumes so). Also decide whether the joint cutover may
use the interim payloads.

## D4. Runtime linkage and key proofs

**Contract.** Agreement §1 and ABI-v10 §2 require one compiler-owned
`moss_runtime` crate:

- built once, linked into every provider, consumer, and executable;
- its identity recorded in the `binary_contract` and in cache keys;
- an identity mismatch fails before rustc.

Agreement §2 additionally requires that `MapKeyProof` exists, at least for
built-in keys.

**Current state.**

- The crate exists, with a policy-free `MossMap<K,V>`/`MossSet<K>`, unit tests,
  and a Rust two-consumer proof. The compiler neither builds nor links it.
- Generated crates still embed their runtime prelude, and Map lowering uses
  Rust `HashMap`.
- No `MapKeyProof` record is emitted.
- Per-argument access and the `MemberDispatchRequirement` hook (agreement §4
  amendment) are present only in the staged codec, not in the compiler's
  `Requirement`.

**Decision required.** Decide whether runtime link plumbing,
`MapKeyProof` emission, and the §4 requirement shape are Team G deliverables
before the joint integration trigger (§5), or are built jointly during it.
Agreement §5 currently lists them as trigger conditions.

## Other open items (not contract decisions)

- The L7 decision on Rust-colliding identifiers is still pending. The gate 6
  and gate 9 fixtures use `item` instead of `box`.
- No fresh independent Architect review has been run at a complete SHA.
- The Team closeout record `docs/phase25/agent-patterns/team-g.md` has not
  been written.
