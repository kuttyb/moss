# Phase 25.1 compatibility matrix

Updated 2026-10-20. R0 evidence at compiler commit
`43973e1adfdf015d5588e85f41d986925a85a565`; review is pending. This is not a
v10 readiness claim.

## Collection and source-free inventory

`tests/phase25/compatibility/inventory.py` builds every production package
manifest under `sdk/` and `packages/` serially, then reads generic export rows
from compiler-emitted `.mossi` `semantic_exports`. It also catalogs existing
source-free harnesses separately; the mere presence of a harness is not a
claim that it covers every generic export. Machine-readable output is
`PHASE_25_1_GENERIC_INVENTORY.json` and includes the exact compiler commit,
artifact hashes, build failures and generic callable records. The machine
oracle result and invalid-language probe are preserved in
`PHASE_25_1_R0_ORACLE.json` and `PHASE_25_1_R0_INVALID_PROBES.json`.

The compiler-emitted scan completed 26 production package roots. Twenty-five
build successfully and emit 4 open generic callables across
`sdk/foundations/bytes` and `sdk/foundations/collections`. One package,
`packages/time`, fails its source build at `time__pad_digit` (declared `string`,
inferred `char`); it remains an explicit unclassified package-export inventory
gap until its generic surface is independently established. See the machine
record for exact artifact paths, hashes, and diagnostic. Existing source-free
harnesses discovered by path and SHA are candidates only; selected-callable
coverage below comes from inspection and execution.

Current source-free harness families:

| Area | Existing proof path | Existing mode/coverage |
|---|---|---|
| ABI-v10 generic corpus | `tests/tooling/check_phase25_m2b2_source_free_corpus.py` | Synthetic v10 positives/negatives; includes v9 controls on supported rows; Debug-oriented. |
| Collections | `tests/tooling/check_phase25_d3d_collection_source_free.py`, `check_phase25_d4_collection_source_free.py`, `sdk/foundations/collections/tests/check_source_free.py` | Actual `index_of`, `contains`, `count_equal` source-free execution; v10 loop poison plus v9 package smoke. |
| Bytes / Range | `sdk/foundations/acceptance/validate_source_free.py`, `sdk/foundations/acceptance/repros/source_free_range/src/main.moss` | Actual `bytes.from_range`; ABI-v9 source-free Debug output `329`; staged-v10 Debug/Release source-backed and v9 pass, v10 rejects at consumer selection with `SOURCE_FREE_CHECKED_BODY_UNAVAILABLE`. |
| Advanced data | `sdk/advanced-data-acceptance/check_source_free.py` | 14-provider compiled-only ABI-v9 graph, Debug; includes concrete and generic uses. |
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

| Workload | Language validity / current v9 evidence | ABI-v10 evidence | Classification |
|---|---|---|---|
| Generic parameter return (`fresh(value)`) | Source-backed and ABI-v9 source-free native Debug/Release pass for Int and String. | V10 Debug/Release pass after source deletion, legacy body poisoning, and checked-projection trace. | `supported-v10`; R0 oracle. |
| Generic scalar literal, field read, direct private call | Source-backed and ABI-v9 source-free native Debug/Release pass. | V10 Debug/Release pass after source deletion, full selected legacy-body poisoning, and checked-projection trace. | `supported-v10`; R0 oracle. |
| Generic `for value in data` over Vector, counting without constraining element type | Source-backed and ABI-v9 source-free native Debug/Release pass. | Provider emits an ABI-v10 interface, but source-free consumer rejects with `SOURCE_FREE_CHECKED_BODY_UNAVAILABLE` in Debug/Release. Existing M2b2 gate asserts this at consumer selection. | `valid-but-unavailable-v10`; P1 iteration/effect work. |
| SDK `collections.index_of`, `contains`, `count_equal` | Compiler inventory finds all three. Existing compiled-only v9 SDK smoke passes; R0 synthetic source-backed/v9 Debug/Release parity passes. | D3/D4 actual SDK source-free Int/String and empty collections pass with checked projection and legacy record removal counts 7/7/8; D4 malformed requirement/effect cases reject. | `supported-v10`; actual SDK D3/D4 evidence. |
| SDK `bytes.from_range(data)` and real Range-copy/FileIO consumer | Source-backed and ABI-v9 source-free native Debug/Release output `329`; provider source is physically removed. | V10 provider emits an interface, then source-free consumer rejects with `SOURCE_FREE_CHECKED_BODY_UNAVAILABLE` in Debug/Release. | `valid-but-unavailable-v10`; P251-GEN-001, R3. |
| Passing `range(1, 4)` as the generic iterator argument | Independent source-backed-invalid probe returns `MOSS_COMPILE_ERROR`: `type 'range[int]' is not statically iterable`. Direct `for i in range(...)` remains supported. | Not counted as a v10 gap. | `invalid-by-language`; P251-GEN-007. |
| `fresh(): return []`, `relay(): return fresh()` | Source-backed/v9 and exhaustive per-case evidence not yet in R0 oracle. | Existing separate R2c and per-callable tests establish only narrow behaviors; source-free ABI-v9 controls remain to run. | `unverified`; P251-GEN-002. |
| `size(value): return value.length()` | Exact structural member requirements not yet probed in R0. | No positive v10 source-free proof. | `unverified`; P251-GEN-003. |
| Generic nominal methods, Map/Queue operations, nonempty collection literals, match, branches, raises, try/recover, and effectful bodies | Language validity and shipped use sites require small source-backed/v9 probes. | No blanket support claim. | `unverified`; P251-GEN-004/005. |
| Production package exports and existing source-free consumers | 26 roots inventoried; 25 build, one source compile failure; four generic callables identified. The three collection helpers and bytes helper are linked to inspected/executed harnesses. | Three collection helpers have v10 proof; bytes has explicit red result. No other exported open generic is omitted. | Package inventory complete subject to `packages/time` source failure; other broad language forms remain `unverified`. |
| Advanced-data provider closure | Existing 14-provider / 5 Team A provider set built and ran compiled-only with source absent. | No claim that all 14 providers use an exported generic callable; R4 will extend v10 only where generic selection exists. | ABI-v9 baseline passed; R4 v10 mapping remains. |

## R0 typed generic surface map

Record the complete accepted generic body algebra from live compiler source
and real SDK callables. This is the Phase 25.1 coverage obligation, not a
claim that every form has already been probed. The following classes are
coverage axes, not unsupported-language claims:

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

The exact valid subset for each row must be established by `moss check --json`
and the source-backed/native or ABI-v9 executable control; do not infer
validity from a Rust artifact or a single failed v10 build.

## R0 completion criteria

- Compiler-emitted inventory completed for 26 production roots; 25 builds
  passed, and the exact `packages/time` source failure is retained as an
  unresolved inventory row. Every discovered generic export has explicit
  callable records and harness evidence.
- Existing source-free harnesses and the 14-provider advanced-data graph are
  mapped to actual selected callables; concrete exports are distinguished
  from open generic exports.
- The 3-mode oracle passes source-backed and v9 Debug/Release for six corpus
  workloads and the real SDK bytes consumer. V10 passes five supported shapes
  and classifies vector iteration plus bytes as unavailable. Existing hostile
  mutation corpus passes; source-free v10 projection and legacy poisoning are
  required for each passing row.
- R0 architecture proposal and critical path receive independent adversarial
  review at a frozen pushed SHA; all P0/P1 findings close before R1.
