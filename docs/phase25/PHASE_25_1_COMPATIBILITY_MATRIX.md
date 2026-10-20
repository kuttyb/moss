# Phase 25.1 compatibility matrix

Updated 2026-10-20. R0 received final independent GO at frozen checkpoint `9c90573bda8daec5bd4254cc5f6c64c5dd3239e4` with no open P0/P1; only P2 `packages/time` remains, tracked for R4. The expanded oracle
uses compiler SHA `19ffd63822f08c255cde231b590828a986e9a00b`; package inventory
artifacts were built at `43973e1adfdf015d5588e85f41d986925a85a565`, with no
compiler source changes between those commits. This is not a v10 readiness
claim.

## Collection and source-free inventory

`tests/phase25/compatibility/inventory.py` builds every production package
manifest under `sdk/` and `packages/` serially, then reads generic export rows
from compiler-emitted `.mossi` `semantic_exports`. It also catalogs existing
source-free harnesses separately; the mere presence of a harness is not a
claim that it covers every generic export. Machine-readable output is
`PHASE_25_1_GENERIC_INVENTORY.json` and includes the exact compiler commit,
artifact hashes, build failures and generic callable records. The original six-case
oracle and latest 19-case expanded oracle are preserved in
`PHASE_25_1_R0_ORACLE.json` and `PHASE_25_1_R0_FULL_ORACLE.json`; invalid
language probes are in `PHASE_25_1_R0_INVALID_PROBES.json`.

The compiler-emitted scan completed 26 production package roots. Twenty-five
build successfully and emit 4 open generic callables across
`sdk/foundations/bytes` and `sdk/foundations/collections`. One package,
`packages/time`, fails its source build at `time__pad_digit` (declared `string`,
inferred `char`); it remains an explicit unclassified package-export inventory
gap until its generic surface is independently established. See the machine
record for exact artifact paths, hashes, and diagnostic. The source-free marker
scanner found 71 paths. These are discovery records, not automatic coverage
claims; exact callable mappings and explicit no-open-generic cases are in
`PHASE_25_1_HARNESS_MAP.json`.

Current source-free harness families:

| Area | Existing proof path | Existing mode/coverage |
|---|---|---|
| ABI-v10 generic corpus | `tests/tooling/check_phase25_m2b2_source_free_corpus.py` | Synthetic v10 positives/negatives; v9 controls on supported rows; generic Vector iteration requires provider artifact emission, source deletion, then rejection at consumer selection. |
| Collections | `tests/tooling/check_phase25_d3d_collection_source_free.py`, `check_phase25_d4_collection_source_free.py`, `sdk/foundations/collections/tests/check_source_free.py` | Actual `index_of`, `contains`, `count_equal` source-free execution; v10 loop poison plus v9 package smoke. |
| Bytes / Range | `sdk/foundations/acceptance/validate_source_free.py`, `sdk/foundations/acceptance/repros/source_free_range/src/main.moss` | ABI-v9 and staged-v10 oracle use the `source_free_range` acceptance repro and output `329`; the actual `sdk/foundations/bytes/examples/range_copy` program is an R3 requirement and has not yet been run source-free. |
| Open results and mixed admission | `tests/tooling/check_phase25_r2c_empty_vector_consumer.py`, `tests/tooling/check_phase25_mixed_provider_admission.py` | Both source-free generic gates were missed by name-only inventory; now included in verified marker scan and explicit harness map. Empty-vector positive v10 still uses the hidden provider flag. |
| Advanced data | `sdk/advanced-data-acceptance/check_source_free.py` | 14-provider compiled-only ABI-v9 graph, Debug. The consumer imports seven modules and calls typed concrete APIs; it does not select any of the four open generic exports. The bytes provider is built but not imported by this consumer. |
| Encoding, text, filesystem, path, algorithms, data | respective `sdk/**/tests/check_source_free.py` and `sdk/data-acceptance/tests/check_source_free.py` | Existing compiled-only v9 controls; must be cloned/adapted for v10 where generic exports are actually selected. |
| Phase 25 record/callable proof | `tests/tooling/check_phase25_h3_*`, `check_phase25_r2c_empty_vector_consumer.py`, `check_phase25_mixed_provider_admission.py` | V10 checked-body shapes, poisoning, mutation negatives; does not establish whole-SDK parity. |

The new `tests/phase25/compatibility/oracle.py` is the R0 three-mode runner. It
uses independent temporary package roots and Margo caches for source-backed,
ABI-v9 source-free and staged-v10 source-free providers/consumers in Debug and
Release. V10 removes the selected callable's legacy `generic_stmt`, requires a
checked projection trace, physically deletes provider source, and records
build-stage/diagnostic/output evidence. `--case <id>` limits iteration; the
default runs every case in the existing source-free corpus. Provider build
failures stay red rather than being silently skipped.

## R0 evidence rows

The expanded run contains 19 cases and 38 Debug/Release profiles per mode.
Source-backed passed 38/38. ABI-v9 passed 36/38; the two failures are the
generic raise/recover consumer's existing `raise requires a statically typed
error value` diagnostic. ABI-v10 passed 12 profiles across six supported generic shapes and rejected
the other 26 valid profiles at
source-free callable selection with `SOURCE_FREE_CHECKED_BODY_UNAVAILABLE`.
The full commands, build logs, outputs, hashes and stage diagnostics are in
`PHASE_25_1_R0_FULL_ORACLE.json`.

| Workload | Language validity / current v9 evidence | ABI-v10 evidence | Classification |
|---|---|---|---|
| Generic parameter return (`fresh(value)`) | Source-backed and ABI-v9 source-free native Debug/Release pass for Int and String. | V10 Debug/Release pass after source deletion, legacy body poisoning, and checked-projection trace. | `supported-v10`; R0 oracle. |
| Generic scalar literal, field read, direct private call | Source-backed and ABI-v9 source-free native Debug/Release pass. | V10 Debug/Release pass after source deletion, full selected legacy-body poisoning, and checked-projection trace. | `supported-v10`; R0 oracle. |
| Generic `for value in data` over Vector, counting without constraining element type | Source-backed and ABI-v9 source-free native Debug/Release pass. | Provider emits an ABI-v10 interface, but source-free consumer rejects with `SOURCE_FREE_CHECKED_BODY_UNAVAILABLE` in Debug/Release. Existing M2b2 gate asserts this at consumer selection. | `valid-but-unavailable-v10`; P1 iteration/effect work. |
| SDK `collections.index_of`, `contains`, `count_equal` | Compiler inventory finds all three. Existing compiled-only v9 SDK smoke passes; R0 synthetic source-backed/v9 Debug/Release parity passes. | D3/D4 actual SDK source-free Int/String and empty collections pass with checked projection and legacy record removal counts 7/7/8; D4 malformed requirement/effect cases reject. | `supported-v10`; actual SDK D3/D4 evidence. |
| SDK `bytes.from_range(data)` and `source_free_range` acceptance repro | Source-backed and ABI-v9 source-free native Debug/Release output `329`; provider source is physically removed. | V10 provider emits an interface, then source-free consumer rejects with `SOURCE_FREE_CHECKED_BODY_UNAVAILABLE` in Debug/Release. | `valid-but-unavailable-v10`; P251-GEN-001, R3. The actual `examples/range_copy` consumer remains to be run under R3. |
| Passing `range(1, 4)` as the generic iterator argument | Independent source-backed-invalid probe returns `MOSS_COMPILE_ERROR`: `type 'range[int]' is not statically iterable`. Direct `for i in range(...)` remains supported. | Not counted as a v10 gap. | `invalid-by-language`; P251-GEN-007. |
| `fresh(): return []`, `relay(): return fresh()` | Source-backed/v9 Debug/Release pass for empty-result Int/Bool specializations and relay dependency closure. Existing R2c proves v10 only with `MOSS_PHASE25_EMPTY_VECTOR_EMIT=1`. | Without the hidden flag, fresh and relay reject source-free v10 selection in both profiles. | `valid-but-unavailable-v10`; P251-GEN-002. |
| `size(value): return value.length()` | Source-backed/v9 Debug/Release pass for String member dispatch. Vector `.length()` is invalid; Moss uses `Count`/pipeline operations for Vector. | Source-free v10 rejects the valid String member callable in both profiles. | String case `valid-but-unavailable-v10`; P251-GEN-003. |
| Generic provider-owned nominal constructor with consumer field read | Source-backed/v9 Debug/Release pass for Int and String specializations. | V10 Debug/Release pass with source deletion, legacy body removal and checked projection. | `supported-v10`; H3 direct-record shape. |
| Nonempty Vector literal `[value]` | Source-backed/v9 Debug/Release pass for independent Int and String specializations. | Source-free v10 rejects selection in both profiles. | `valid-but-unavailable-v10`; P251-GEN-005. |
| Generic Map indexed write/read | Source-backed/v9 Debug/Release pass after the consumer seeds a concrete Map key/value type. | Source-free v10 rejects selection in both profiles. | `valid-but-unavailable-v10`; P251-GEN-005. |
| Generic `if` / early return, `let`/`var`, assignment, `while` and functional pipeline | Source-backed/v9 Debug/Release pass for each probe. | Source-free v10 rejects selection in both profiles. | `valid-but-unavailable-v10`; P251-GEN-005. |
| Generic match over a statically typed provider enum, with an open Int parameter used in the selected body | Source-backed/v9 Debug/Release pass for both enum variants and both concrete calls. | Source-free v10 rejects selection in both profiles. | `valid-but-unavailable-v10`; P251-GEN-005. |
| Generic `assert` effect | Source-backed/v9 Debug/Release pass for `assert(value > 0)` and Int specialization. | Source-free v10 rejects selection in both profiles. | `valid-but-unavailable-v10`; P251-GEN-005. |
| Generic `try` / `raise` / `recover` with provider enum | Source-backed Debug/Release passes. ABI-v9 source-free consumer fails both profiles with `raise requires a statically typed error value`. | Source-free v10 rejects selection in both profiles with `SOURCE_FREE_CHECKED_BODY_UNAVAILABLE`. | `valid-but-unavailable-v10`; P251-GEN-009 also tracks the independent v9 source-free failure. |
| Queue `.push` on an open generic receiver | Source-backed probe rejects with `MOSS_COMPILE_ERROR` / `unresolved statically dispatched method 'values.push'`; concrete Queue calls remain valid. | Not run as v10 because this exact generic form is invalid. | `invalid-by-language`; P251-GEN-010. |
| Exhaustive enum `match` on an untyped open parameter | Source-backed specialization fails with `TYPE_INFERENCE_FAILED`; the generic parameter remains unclosed. | Not run as v10 because this exact generic form is invalid. | `invalid-by-language`; P251-GEN-011. |
| Production package exports and existing source-free consumers | 26 roots inventoried; 25 build, one source failure; four open generic callables identified. The selected callable mappings are explicit; 71 marker/name candidates are catalogued. | Three collection helpers have v10 proof; bytes has explicit red result. R2c and mixed-admission tests are included despite lacking `source_free` in their names. | Inventory gap `packages/time` remains P2; no discovered generic export silently omitted. |
| Filesystem native SDK acceptance | `sdk/fs/tests/check_native.py` builds a native consumer with the path dependency and provider source available; it exercises concrete FileIO calls and selects no inventoried open generic export. | Not a source-free or generic checked-body parity claim. | Source-backed native control only. |
| Phase 24 imported generic expressions (`probe.make`, `contains`, `index_of`) | ABI-v9 source-backed and source-free checks pass after provider source removal; compiled-only consumer executes nominal construction/field access, Vector count/index loops, assertions, and nested generic calls in Debug/Release. | No v10 claim in this legacy harness; D3/D4 provide the actual SDK v10 collection exports. | Existing ABI-v9 source-free generic coverage, mapped from `tests/tooling/check_phase24_imported_generic_expressions.py`. |
| Existing cross-crate generic, Range, and effects controls | Phase 15.9 proves v9 wrapper/helper/structural trait specialization with provider source hidden; Phase 20 separately proves generic Range member use source-free; Phase 21 preserves generic raise-set and enum identity in v9 artifacts. | These legacy fixtures do not establish the staged-v10 checked-body contract; the three-mode R0 oracle and later R gates do. | Mapped in `PHASE_25_1_HARNESS_MAP.json`. |
| Team A and typed semantic API generic source-free controls | Team A v9 proves fresh/relay/member calls after provider source deletion; typed API captures imported open summaries and dependencies. | Team A and typed API include v10 fresh positive behind `MOSS_PHASE25_EMPTY_VECTOR_EMIT=1` plus relay/member and unsupported body rejection; not default unflagged readiness evidence. | Existing specialized controls, mapped in `PHASE_25_1_HARNESS_MAP.json`. |
| Advanced-data provider closure | 14 provider pairs built; 5 Team A modules included; consumer source is absent from module artifacts. It imports seven providers and uses concrete typed APIs; no open generic export is selected. | No v10 generic parity claim for this fixture. | ABI-v9 baseline passed; v10 selection map is in `PHASE_25_1_HARNESS_MAP.json`. |

## R0 typed generic surface map

The first R0 checkpoint left several core language shapes `unverified` and
was rejected by independent review. `tests/phase25/compatibility/surface_probes.json`
adds A/B controls for open results, member requirements, nonempty Vector
literals, Map operations, branch/early return, typed match, assert and
raise/recover. Their three-mode outcomes are preserved in
`PHASE_25_1_R0_FULL_ORACLE.json`. Invalid open Queue method dispatch and
untyped enum match are separately recorded in
`PHASE_25_1_R0_INVALID_PROBES.json`. The evidence map below makes explicit
which valid shapes have direct positive controls and which remain outside the
representative oracle; no unsupported shape is treated as invalid by omission.

| Moss body axis | Direct R0 evidence | Current classification |
|---|---|---|
| Parameter/local reads, inferred/open type, scalar return, helper call and private dependency closure | `parameter-return`, `scalar-literal`, `single-call`, `generic-zero-argument-relay` | Baseline forms include supported-v10 cases; relay is valid-but-unavailable-v10. |
| Explicit local initialization/rebinding and mutation | `generic-local-var-while`; SDK collection and hostile source-free corpus controls | Let/var/assignment is valid-but-unavailable-v10; individual inferred READ/WRITE/CONSUME combinations remain R1/R2 codec invariants, with malformed/overlap controls in existing ownership suites. |
| If/else, early return, match/case | `generic-if-early-return`, `generic-typed-enum-match`; untyped enum match invalid probe | Valid branch and typed match are valid-but-unavailable-v10; untyped open match is invalid-by-language. |
| While/for and static iteration | `collection-loop-equality-index`, `generic-range-iteration`, `generic-local-var-while`; range-as-helper-argument invalid probe | Existing supported loop shape passes v10; Vector iteration and local-var/while are valid-but-unavailable-v10; range value argument is invalid-by-language. |
| Vector and Map construction/indexing/mutation; Queue methods | `generic-empty-vector-result`, `generic-nonempty-vector-literal`, `generic-map-index-mutation`; open Queue `.push`/`.pop` invalid probe | Open results, Vector literal and Map indexed operation are valid-but-unavailable-v10; Queue generic method form is invalid-by-language. Generic Queue construction is not separately probed. |
| Field/member access and constrained member calls | `field-read`, `generic-member-length` | Field read supported-v10; String `.length()` is valid-but-unavailable-v10. |
| Operators, Count/Index, equality, pipeline and assertions/effects | `collection-loop-equality-index`, `generic-functional-pipeline`, `generic-assert-effect`, D3/D4 actual collection callables | Existing supported arithmetic/collection rows pass; pipeline and assert are valid-but-unavailable-v10. |
| Try/recover/raise | `generic-raise-recover` | Valid source-backed; ABI-v9 source-free currently fails in both profiles; v10 is valid-but-unavailable-v10. |
| Float, Char, nested collection and arbitrary nominal type parameter cases | Existing package/integration controls; no complete cross-product of open-body shapes and concrete specializations is claimed. | Residual surface to cover during R1–R4; not a silent test exclusion. |

- Statements: implicit/`let`/`var` initialization, rebinding and mutation,
  expression statements, return/early return, `if`/`else`, `while`, `for`,
  match/case, assert, raise, try/recover and `pass` where valid in an ordinary
  function.
- Expressions: parameter/local reads; Int/Float/Bool/Char/String literals;
  unary/binary operators; field and index access; nonempty/empty Vector, Map
  and Queue construction; nominal construction; static calls; constrained
  member calls; Count/Index and supported collection methods; supported
  pipeline operations; returned or nested helper calls. `for` may also select
  the current static user-defined iteration contract when its concrete use
  satisfies that contract.
- Semantic edges: TypeRefs/open variables and freshening; local identity,
  scope and mutability; READ/WRITE/CONSUME; field/member selection; operation
  and requirement identity; may-panic/raise/assert effects; nominal/provider
  identity; private helper dependency closure; source provenance; bounds and
  malformed artifact rejection.

The language contract has untyped ordinary functions specialized at concrete
uses and named structural traits; it has no user-defined generic type
variables, runtime trait objects, dynamic dispatch, or user operator
overloading. The surface map must cover valid specialized function bodies,
not invent syntax or treat a Moss trait as a storage type. The current practical
reference is §8–9 of `docs/GENTLE_INTRODUCTION_TO_MOSS.md`; formal
specialization and structural-trait rules are in §2 of
`docs/MOSS_V0_1_LANGUAGE_DESIGN.md`.

The exact valid subset for each row was established by `moss check --json` and
source-backed/native or ABI-v9 executable controls where marked. Rows
explicitly called partially mapped remain open inventory work; validity is not
inferred from a Rust artifact or a single failed v10 build.

## R0 completion criteria

- Compiler-emitted inventory completed for 26 production roots; 25 builds
  passed, and the exact `packages/time` source failure is retained as an
  unresolved inventory row. Every discovered generic export has explicit
  callable records and harness evidence.
- Existing source-free harnesses are discovered by explicit module-path, ABI,
  staged-body and diagnostic markers as well as path names; selected callable
  coverage and the 14-provider advanced-data concrete-call set are mapped in
  `PHASE_25_1_HARNESS_MAP.json`.
- The 3-mode oracle passes source-backed and v9 Debug/Release for the six
  original corpus workloads and the bytes acceptance repro. The expanded
  surface-probe run records A/B/C outcomes for open results, member calls,
  collection construction/operations, control flow and effects. Existing
  hostile mutation corpus passes; every passing v10 row requires source
  deletion, legacy poisoning and projection evidence.
- R0 architecture proposal and critical path receive independent adversarial
  review at a frozen pushed SHA; all P0/P1 findings close before R1.
