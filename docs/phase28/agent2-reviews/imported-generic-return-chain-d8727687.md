# Independent review: source-free multi-hop generic ReturnValue

**Candidate:**  d8727687f6fcf9e38607dfd88978db26720db544
**Base:**  1d5feb1ea237e1d57297d38af0734cb65722c348
**Scoped verdict:** **GO** for source-free ABI-v10 multi-hop generic ReturnValue authority. The ABI-v9 compatibility controls pass; they do not independently establish an ABI-v9 source-deletion guarantee. This is not a whole-Phase-28 GO.

## Independent validation

The worktree was clean at review start and HEAD matched the exact candidate. I independently built isolated dev and optimized compilers:

```sh
make --jobs="$(nproc)" COMPILER_DIR=tmp/review-return-chain-build moss-dev moss-opt
```

The build completed successfully. I ran:

```sh
python3 tests/tooling/check_phase25_r2_wp1_call_chain.py tmp/review-return-chain-build/moss-dev
```

Result: **R2 WP1 three-hop generic result chain, source-backed Debug/Release interfaces, ABI-v9 controls, poisoned legacy bodies, and hostile closure: PASS**.

The gate stages provider/consumer ABI-v10 Debug and Release artifacts, removes the legacy generic_stmt representation from the imported interface, and builds/runs consumers that call fresh() -> middle() -> relay() for both Vector[Int] and Vector[Bool]. It checks expected output [17] / [true], checked-body callable trace markers, hostile identity/type/body/dependency mutations, five checker-side fact corruption modes, six Native consumer corruption modes, six Fast Debug corruption modes, and crossed specialization isolation: a Vector[Int] specialization perturbation leaves the Vector[Bool]-only consumer passing. The script also passes ABI-v9 Debug/Release compatibility controls. It does not explicitly assert that ABI-v9 provider source is deleted, so I do not claim source-free ABI-v9 coverage.

git diff --check HEAD^..HEAD passed. I did not edit compiler source, tests, registries, allowlists, or implementation files; only this review report was added.

## Authority and identity review

The imported .mossi loader assigns Function.imported_checked_body_semantic_hash from the loader-validated ABI-v10 checked-body record. During consumer specialization, the checker requires one exact imported function owner, exact specialization object and stable key, exact body statement occurrence/ordinal, unique ReturnValue handle, matching arena/ExprId, nonzero handle fingerprint matching the statement source fingerprint, and closed checker-selected return TypeRef. A closed conflicting EST result or pre-existing closed context TypeRef fails; an open generic ReturnValue EST root is replaced only at the exact specialization-specific context by the checker-selected closed return TypeRef. That context includes the stable specialization key. The specialized fact is materialized after import; no consumer type inference or .mossi format version change is introduced.

The source-free occurrence record deliberately has no source byte span. Instead it requires the checked-body semantic hash, statement ordinal, exact handle source fingerprint, and specialization identity. Native and Fast Debug require the imported projected function owner to be unique, its module to be external and uniquely identified, the fact’s defining-module/callable provenance to match, and the helper typed_return_value_source_free_occurrence_matches to find exactly one ReturnValue handle at the recorded statement ordinal in that same body hash. Requirement/fact lookup checks exact statement, arena, ExprId, specialization context and identity and compares all fact fields. Missing/duplicate/type/context/specialization/body/statement/fingerprint corruption fails closed in the gate.

## Native, Fast Debug, and copy review

The checker marks returned call-chain results as TransferOwnedValue with checker-proved owned-rvalue authority. Native consumes the exact ReturnValue fact and emits the returned expression directly; the transfer path is a Rust move and contains no clone fallback. Fast Debug consumes the same fact. For a transfer whose source is an owned local it moves and erases that binding; for the fresh call-chain expression used here it evaluates the call result directly and does not call independent(). No implicit Moss copy or new clone authorization is added. The two concrete specialization runtime outputs passed in Debug and Release.

## Scope boundary

This is a scoped GO for the exact source-free ABI-v10 multi-hop ReturnValue chain and its covered ABI-v9 compatibility controls. The gate does not establish full Default/Shadow suite recovery, the frozen ownership oracle, whole-engine parity, or independent runtime clone provenance. Whole Phase 28 remains separately gated.
