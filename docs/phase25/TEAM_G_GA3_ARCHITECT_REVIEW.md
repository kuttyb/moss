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

## Addendum 1 — closure check at 235072d

- Subject: `235072d` ("Close Checkpoint A review conditions ARCH-001/002/003 and harden ARCH-010"). Its history is `2a7454b → c787dc3` (this review, cherry-picked) `→ 235072d`.
- I checked it out detached in the review worktree, then ran `make` and `./moss agent bootstrap --json` (`language_version` is `moss-0.1`).
- For comparison I also built `2a7454b` from `git archive` (`tmp/prev/moss`) and kept `1690de5` (`tmp/base/moss`).
- I did not implement any of these fixes.

### Verdict

**CONDITIONAL GO for Checkpoint A. The only remaining condition is the L7 action (original condition 3).**

- Conditions 1 and 2 are met: ARCH-001, ARCH-002, and ARCH-003 are closed with evidence below. ARCH-010 is also closed.
- Original condition 3 is not an implementation change and remains open: L7 must record that gate item 2 (ARCH-004) and the instance-key part of item 5 (ARCH-007) carry into G-A1 Checkpoints B/C and the ABI-v10 record work. Once L7 records it, Checkpoint A is GO.
- G-A3 as a whole is still **not complete**: ARCH-004, -005, -006, -007, -008, and -009 remain open as scheduled.
- New finding ARCH-011 is a small **regression introduced by 235072d**. It is non-blocking for the checkpoint but must close before G-A3 completion.

### Suites at 235072d (all pass)

```
python3 tests/tooling/check_phase25_type_ir.py                  -> PASS (exit 0)
python3 tests/tooling/check_phase25_nominal_identity.py ./moss  -> cases A, B, C, E, D, F PASS (exit 0)
python3 tests/tooling/check_phase25_conformance.py ./moss       -> gates 1-9, 11 PASS; gate 10 PENDING (joint); exit 0
python3 tests/tooling/check_phase21_standard_error_namespace.py ./moss -> PASS (affected by the std-error declaration change)
```

### Per-finding status

| Finding | Status at 235072d | Evidence |
|---|---|---|
| ARCH-001 `__` identity | **Closed** | See below |
| ARCH-002 type/domain clash | **Closed** | See below |
| ARCH-003 trait/type clash | **Closed** | See below |
| ARCH-010 context lifetime and scoping | **Closed** (residual note) | See below |
| ARCH-004 item 2 spelling comparisons | Open (G-A1 B/C) | Unchanged by this commit |
| ARCH-005 bare unknown spelling is a closed `Primitive` | Open | `Type::from_canonical_string("Ghost")` → `kind=Primitive`, `is_closed()=1` (`tmp/ga3/ctxprobe.cpp`) |
| ARCH-006 v9 cross-provider identities | Open (v10 cutover) | Shadow probe: v9 `check` OK with a wrong id; v10 `SOURCE_FREE_MODULE_ID_CONFLICT` |
| ARCH-007 v10 instance keys and typed references | Open (ABI v10) | Unchanged |
| ARCH-008 enum/domain ID formats | Open (L7/Team S ratification) | Unchanged |
| ARCH-009 claimed package/version not bound to the lock | Open (observation) | Forgery F5 still accepted under v9 and v10 |

**ARCH-001 — closed.**

What changed:
- `rewrite_module_program` sets `defining_module` and `declaration_name` on records, enums, and domains (`src/moss.cpp:28160-28197`). The standard errors take `std`.
- `build_type_context` uses only those fields.
- `exported_declaration_identity` fails with `MOSS_INTERFACE_ERROR` instead of re-deriving.
- In projects, `program_module_identity` fails closed with `MODULE_IDENTITY_UNRESOLVED`.

Evidence:
- Root-level `Foo__Bar` in project `nmproj@4.5.6` → `nominal-template-v1:module-v1:nmproj@4.5.6::nmproj:type:Foo__Bar`.
- The same declaration in a standalone file → `…__standalone__@0.0.0::main:type:Foo__Bar`, with a different instance key (`128310…` vs `3a6821…`). The two packages' declarations no longer share an identity.
- Module-level `__` names in module `Geo` of `geo@2.0.0` (`tmp/ga3/dunder2.py`) give `…geo@2.0.0::Geo:type:Foo__Bar`, `nominal-enum-v1:…::Geo:enum:Mode__X`, and `nominal-domain-v1:…::Geo:domain:Svc__Y`. These are identical:
  - in the provider's source session;
  - in the emitted v9 and v10 `declaration_id` lines;
  - in a compiled-only consumer (provider `src/` removed, v9 and v10; the run prints `7`);
  - under `margo debug` with a path dependency.
- A mixed project (explicit `module P` plus an unmodular root file) gets `mixed@3.0.0::P:type:Box`, `mixed@3.0.0::mixed:type:Local`, and `…::mixed:enum:Color`, and runs.
- I could not reach `MODULE_IDENTITY_UNRESOLVED` from source. Every project shape I tried had a resolved identity.

**ARCH-002 and ARCH-003 — closed.** Every cross-kind clash now gives a positioned `DUPLICATE_SYMBOL` (`tmp/ga3/clash/*.moss`):

| Probe | Line | `1690de5` |
|---|---|---|
| type/domain (`dupdom`) | 1 | accepted |
| domain-then-type | 5 | accepted |
| enum/type | 4 | `MOSS_COMPILE_ERROR` |
| enum/domain | 1 | `MOSS_COMPILE_ERROR` |
| trait/type (`traitclash`) | 1 | accepted; native build died with non-JSON "missing static specialization" |
| trait/enum | 1 | accepted |
| trait/domain | 1 | accepted |
| `type Iterator` | 1 | accepted |
| `enum Iterator` | 1 | accepted |
| `domain Iterator` | 1 | accepted |

A user `trait Iterator` keeps its existing `MOSS_COMPILE_ERROR` reserved-name diagnostic. A user `type ParseError` gives an unpositioned diagnostic; see ARCH-012.

**ARCH-010 — closed, with a residual note.** `NominalDeclaration::owner` is a `weak_ptr`, and `Scope`/`Binding` release their own entry. A direct unit probe (`tmp/ga3/ctxprobe.cpp`, compiled against `src/type_ir.hpp`) shows:
- Releasing A's `Scope` before B's `Binding` leaves B current; releasing both leaves no context.
- `stable_type_key()` on a `TypeRef` whose session was dropped throws `invalid_argument "… outlived its compilation session"` instead of dereferencing a dangling pointer.

Residual note: a stack-allocated `TypeContext` (no `shared_ptr` owner) declares successfully, but every key computation then fails with the misleading "outlived" message. `declare` could reject a context not owned by a `shared_ptr`. The repository has no regression test for release order or an outlived session; consider adopting the probe.

**Field-privacy change — no weakening found.** The change is to use the declaration's own module, and to treat an unmodular program as one module. Probe `tmp/ga3/priv.py`, run with 235072d and with `1690de5`, gives identical results for every explicit-module case:
- Frozen fixture `private_field_visibility_negative.moss` with `provider_module.moss` in a project → `FIELD_VISIBILITY_ERROR` (both). The positive fixture → OK.
- Explicit `module App` reading `provider_module.Record.secret_field` → `FIELD_VISIBILITY_ERROR` (both).
- A same-module read from another file of `module P` → OK (both).
- Exported types: fields are exported by default (parser `field_exported = exported`, `src/moss.cpp:1364`), so `secret_field` of an `export type` is readable from another module and compiled-only in v9/v10. This is pre-existing language behaviour, identical at `1690de5`, and not a weakening.
- The only behavioural change: a two-file unmodular project reading a same-program record's field is now OK; it was `FIELD_VISIBILITY_ERROR` at `1690de5`. This is intended.

Coverage gap: see ARCH-013.

Regression re-runs, all unchanged from `2a7454b`:
- `tmp/ga3/cross.py v10` and `v9`: output byte-identical to the `2a7454b` run.
- Forgery F0–F5 for v9 and v10.
- Fast Debug path dependencies: identity and instance key `4e6b9c…` agree with native.
- Domain and enum providers.
- v10 shadow rejection.

### New findings

#### P25-G-GA3-ARCH-011 — Legacy-declared standard errors lost their `moss-std` identity (regression in 235072d; non-blocking for Checkpoint A, must close before G-A3 completion)

A program may still declare the legacy `enum ParseError: Invalid, Overflow`. Phase 21 migrates it to the canonical compiler type `std.ParseError`, and the checker then skips the synthesized `std` declaration (`present`, `src/moss.cpp:2095-2099`). The user declaration never passes through `rewrite_module_program`, so it carries no `defining_module`/`declaration_name`. Its identity therefore becomes package-local, with the mangled name as the declaration name.

Reproducer (`tmp/ga3/legacy_std.moss`):

```moss
enum ParseError:
  Invalid
  Overflow

fn f(e: ParseError) -> Int:
  return 1

fn main():
  echo f(ParseError.Invalid)
```

Command: `moss type entity-v1:enum:std__ParseError --source <file> --json`

| Program | `2a7454b` | `235072d` |
|---|---|---|
| standalone | `nominal-enum-v1:module-v1:moss-std@0.1.0::std:enum:ParseError` | `nominal-enum-v1:module-v1:__standalone__@0.0.0::main:enum:std__ParseError` |
| same file in an unmodular project `lsp@1.0.0` | `moss-std` identity | `…lsp@1.0.0::lsp:enum:std__ParseError` |

Scope of the regression:
- Programs written with `import std` / `std.ParseError` keep the `moss-std` identity.
- Mixed projects (an explicit module plus an unmodular root with the legacy declaration) keep it as well, and `recover ParseError.Invalid` against `P.bad()` runs (`1`, `6`) under both binaries.
- So the regression is limited to standalone programs and purely unmodular projects. There it changes reported identities and every type key that includes a standard error.

To close: when the legacy alias path migrates a declaration to `std__<Name>`, set `defining_module = "std"` and `declaration_name = <Name>`. Alternatively, drop the user declaration in favour of the synthesized one, as the project path does. Add a `case F` assertion that the legacy and canonical spellings give the same `declaration_id`.

#### P25-G-GA3-ARCH-012 — A user record named like a standard error reports an unpositioned, misleading duplicate (non-blocking; pre-existing, now coded `DUPLICATE_SYMBOL`)

`type ParseError:\n  v: Int\nfn main():\n  echo 1` gives `DUPLICATE_SYMBOL duplicate enum type: std__ParseError` at line 0, naming an enum the user did not write. `1690de5` gives the same text as `MOSS_COMPILE_ERROR`. This contradicts the claim that every clash gives a positioned diagnostic.

To close: position the diagnostic at the user's declaration, and name the reserved standard error.

#### P25-G-GA3-ARCH-013 — Field-privacy enforcement has no automated test (non-blocking)

`grep -rln "private_field_visibility\|FIELD_VISIBILITY_ERROR" tests/ Makefile` finds only the forward fixture and its README. No runner executes the fixture or asserts `FIELD_VISIBILITY_ERROR`, even though 235072d changed the privacy decision.

To close: add the fixture pair, plus the explicit-module negative and same-module multi-file positive from `tmp/ga3/priv.py`, to a checked runner.

## Addendum 2 — verification at 27fc2fb

- Subject: `27fc2fb` ("Close closure-check findings ARCH-011/012/013"). Its history is `235072d → c517127` (Addendum 1, cherry-picked) `→ 27fc2fb`.
- I checked it out detached in the review worktree and ran `make`.
- For comparison I used the `2a7454b` binary (`tmp/prev/moss`). I did not implement these fixes.

### Verdict

**CONDITIONAL GO for Checkpoint A. The only open condition is the L7 record (original condition 3).**

- ARCH-011, ARCH-012, and ARCH-013 are closed.
- The new finding ARCH-014 is non-blocking.
- L7 must still record that gate item 2 (ARCH-004) and the ABI-v10 instance-key part of item 5 (ARCH-007) carry into G-A1 Checkpoints B/C and the ABI-v10 record work. Once L7 records it, Checkpoint A is GO.
- G-A3 as a whole remains incomplete until ARCH-004 and ARCH-007 close.
- ARCH-005, -006, -008, and -009 remain open as scheduled. No commit since Addendum 1 changed them.

### Suites at 27fc2fb (all exit 0)

```
python3 tests/tooling/check_phase25_nominal_identity.py ./moss       -> cases A-F PASS
python3 tests/tooling/check_phase25_conformance.py ./moss            -> 10/11, gate 10 PENDING (joint)
python3 tests/tooling/check_phase25_field_visibility.py ./moss       -> PASS
python3 tests/tooling/check_phase25_type_ir.py                       -> PASS
python3 tests/tooling/check_phase21_standard_error_namespace.py ./moss -> PASS
```

### Per-finding status

**ARCH-011 — closed.** The probe (`tmp/ga3/std2.sh`) declares all three legacy standard errors: `enum FileError` (6 tags), `enum ParseError`, and `enum ConversionError`. `moss type entity-v1:enum:std__<Name>` reports `nominal-enum-v1:module-v1:moss-std@0.1.0::std:enum:<Name>` for all three in each of these shapes:
- a standalone file;
- a purely unmodular project;
- a mixed project (explicit `module P` plus an unmodular root with the legacy declarations).

This matches the `2a7454b` identities. The closed variant contract still holds. Each of these gives `BUILTIN_ERROR_TYPE_SHAPE ParseError must declare exactly these tag-only variants: Invalid, Overflow` at line 1, the same as at `2a7454b`:
- a payload variant (`Invalid(code: Int)`);
- a missing variant;
- an extra variant.

**ARCH-012 — closed.** A user declaration named like a standard error now gives a positioned `DUPLICATE_SYMBOL` that names the standard type:

| Probe | Result | `2a7454b` |
|---|---|---|
| `type ParseError` | `'ParseError' names the compiler standard error std.ParseError; choose another name` at the user's line 3 | `MOSS_COMPILE_ERROR` at line 0 |
| `domain FileError` | same form, line 3 | `MOSS_COMPILE_ERROR` at line 0 |
| `clash/std_name_type.moss` | line 1 | — |
| `enum ConversionError: A` | `BUILTIN_ERROR_TYPE_SHAPE` at line 3 (legacy contract, unchanged) | same |
| `trait ParseError` | `DUPLICATE_SYMBOL` at line 3 (see ARCH-014) | accepted |

Module-local namesakes are not regressed. In an explicit-module project, `module P` exports `enum ParseError: Invalid(code: Int)` and raises it. The consumer's arms run in this order:

```
recover std.ParseError.Invalid
recover P.ParseError.Invalid(code)
```

- `margo run` prints `9`, then `1` for `parse_int("x")`. The std arm never selects the provider error.
- Identities are `…expl@1.0.0::P:enum:ParseError` and `…moss-std@0.1.0::std:enum:ParseError`.
- `2a7454b` accepts the same program.

Pre-existing and out of scope, identical at `2a7454b`: a module-local *record* or *domain* named `ParseError`/`ConversionError` cannot be constructed. The constructor is rewritten to `std__…`, giving `UNKNOWN_SYMBOL_OR_TYPE`.

**ARCH-013 — closed, with a coverage note.** `check_phase25_field_visibility.py` (wired into `run.sh`) builds and runs real projects. It asserts:
- `FIELD_VISIBILITY_ERROR` at the exact line for an explicit `module App` reading a private field of a non-exported `Model.Record`;
- that a second file of `module Model` may use the field, with native and `run --interp` agreeing;
- that an unmodular two-file project may use its own record's private field.

Using a non-exported record is correct: the parser exports every field of an `export type` by default (`field_exported = exported`).

Coverage note: the test does not exercise the frozen fixture's shape, an *unmodular* consumer reading an explicit module's private field (`tests/phase25/g0/conformance/private_field_visibility_negative.moss` plus `provider_module.moss`). That path goes through a different branch of the privacy decision. My probe (`tmp/ga3/priv.py`) shows it still reports `FIELD_VISIBILITY_ERROR` at 27fc2fb. Adding that pair to the test is recommended, not required.

The `priv.py` results otherwise match Addendum 1 and `1690de5`. All earlier clash reproducers (`clash/*.moss`, `dupdom.moss`, `traitclash.moss`) still give positioned `DUPLICATE_SYMBOL`.

### New finding

#### P25-G-GA3-ARCH-014 — A trait named like a standard error reports the mangled spelling (non-blocking; message quality)

`trait ParseError:\n  fn f() -> Int` gives `DUPLICATE_SYMBOL trait 'std__ParseError' has the same name as a type, enum, or domain; one name names one declaration` at line 3. It shows the internal `std__` spelling instead of naming `std.ParseError`, as the type/domain message now does.

To close: use the standard-error wording for traits as well.
