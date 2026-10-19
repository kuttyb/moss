# Phase 25-C1: ABI-v10 Dependency Audit

**Baseline:** `ba3a289aaf52b4e5e3df3d1fb9e88254edc36636`
**Branch:** `phase25-integration`
**Scope:** Read-only audit of ABI-v10 production behavior; no compiler or runtime changes.

## Verdict

ABI-v10 does not reopen the provider's physical `.moss` file. It can compile a
source-free generic specialization from a sealed checked-body record for the
narrow H3 form exercised by Scenario 6. However, ABI-v10 still emits and imports
legacy generic statement records, then uses them to reconstruct generic bodies
when no checked-body record is attached. Therefore the general answer to
“Can a compiled-only ABI-v10 provider support required generic specialization
without its source or legacy generic statement reconstruction?” is **NO**.

The Scenario 6 test passes because its three generic functions fit the checked
H3 projection. It demonstrates a real production path, but not general checked
body coverage. A simple arithmetic generic also compiles and runs source-free,
but the `.mossi` artifact has one `generic_stmt` and no checked-body record for
that callable. That execution depends on legacy statement reconstruction.

## Provider-to-consumer path

1. **Provider checking and interface emission.** For staged v10,
   `write_module_interfaces()` constructs `abi_v10::Interface`, but first
   serializes the generic declarations accumulated in `contents[module]` as
   ordinary legacy line payloads. `abi_v10_declaration_records()` groups
   those declaration lines; `value.semantic_contract` receives them before
   the checked summary/body records are appended
   ([`src/moss.cpp:38734`](../../src/moss.cpp:38734),
   [`src/moss.cpp:39750`](../../src/moss.cpp:39750)). The generic emitter writes
   `generic_ir_version`, `generic_source`, `generic_param`,
   `generic_constraint`, and each `generic_stmt` from the checked provider AST
   ([`src/moss.cpp:39470`](../../src/moss.cpp:39470)). Private helper closures
   are emitted as `generic_dependency_begin` plus the same generic records
   ([`src/moss.cpp:39520`](../../src/moss.cpp:39520)). The v10 checked-record
   producers append additional summaries and bodies only for their supported
   subsets ([`src/moss.cpp:38861`](../../src/moss.cpp:38861),
   [`src/moss.cpp:38976`](../../src/moss.cpp:38976)). The interface is sealed
   and written as `.mossi` at [`src/moss.cpp:39775`](../../src/moss.cpp:39775).

2. **Provider native artifact.** Module Rust is generated from the checked
   provider program. The emitted interface digest is embedded as the ABI-v10
   marker, then `rustc --crate-type=rlib` compiles the module crate
   ([`src/moss.cpp:40165`](../../src/moss.cpp:40165),
   [`src/moss.cpp:40175`](../../src/moss.cpp:40175),
   [`src/moss.cpp:40230`](../../src/moss.cpp:40230)). The provider's ordinary
   concrete declarations and native symbols reside in this `.rlib`. A generic
   specialization selected only by a consumer is generated in the consumer
   crate; the provider `.rlib` is not regenerated from provider source.

3. **Consumer interface loading.** The selected compiled interface is loaded
   by `load_module_interface()` ([`src/moss.cpp:37513`](../../src/moss.cpp:37513)).
   ABI-v10 parsing verifies the interface digest, exact selected `.rlib`
   marker, canonical encoding, and record closure before projecting declaration
   records for the normal module loader
   ([`src/moss.cpp:35975`](../../src/moss.cpp:35975)). The loader then
   recognizes `generic_stmt`, `generic_source`, and the related generic lines
   and calls `parse_generic_ir_line()` ([`src/moss.cpp:37109`](../../src/moss.cpp:37109),
   [`src/moss.cpp:35640`](../../src/moss.cpp:35640)). `generic_stmt` rebuilds
   `Function::body`; it is not just diagnostic text.

4. **Checked-body override.** The loader separately attaches v10 checked
   summary/body payloads to matching callable IDs
   ([`src/moss.cpp:37265`](../../src/moss.cpp:37265)). Before inference, the
   checker calls `project_imported_h3_checked_bodies()`
   ([`src/moss.cpp:3272`](../../src/moss.cpp:3272)). When a checked body exists,
   that function validates the callable and body hashes, decodes the checked
   bytes, validates dependency identities, and replaces the body reconstructed
   from `generic_stmt` with a lowering view derived from the checked IR
   ([`src/moss.cpp:2966`](../../src/moss.cpp:2966),
   [`src/moss.cpp:3130`](../../src/moss.cpp:3130)). When no checked body exists,
   this override is skipped and the legacy body remains active.

5. **Specialization and consumer native compilation.**
   `register_static_specialization()` checks the selected `Function::body`
   under concrete parameter types and stores a consumer-side specialization
   ([`src/moss.cpp:18063`](../../src/moss.cpp:18063),
   [`src/moss.cpp:18205`](../../src/moss.cpp:18205)). Module projection retains
   required specializations and their private helper closure
   ([`src/moss.cpp:38107`](../../src/moss.cpp:38107)). The Rust generator emits
   specialization bodies from `Function::body` via `gen_stmts()`
   ([`src/moss.cpp:27064`](../../src/moss.cpp:27064),
   [`src/moss.cpp:27128`](../../src/moss.cpp:27128)). Finally, rustc builds
   consumer module crates and links the selected provider `.rlib`
   ([`src/moss.cpp:40235`](../../src/moss.cpp:40235),
   [`src/moss.cpp:40251`](../../src/moss.cpp:40251)). Consequently, the source
   of the specialized consumer Rust function is either the checked H3 lowering
   view or the legacy reconstructed function body.

   ABI-v10 declares typed `Specialization` records in
   [`src/abi_v10_records.hpp:996`](../../src/abi_v10_records.hpp:996), but the
   production emitter and loader do not write or read those records
   (`src/moss.cpp` has no specialization-record call site). The consumer
   recomputes specializations from the imported `Function::body` in
   `register_static_specialization()`. This keeps specialization ownership in
   the consumer checker, while making checked-body versus legacy-body
   reconstruction decisive for the input to that process.

## Legacy dependency classifications

“Active” below means the record is read in the staged-v10 path and is needed
for at least one currently supported generic declaration, specialization,
private-helper, or diagnostic behavior. The active count is the number of
generic-specific textual record kinds consumed by the importer, not the number
of source statements in an artifact.

| Legacy record/path | Classification | Current v10 use |
|---|---|---|
| `generic_stmt` | **ACTIVE** | Reconstructs `Function::body`; used for generic forms without a checked body. |
| `generic_param` | **ACTIVE** | Reconstructs generic parameter names and open type spellings. |
| `generic_constraint` | **ACTIVE** | Reconstructs callable constraints used during checking and Rust specialization. |
| `generic_result` | **ACTIVE** | Reconstructs a generic result expression when one is emitted. |
| `generic_source` | **ACTIVE** | Supplies provider filename metadata for related diagnostics; it does not open or read that file. |
| `generic_entity_id` | **ACTIVE** | Supplies the source line used for diagnostics; it does not establish the canonical callable identity. |
| `generic_dependency_begin` / `generic_dependency_end` | **ACTIVE** | Reconstructs private helper declarations and scopes their following generic records. |
| `open_parameters` and ordinary `param` declaration lines | **ACTIVE** | Provide the imported generic marker, parameter slots, and declared access used to construct the callable shell. |
| `generic_ir_version` | **UNREACHABLE** | The loader dispatches it to `parse_generic_ir_line()`, which has no matching case; the version value is ignored. |
| `generic_ir_end` and `semantic_ir_body_hash` | **UNREACHABLE** | Emitted as legacy markers but not consumed as checked-body identity or integrity by the v10 importer. |
| ABI-v9 v2 container and its native-ABI-9 checks | **COMPATIBILITY** | Used only when staged v10 is disabled; the loader rejects v9/v10 cross-mode artifacts. |

There are **8 ACTIVE generic-specific legacy record kinds** under this counting
rule: statement, parameter, constraint, result, source, entity ID, private
dependency begin, and private dependency end. The generic declaration shell
records (`open_parameters` and `param`) are listed separately. Of these, only
`generic_source` is a source filename; no code path opens the original provider
source after interface loading.

## Checked-body coverage and blockers

The checked-body encoder can encode a broad `checked_ir::Stmt` tree
([`src/checked_body_ir.hpp:422`](../../src/checked_body_ir.hpp:422)). The current
source-free decoder is explicitly H3-specific: it requires exactly two
statements, a leading `let`, and a terminal `return`; expression nodes are
limited to parameter/local reads, calls, and nominal constructors
([`src/checked_body_ir.hpp:443`](../../src/checked_body_ir.hpp:443),
[`src/checked_body_ir.hpp:535`](../../src/checked_body_ir.hpp:535),
[`src/checked_body_ir.hpp:599`](../../src/checked_body_ir.hpp:599)). The provider
bridge is narrower still: `checked_private_chain_body()` requires a two-
statement local-result form whose first expression is a simple call or nominal
construction from parameters ([`src/moss.cpp:2787`](../../src/moss.cpp:2787)).
Checked-record emission also restricts summaries to complete, pure forms with
no unresolved/member requirements ([`src/moss.cpp:38861`](../../src/moss.cpp:38861),
[`src/moss.cpp:38993`](../../src/moss.cpp:38993)).

This leaves two concrete implementation blockers to making checked records the
general v10 authority:

1. **Producer coverage:** A normal single-return arithmetic generic such as
   `export fn compute(x): return x * 2 + 20` is emitted with `generic_stmt` but
   receives no checked body record. The consumer therefore needs the legacy
   body reconstruction path.
2. **Importer coverage:** Even if such a body were encoded, the H3 decoder
   cannot reconstruct its one-statement return or arithmetic expression. It
   rejects those shapes rather than interpreting the general checked IR.

The targeted source-free reproduction for blocker 1 passed: the v10 provider
was built, its `.mossi` and `.rlib` were retained, its source tree was removed,
and the consumer printed `30`. Artifact inspection found one `generic_stmt`
and zero `checked-body-v1:` records for `compute`. The existing Scenario 6
also passed with output `99`; its artifact contained three checked-body records
and still contained legacy generic statement records. Its checked functions
match the H3 local-result shape, so `project_imported_h3_checked_bodies()`
replaced their initially reconstructed legacy bodies before specialization.

## Test evidence

- `python3 tests/phase25/integration/cross_crate_harness.py --scenario 6 --staged-v10`
  — **PASS**, “Source-removed generic provider with nested private helpers
  executes”; the scenario asserts exact output line `99`. The fixture physically
  removes the provider package tree before building/running the consumer
  ([`tests/phase25/integration/cross_crate_harness.py:239`](../../tests/phase25/integration/cross_crate_harness.py:239),
  [`tests/phase25/integration/cross_crate_harness.py:351`](../../tests/phase25/integration/cross_crate_harness.py:351)).
- Isolated single-return arithmetic provider/consumer with `MOSS_STAGED_ABI=v10`
  — provider build **PASS**; after provider source removal, consumer build and
  run **PASS**, output `30`. The inspected provider interface had
  `generic_stmt=1`, `generic_source=1`, and `checked-body-v1:=0`.
- No compiler or runtime source was changed; no broad regression suite was run.

## Conclusion

The staged Scenario 6 is the same compiler build, interface loader, checker,
specializer, Rust generator, and rustc pipeline used for staged production
consumers. It proves the narrow checked H3 source-free route works. It does
not prove that ABI-v10 generic bodies are generally independent of legacy
statement records. Physical source independence is **YES** for the tested
providers; independence from legacy generic statement reconstruction is **NO**
for the general supported generic surface. The checked-body coverage gaps above
remain before the v10 design's typed checked records can be considered the
authoritative source for all supported generic specializations.
