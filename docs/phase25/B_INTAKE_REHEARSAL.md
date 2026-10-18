# Phase 25 Team I — Team B merge rehearsal

This is a disposable ancestry-preserving rehearsal, not the reviewed B intake
PR or an integration-target merge. It starts from S/H candidate `9a04230` and
merges pinned Team B `755f13172c37447bca981d1fefbb885ec46f3a59`.
The eventual B PR must start from the latest reviewed `phase25-integration`
target, preserve this input ancestry, and pass its own exact-head CI and L6
review.

The merge had three textual conflicts and no compiler-source conflict:

| File | Resolution |
| --- | --- |
| `.codex/CURRENT_STATUS.md` | Retain the current integration recovery log. Team B's historical closeout remains in its delivery report and branch ancestry. |
| `docs/phase25/TEAM_A_TYPED_SEMANTIC_API.md` | Retain Team A's canonical implementation contract and preserve Team B's distinct agreement as `TEAM_B_TYPED_SEMANTIC_API_AGREEMENT.md`. Update Team B review references. |
| `tests/run.sh` | Retain L7's option 1 exact `ECHO_DISPLAY_UNSUPPORTED` assertion for `unresolved_collection_type`; Team B's older fallback accepted two mutually exclusive diagnostic paths. |

The runtime, ABI record, linker helper, and Team B test changes merge without
textual conflict. This only demonstrates mergeability. Compiler binding of
Team B's typed records and shared runtime, nominal `MapKeyProof` Gate 10,
same-SHA source-free proof, and production ABI-v10 cutover remain open.

Rehearsal validation: compiler build, `check_agent_feedback.py`, Team B
debug/release runtime source-removal and identity checks, and both ABI-v10
staged/adversarial C++ suites pass. Team B's acceptance suite reports
**22 PASS, 1 PENDING, 0 FAIL**; the pending check is correctly Gate 10. Its
source-free G2 Gate 5 test was tightened to require the new exact
`METHOD_NOT_FOUND` code and original `provider.moss` related origin.

The frozen ABI design requires robin-hood open addressing for production
`MossMap`, while the pinned Team B delivery used chained bucket vectors.
The rehearsal follow-up replaces that storage kernel with robin-hood probes,
growth, and backward-shift deletion without changing the public operation API
or adding Rust `Eq`/`Hash` bounds. A collision-heavy differential check against
an independent ordered-map oracle and Team B's complete debug/release runtime
suite pass. Compiler lowering and key-proof wiring remain separate work.

## Shared indexed String staging checkpoint

The opt-in combination `MOSS_STAGED_ABI=v10` and
`MOSS_STAGED_STRING_REPR=indexed-v1` now links every generated Rust crate to one
content-addressed `moss_runtime` rlib. The runtime's String module contains
Team S's indexed-v1 implementation; generated crates import its type and
operations rather than defining separate nominal Rust types. Margo supplies
the runtime source path to dependency builds. The v10 binary contract records
the String representation and exact runtime build/artifact hashes, and a
source-free consumer checks the selected provider's runtime identity before
Rust linking. A runtime change also invalidates a warm native cache.

`tests/phase25/runtime/check_shared_string_linkage.py` builds a provider,
removes its Moss source, compiles two independent consumers in debug and
release, executes both, then changes the runtime source and requires the exact
`SOURCE_FREE_ARTIFACT_CONTRACT_MISMATCH` diagnostic before native linking. This
is a staged boundary proof, not a production ABI-v10 cutover or Gate 10 closure.
Typed ABI records, compiler Map/Set lowering, nominal MapKeyProof, and the full
same-SHA acceptance suite remain open.
