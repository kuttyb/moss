# Phase 25 Team G — G-A3 Checkpoint A independent Architect review

- Reviewer: independent L6 Architect. I did not implement the change.
- Subject: commit `2a7454b` ("G-A3 Checkpoint A: intern stable nominal declaration identities"), on branch `phase25-g-architect-ga3`.
- Baseline for regressions: parent `1690de5`. I built it separately from `git archive` at `tmp/base/moss`.
- Authorities: `TYPE_IR_CONTRACT.md` §§2–3; `ABI_V10_DESIGN.md` §§1–3, §5, §7–9; `TEAM_S_TEAM_G_INTERFACE_AGREEMENT.md`; and the L7 G-A3 completion gate (items 1–6).
- Method: I tried to falsify the claim. I treated the implementer's notes (`TEAM_G_UNRESOLVED_CONTRACT_DECISIONS.md` and `.codex/CURRENT_STATUS.md`) as claims, not facts. All evidence below comes from real Moss programs:
  - built with Margo;
  - compiled-only cross-package (provider `src/` deleted, resolved through `MOSS_MODULE_PATH`);
  - run under production ABI v9 and staged `MOSS_STAGED_ABI=v10`.
- Instrumented build: I made an uncommitted copy of the compiler under `tmp/instr/`. It logs every Type IR name resolution that falls back to `Primitive`, or that creates a nominal type with no active `TypeContext`. I used it to look for identity decisions made without a bound context.

## Verdict

**CONDITIONAL GO for Checkpoint A as an intermediate checkpoint. NO-GO for declaring G-A3 complete at `2a7454b`.**

The identity foundation is real:

- Declarations are interned.
- Template, enum, and domain IDs follow the module-identity scheme.
- Instance keys are independent of argument order and layout.
- Compiled-only consumers and Fast Debug resolve the same provider identity.
- Staged ABI v10 rejects impersonation, version skew, and forged `declaration_id` lines with the specified codes.

Not every gate item holds:

- Item 2 is **not met**.
- Item 5 is **partially met**.
- Item 6 has a concrete identity defect (P25-G-GA3-ARCH-001).
- Two name-resolution holes show that the "one spelling per declaration in a session" premise behind the item-2 deferral is not enforced (ARCH-002, ARCH-003).

Conditions for accepting Checkpoint A and proceeding to G-A1 Checkpoint B:

1. Fix **ARCH-001**: derive identity from the declaration's defining module, not by splitting the mangled spelling. Do this before or as the first commit of Checkpoint B, with a regression in `check_phase25_nominal_identity.py`.
2. Fix **ARCH-002** and **ARCH-003**: every cross-kind name clash (record, enum, domain, trait) must fail closed with a structured `DUPLICATE_SYMBOL` diagnostic, with regressions.
3. L7 must record explicitly that gate item 2 (ARCH-004) and the instance-key part of item 5 (ARCH-007) carry into G-A1 Checkpoints B/C and the ABI-v10 record work. G-A3 is not "complete" until the closing Architect review re-tests them.

## Existing suites at `2a7454b` (all pass)

```
python3 tests/tooling/check_phase25_type_ir.py                  -> "Phase 25 structured Type IR: PASS" (exit 0)
python3 tests/tooling/check_phase25_nominal_identity.py ./moss  -> cases A, B, C, E, D PASS (exit 0)
python3 tests/tooling/check_phase25_conformance.py ./moss       -> gates 1-9, 11 PASS; gate 10 PENDING (joint integration); exit 0
```

I also ran the latter two with `tmp/instr/moss`, which carries the trace patch only. They pass.

## Gate items

### 1. Source-backed and imported nominal types resolve to exact declaration IDs — **MET under staged v10. PARTIALLY MET under production v9. Defect in one naming case.**

Evidence for:

- `build_type_context` (`src/moss.cpp:232`) interns every record, enum, and domain after the duplicate checks (`src/moss.cpp:2130`).
- Imported providers take the interface identity: `interface_module_identity`, `src/moss.cpp:28514`, which uses `program.module_identities` (`src/moss.cpp:30318-30340`).
- `moss type` reports the bound identity for:
  - local records, enums, and domains;
  - compiled-only imported records, enums, and domains;
  - an imported type's own entity (`entity-v1:type:Util__Token`);
  - `moss inspect`.
- Examples I observed:
  - `nominal-template-v1:module-v1:alpha@1.0.0::Util:type:Token`
  - `nominal-enum-v1:module-v1:ea@1.0.0::EA:enum:Color`
  - `nominal-domain-v1:module-v1:svc@1.2.3::Service:domain:Worker`
- A mixed typed/inferred record from a compiled package that holds another package's type is resolved through to the inner provider. `Mid.Wrapper{inner: Provider.Token, tag}` has instance key `592a6f…` with Provider 0.1.0 and `eec74c…` with Provider 0.2.0.

Evidence against:

- ARCH-001: a `__`-containing declaration name gets the wrong package and module.
- ARCH-006: under production ABI v9 a consumer cannot know which provider identity a compiled dependency was built against. `moss check` succeeds, and `moss type` reports a confident but wrong `declaration_id`. Only rustc (E0460) stops the build.

### 2. All nominal comparisons use typed IDs and resolved type keys — **NOT MET**

Inside the Type IR, comparisons use typed IDs:

- `same_head` (`src/type_ir.hpp:259`), `structurally_equal`, `TypeSolver::unify`, `stable_type_key`, and `nominal_instance_key`.
- The checker's `same_type`/`is_type_compatible` and the native specialization key go through them.

The implementer's stated limitation is accurate, and it means item 2 is not met for G-A3 as L7 wrote it. Decisions still made on spellings include:

- the return-annotation check, `canonical_type_name(*f.return_type) != canonical_type_name(*actual)` (`src/moss.cpp:3973`);
- the recursion-edge comparison (`src/moss.cpp:9734`);
- same-domain message detection (`src/moss.cpp:14159`);
- `valid_type`'s `objects_/enums_/domains_.count(type)` membership;
- trait conformance via `objects_.find(type)`;
- Fast Debug's specialization selection, `candidate.parameter_types == edge.argument_types` (`src/interpreter.hpp:600`). This is string-vector equality, while native lowering uses `specialization-v2` keys.

These spelling checks can disagree with the Type IR. Reproducer (also fails on baseline):

```moss
type Box:
  value
fn make() -> Box:
  return Box(value: 1)
fn main():
  b = make()
  echo 1
```

This gives `MOSS_COMPILE_ERROR function 'make' returns 'Box[value=int]' but is annotated 'Box'`, yet `is_type_compatible` admits it under the bare-template rule (`src/moss.cpp:2779-2781`).

The deferral relies on the claim that spellings are unique per declaration within a session. That claim is only conditionally true:

- A clash of the mangled spelling across modules is rejected (`DUPLICATE_SYMBOL duplicate object type: A__B__C`).
- A record/domain name clash reaches the `TypeContext` as an internal error (ARCH-002).
- A trait and a record sharing a name are accepted (ARCH-003).

I found no case in a single session where two *declarations* are silently conflated by a spelling comparison.

### 3. Layout-identical but declaration-distinct types remain different — **MET**

These all report `TYPE_MISMATCH`:

- case A (`b = c`, Box vs Cell);
- `items.push(Cell(...))` into `[Box(...)]`;
- `var b = pick(Box(..)); b = pick(Cell(..))`;
- enums with identical cases (`Color` vs `Shade`);
- cross-package `EA.code(EB.red())` (enum) and `Alpha.read(Beta.make())` (record).

Other evidence:

- Generic calls produce separate `specialization:pick<Box>` and `specialization:pick<Cell>` specializations, and the native build prints `1 2 1`.
- Instance keys differ for `Box[value=Box[value=int]]` vs `Box[value=Cell[value=int]]`.
- Under `margo debug` with path dependencies, `Alpha.read(Beta.make())` is a `TYPE_MISMATCH`.

### 4. Independent consumers resolve the same provider identity — **MET (staged v10 and non-adversarial v9)**

- Case C/E passes.
- Fast Debug (`MOSS_FAST_DEBUG_SOURCE_ROOTS`, path dependencies) and the native build report the identical `declaration_id` and instance key (`4e6b9c…`) for `Alpha.Token`.
- `Mid.Wrapper` has the same instance key (`592a6f…`) in Mid's own source session and in a compiled-only consumer session, whether or not the consumer imports `Provider` directly.
- A version bump of a provider changes all identities. After an in-place `Moss.toml` bump, a rebuild emits the new `module_id`/`declaration_id` and does not reuse stale artifacts.

### 5. ABI-v10 can serialize and validate these identities — **PARTIALLY MET**

What works:

- Every exported record, enum, and domain carries `declaration_id` (`src/moss.cpp:31734ff`), sealed inside the v10 semantic contract.
- The loader checks it against the sealed `module_id` (`src/moss.cpp:29110`).
- A missing `declaration_id` is rejected only under v10 (`src/moss.cpp:29943-29947`).
- Providers claiming one identity with different seals, and dependency-hash mismatches, are rejected (`src/moss.cpp:30175-30191`, `30341-30362`).

Forged-but-resealed v10 artifacts — I re-encoded each with the real `abi_v10::encode` and patched the rlib marker:

| Forgery | Result |
|---|---|
| wrong declaration name | `SOURCE_FREE_TYPE_IDENTITY_MISMATCH` |
| wrong package | `SOURCE_FREE_TYPE_IDENTITY_MISMATCH` |
| enum ID re-labelled as a record template | `SOURCE_FREE_TYPE_IDENTITY_MISMATCH` |
| `declaration_id` lines removed | `SOURCE_FREE_TYPE_IDENTITY_MISMATCH … has no declaration identity` |
| edited without resealing | `SOURCE_FREE_CONTRACT_INVALID` |

Staged v10 conflicts (all `SOURCE_FREE_MODULE_ID_CONFLICT`):

- a consumer built against `alpha::Util` given `beta::Util`, including when `Util` is reachable only transitively;
- provider 0.1.0 replaced by 0.2.0, including an in-place swap after a cached build;
- a local source module shadowing the compiled provider a dependency was built against.

What is missing (ARCH-007):

- No `NominalTemplate`/`ConcreteInstance` records.
- No serialized instance or type keys. `grep -c 'nominal-instance\|instance_key\|type-key-v1\|template_id'` is 0 on every emitted v10 `.mossi`.
- Type references are still source-only spellings (`public_representation field inner Provider__Token`, `return=Mid__Wrapper[tag=int]`). They are bound only indirectly, through the import record's `module_id` and hashes.
- `ABI_V10_DESIGN` §3.2 and §5 step 6 ("recomputed nominal instance … keys") are therefore not implemented.

### 6. Canonical IDs, and no paths, lines, layout, unqualified names, Rust symbols, or ABI hashes as identity — **PARTIALLY MET**

What holds:

- Template IDs match `nominal-template-v1:<module-v1:pkg@ver::Module>:type:<Name>` exactly.
- No path, line, layout, or Rust symbol enters the ID.
- Instance keys are domain-separated SHA-256 over a length-prefixed encoding of the template ID plus ordered resolved fields, with recursive back-references.
- Constructor argument order does not change identity: `Pair(key:,value:)` and `Pair(value:,key:)` give the same key `c203a8…`, and `Box[value=Box]` vs `Box[value=Cell]` give different keys.

Defect: the module and declaration name are recovered by splitting the program spelling at the first `__` (ARCH-001). For some declarations, identity is therefore a function of a mangled name, not of the declaration.

Contract gap: the enum and domain ID schemes (`nominal-enum-v1:…:enum:`, `nominal-domain-v1:…:domain:`) are not in the frozen contract (ARCH-008).

## Findings

### P25-G-GA3-ARCH-001 — Identity is derived from the mangled spelling; `__` names get a wrong, cross-package identity (blocking for G-A3 completion)

`build_type_context` computes `module = module_name_from_symbol(program_name)` (`src/moss.cpp:214-217`, first `"__"`). It then looks that module up in `program.module_identities`. A miss silently falls back to `__standalone__@0.0.0::<prefix>` (`src/moss.cpp:222-229`). A root-level declaration named `Foo__Bar` in a project therefore gets another module's identity.

Reproducer: `tmp/ga3/nm` — `Moss.toml` with `name = "nmproj"`, `version = "4.5.6"`, no `module` line.

```moss
type Foo__Bar:
  value: Int
type Plain:
  value: Int
fn main():
  b = Foo__Bar(value: 1)
  p = Plain(value: 2)
  echo 1
```

`moss type b --source src/main.moss --kind binding --enclosing main --json` reports:

- `b`: `declaration_id = nominal-template-v1:module-v1:__standalone__@0.0.0::Foo:type:Bar` and `instance_key = …367ab436…`.
- `p`: `nominal-template-v1:module-v1:nmproj@4.5.6::nmproj:type:Plain`, which is correct.

A standalone file declaring `Foo__Bar` yields the *same* ID and instance key `367ab436…`. Two different packages' declarations therefore share one identity, and the package, module, and declaration name are all wrong.

To close: carry the defining module on each `ObjectType`/`EnumType`/`Domain` from the parser or merge step, set by the module of its file, and use it in `build_type_context` and `exported_declaration_identity` (`src/moss.cpp:31516`). Alternatively, reject `__` in user identifiers with a structured diagnostic. In either case, never fall back to `__standalone__` inside a project; fail closed instead. Add a regression.

### P25-G-GA3-ARCH-002 — A record and a domain with the same name reach the `TypeContext` as an internal error (non-blocking; fail-closed but unstructured)

The checker's duplicate pass checks objects against objects and domains against domains. Only enums are checked across kinds (`src/moss.cpp:2114-2127`). A `type X` and a `domain X` therefore reach `TypeContext::declare`, which throws `std::invalid_argument` (`src/type_ir.hpp:114-123`).

Reproducer (`tmp/ga3/dupdom.moss`):

```moss
type X:
  value: Int
domain X:
  count: Int = 0
  fn Get() -> Int:
    reply count
fn main():
  echo 1
```

- At `2a7454b`: `MOSS_INTERNAL_OR_IO_ERROR conflicting nominal identity for 'X'`, at line 0.
- At baseline `1690de5`: `ok: true`, accepted silently.

The new code is strictly safer, but the diagnostic is wrong.

To close: add the cross-kind duplicate check in the checker (`DUPLICATE_SYMBOL` with source position), and add a negative case.

### P25-G-GA3-ARCH-003 — A trait and a record may share one spelling; it resolves differently in different places (blocking for G-A3 completion; pre-existing)

There is no trait-vs-type duplicate check (`src/moss.cpp:2132-2135`). `TypeContext::find("Circle")` binds the record, while the checker treats the annotation `shape: Circle` as the trait.

Reproducer (`tmp/ga3/traitclash.moss`):

```moss
trait Circle:
  fn area() -> Int
type Circle:
  radius: Int
  fn area():
    return radius * radius
type Square:
  side: Int
  fn area():
    return side * side
fn print_area(shape: Circle):
  echo shape.area()
fn main():
  print_area(Circle(radius: 3))
  print_area(Square(side: 2))
```

- `moss check`: `ok: true`.
- `moss run --interp`: prints `9`, `4`.
- Native `margo run` / `moss build --json`: fails with non-JSON `moss: error: missing static specialization for function 'print_area' with argument types (Circle)`.

Baseline behaves the same. This directly falsifies the premise ("spellings are unique per declaration within a session") used to defer item 2.

To close: reject a trait/type (and trait/enum/domain) name clash with `DUPLICATE_SYMBOL`. Alternatively, intern traits in the same declaration table so name resolution is unique.

### P25-G-GA3-ARCH-004 — Gate item 2 is not met: nominal decisions remain spelling-based outside the Type IR (blocking for G-A3 completion; scheduled into G-A1 B/C)

See gate item 2: `src/moss.cpp:3973`, `9734`, `14159`, `valid_type` membership, trait conformance lookup, and `src/interpreter.hpp:600`. In Fast Debug, the last one selects specializations by spelling while native lowering uses typed keys. The bare-template return-annotation reproducer shows a spelling decision that contradicts the Type IR.

To close: in G-A1 B/C, route these decisions through `TypeRef` with `same_head`, `structurally_equal`, and stable keys. That includes making Fast Debug specialization selection use the same `specialization-v2` key as native (`edge_requires_specialization`). The closing review should grep for `canonical_type_name(..) [!=]= canonical_type_name(..)` and string-vector type equality.

### P25-G-GA3-ARCH-005 — "An unbound nominal is never closed" holds only for bracketed spellings (non-blocking; latent)

In `Type::parse_canonical`, any bare head that is not found in the active context becomes `Type::primitive(head)` (`src/type_ir.hpp:541`). With no context at all, every bare record, enum, or domain spelling becomes `Primitive`. Such a type is closed, has a `type-key-v1` built from `("primitive", name)`, and compares by spelling.

`same_head` also compares two unbound nominals by name (`src/type_ir.hpp:264`). The new unit test checks unboundness only for `Box[value=int]`, not for bare `Pt`/`Box`.

The instrumented run of the nominal identity and G2 conformance suites and my cross-package probes found:

- no in-context fallback for a declared nominal name;
- no identity decision outside a bound context — the 95 context-free fallbacks were all parser-time `canonical_type_name` normalization;
- many internal placeholder and expression strings (for example `_specialized_result:get_value`, `_method_value`, `CA__token`, `x.required`) parsed as closed `Primitive` types.

To close: restrict `Primitive` to the primitive ID set. Make an unknown bare head an unbound/unresolved nominal (open, no key), or a parse failure. Add a unit test for bare spellings with no context.

### P25-G-GA3-ARCH-006 — Production ABI v9: the semantic API reports confident but wrong identities across compiled-provider edges (non-blocking while v10 is staged; must close at cutover)

v9 interfaces now carry `package_version` and `declaration_id`, but v9 `imports` list only module names. When a consumer combines a compiled dependency with a different provider of the same module, nothing detects it at the Moss level:

- `CA` built against `alpha::Util` given `beta::Util`;
- `CB` built against `prov@0.1.0` given `prov@0.2.0`;
- a local source module `Provider` shadowing the one `CB` was built against.

`moss check` returns `ok`. `moss type t` reports, for example, `module-v1:shadowapp@0.1.0::Provider:type:Token` for a value the compiled `CB` produces as `prov@0.1.0`'s `Token`. The build fails only in rustc (`E0460 found possibly newer version of crate moss_Provider`). Staged v10 rejects all of these with `SOURCE_FREE_MODULE_ID_CONFLICT`.

To close: either make v10 the production ABI as planned, or record imported module identities (and digests) in v9 `imports` and apply the same check. Until then, the semantic API should not present v9 cross-provider identities as verified.

### P25-G-GA3-ARCH-007 — ABI v10 does not yet serialize or validate instance keys or typed references (non-blocking for Checkpoint A; blocking for ABI-v10 completion)

Emitted v10 records contain no `NominalTemplate{template_id, field_slots, variable_count, representation_hash}`, no `ConcreteInstance{instance_key, …}`, and no `TypeExpr` using `template_id`. Field, parameter, and return types are module-spelled (`Provider__Token`).

The consumer therefore cannot perform `ABI_V10_DESIGN` §5 step 6 (recompute and compare instance keys), and §3.2's "consumer rejects an instance whose recomputed key … differs" is not implemented. `.codex/CURRENT_STATUS.md` acknowledges this ("typed TypeExpr records … remain").

To close: emit the §3.2 records and validate recomputed keys on load, with a forged-key negative test.

### P25-G-GA3-ARCH-008 — Enum and domain identity formats are not in the frozen contract (non-blocking)

The implementation uses `nominal-enum-v1:<ModuleIdentity>:enum:<Name>` and `nominal-domain-v1:<ModuleIdentity>:domain:<Name>`. `TYPE_IR_CONTRACT` §2–3.1 defines only `NominalTemplateId`, and `ABI_V10_DESIGN` §3.2 names `Enum(enum_id)`/`Domain(domain_id)` without a format.

To close: ratify the formats (L7, with Team S concurrence per the interface agreement) and add them to `TYPE_IR_CONTRACT` §3.1 and `ABI_V10_DESIGN` §3.2.

### P25-G-GA3-ARCH-009 — A provider's claimed `<package>@<version>` is not bound to the consumer's resolution (non-blocking observation)

A provider re-encoded with a consistent forged identity (`alpha@9.9.9` in `module_id`, `package_version`, and `declaration_id`, resealed) is accepted under both v9 and v10. `t` then reports `alpha@9.9.9`. The contract requires only that conflicting providers in one graph be rejected, which works. Nothing ties the claimed identity to `Moss.toml`/`Moss.lock`.

To close (optional): have Margo pass the resolved package name and version per provider directory, and have the loader compare them.

### P25-G-GA3-ARCH-010 — `TypeContext` lifetime and scoping hazards (non-blocking)

- `NominalDeclaration::owner` is a raw `const TypeContext*` (`src/type_ir.hpp:83`), dereferenced by `resolved_fields` (`src/type_ir.hpp:562`). A `TypeRef` that outlives every `Program` copy holding the `shared_ptr<TypeContext>` would dangle. I found no such path today.
- The thread-local context stack (`src/type_ir.hpp:167`) assumes LIFO destruction. The copyable `Binding` (`src/type_ir.hpp:154`) breaks LIFO when copies of generators for *different* programs interleave. Today all projected module programs share one context pointer, so this is harmless.
- Conflicts surface as `std::invalid_argument` (see ARCH-002).

To close: hold the owner as `std::weak_ptr`/`shared_ptr`, or pass the context explicitly. Make `Scope`/`Binding` check that they pop their own entry.

### Out-of-scope observations (pre-existing at `1690de5`; not G-A3 defects)

- A consumer that imports only `Mid` fails in rustc (`E0425 cannot find … Provider__read`) when `Mid`'s specialized generic calls a transitive provider. With `import Provider` added it builds and prints `5`. This is A1 linkage.
- In a non-modular project, `p.value` on a same-file record fails with `FIELD_VISIBILITY_ERROR … private to its defining module`.

## Coverage: what I tested that passed

Scratch harnesses are under `tmp/ga3/`, which is disposable: `lib.py`, `cross.py`, `forge.py`, `forge10.cpp`, `shadow.py`, `swap.py`, `bump.py`, `dom.py`, `fastdebug.py`, and `midsrc.py`.

Local and single-session behaviour:

- Reordered constructor arguments give the same instance key.
- Nested instances are distinct.
- Same-layout records are distinct in assignment, vector push, and generic results.
- Enums with identical cases are distinct.
- Mixed typed/inferred records are now valid instance types. The baseline rejected `Mid__Wrapper[tag=int]` with `UNKNOWN_SYMBOL_OR_TYPE`.

Cross-package, compiled-only (provider `src/` removed), v9 and v10:

- same module name in two packages → `MODULE_IMPORT_AMBIGUOUS` when both are present;
- correct provider → runs (`1`);
- identities and instance keys reported for imported records, enums, and domains;
- exported domain with a raised enum runs (`7`) and reports `nominal-domain-v1:module-v1:svc@1.2.3::Service:domain:Worker`;
- cross-package enum mismatch → `TYPE_MISMATCH`.

Staged v10 rejects with `SOURCE_FREE_MODULE_ID_CONFLICT`:

- wrong provider, both directly imported and transitive-only;
- version skew;
- an in-place provider swap after a cached build;
- local source shadowing.

Byte-identical duplicate providers dedup (case D).

Forgery (v9 with the FNV section hashes and SHA-256 digest recomputed; v10 resealed with `abi_v10::encode`; rlib marker patched):

- every inconsistent `declaration_id` → `SOURCE_FREE_TYPE_IDENTITY_MISMATCH`;
- v9 tolerates a missing `declaration_id` (legacy), v10 rejects it.

Fast Debug and native: `margo debug` with two path dependencies runs (`1`, `2`) and rejects the cross-package mismatch. The `moss type` identity under `MOSS_FAST_DEBUG_SOURCE_ROOTS` equals the native compiled-only identity.

Version bump: an in-place `Moss.toml` bump re-emits `module_id`/`package_version`/`declaration_id` (v9 and v10), and the source identity follows.

Context binding (instrumented): no `Type::nominal` was created without a context, and no declared nominal fell back to `Primitive` inside a context, across the nominal-identity suite, G2 conformance, and all cross-package probes.
