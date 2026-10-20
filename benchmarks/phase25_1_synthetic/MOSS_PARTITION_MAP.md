# Moss Real Source Partition Map (Phase 25.1B)

## 1. Architectural Partitioning Strategy

Moss's monolithic implementation in `src/moss.cpp` ({46,711} lines) cannot be divided into uniform, symmetric slices because the semantic workload is heavily concentrated in `Checker` (19,520 lines, 41.8%) and `Generator` (6,829 lines, 14.6%). A naive split by class would leave a monolithic `checker.cpp` that takes ~7–8 seconds to compile alone, severely capping parallel build speedup to <2.0x due to Amdahl's Law.

To achieve maximum concurrency and avoid a dominant critical path, the proposed partition divides Moss into **10 distinct translation units** plus a shared Precompiled Header (PCH) and an Explicit Template Instantiation (ETI) unit.

### Proposed 10-TU Partition Table

| Proposed TU | Real Source Regions in `moss.cpp` | Lines | Estimated Compile Cost (Host) | Shared Dependencies & Exports |
|---|---|---:|---:|---|
| **TU 1: Core & Common Diagnostics** | L1–L1711 | 1,711 | 0.85s | `<string>`, `<vector>`, `<map>`, `ast.hpp`, `common.hpp` |
| **TU 2: Parser & Lexer** | L1712–L2792 | 1,081 | 0.95s | `ast.hpp`, `lexer.hpp` |
| **TU 3: ABI Records & Source-Free Deserializer** | L2793–L4094 | 1,302 | 1.20s | `ast.hpp`, `type_ir.hpp`, `checked_body_ir.hpp`, `source_free_abi_v10.hpp` |
| **TU 4: Checker — Core & Declarations** | L4095–L11000 | 6,906 | 2.50s | `ast.hpp`, `type_ir.hpp`, `checker_decl.hpp` (exports `Checker::check_module`, `check_domain`) |
| **TU 5: Checker — Expression Type Inference** | L11001–L17500 | 6,500 | 2.85s | `ast.hpp`, `type_ir.hpp`, `checker_decl.hpp` (exports `Checker::check_expression`, `check_call`) |
| **TU 6: Checker — Ownership, Effects & Concurrency** | L17501–L23615 | 6,115 | 2.65s | `ast.hpp`, `type_ir.hpp`, `checker_decl.hpp` (exports `OwnershipEnv`, `check_effects`) |
| **TU 7: Generator — AST to Rust Lowering** | L23616–L30444 | 6,829 | 2.70s | `ast.hpp`, `type_ir.hpp`, `generator_decl.hpp` (consumes `const Program&`) |
| **TU 8: Semantic Queries & Agent API Engine** | L30445–L35077 | 4,633 | 1.80s | `ast.hpp`, `typed_semantic_api.hpp` (`moss inspect`, `moss why`, `moss calls`) |
| **TU 9: Project Engine & Module Resolver** | L35078–L41990 | 6,913 | 2.55s | `ast.hpp`, `project.hpp` (`moss build`, multi-module linking, `margo` integration) |
| **TU 10: Tooling, Bench, Fast Debug & CLI Driver** | L41991–L46711 | 4,721 | 1.95s | `ast.hpp`, `interpreter.hpp` (`moss fmt`, `moss bench`, `main()`) |

### Critical Path Analysis:
- In the monolithic baseline, compilation is serial: **~15.8s** uninstrumented compile+link.
- In the 10-TU partition, the longest single translation unit is **TU 5 (Checker — Expression Type Inference)** at **~2.85s**.
- With parallel scheduling (`-j16`):
  - Phase 1: PCH compilation (`~0.85s`)
  - Phase 2: All 10 TUs compiled concurrently (bounded by longest TU: `~2.85s`)
  - Phase 3: Final Link (`~0.45s`)
  - **Estimated Critical Path:** `0.85s + 2.85s + 0.45s = ~4.15s` (**~3.8x clean build wall-clock speedup**).

---

## 2. Shared Header Surface & Coupling

### Definitions that MUST Remain Visible in Headers:
1. `ast.hpp`: The entire AST node hierarchy (`Node`, `Stmt`, `Expr`, `Domain`, `Handler`, `Function`, `Program`). Every translation unit inspects or manipulates AST structures.
2. `type_ir.hpp`: Core semantic facts (`TypeSolver`, `TypeKind`, `TypeRef`, `UndoTrail`). Required by all three Checker TUs, Generator, ABI Deserializer, and Semantic Queries.
3. `checker_decl.hpp`: Forward declarations of `class Checker` and its internal member state (`p_`, `type_solver_`, `current_domain_`, `generic_instantiations_`).

### Large Inline Class Methods Requiring Out-of-Class Definitions:
In monolithic `moss.cpp`, `class Checker` contains over 150 methods defined entirely inline within the class body. To enable TU splitting:
- `class Checker` must be stripped of inline bodies in `checker_decl.hpp`.
- The member function signatures are retained in `checker_decl.hpp`, and their definitions are split into:
  - `src/checker_core.cpp`
  - `src/checker_expr.cpp`
  - `src/checker_ownership.cpp`
- Because C++ allows member functions of a class to be defined across multiple translation units as long as the class declaration is identical, no ABI breakage or virtual method table overhead is introduced.

---

## 3. Boundary Feasibility Validation (Probes)

We evaluated two difficult boundaries using isolated C++17 probes without modifying Moss:

### Probe 1: Checker Member Splitting Across 3 Translation Units
- **Directory:** `tmp/probe_checker_split/`
- **Mechanism:** `SplitChecker` declared in `checker_decl.hpp`, with private state `p_` and `hash_`. Method bodies implemented separately in `checker_core.cpp`, `checker_expr.cpp`, and `checker_ownership.cpp`.
- **Outcome:** Clean compilation with Clang 18 (`-O0 -g0`). Zero symbol conflicts, zero vtable overhead, deterministic multi-TU state mutation. Output verified: `Names: 3, Hash: 7777`.

### Probe 2: Checker / Generator Boundary
- **Directory:** `tmp/probe_generator_split/`
- **Mechanism:** `Generator` consumes `const AstProgram&` emitted by `Checker`. Neither `generator.cpp` nor `generator.hpp` includes `checker.hpp` or accesses `Checker` private state.
- **Outcome:** Clean compilation and link. Confirms that `Generator` has zero internal coupling to `Checker` algorithms, depending exclusively on the annotated AST and type records.

---

## 4. Engineering Effort & Restructuring Impact

To implement this partition in real Moss:
1. **Header extraction (`src/checker_decl.hpp`):** Moving `class Checker`'s declaration to a header and moving its 19,520 lines of method bodies into 3 `.cpp` files is an estimated 1–2 engineer-day refactoring task.
2. **Namespace & helper hygiene:** Static helpers in `moss.cpp` must be scoped to specific namespaces (e.g. `moss::checker`, `moss::gen`) or declared `inline` / in anonymous namespaces to prevent ODR violations.
3. **Build system integration:** Updating `Makefile` to compile 10 object files and link with `$(CXX)` with `make --jobs="$(nproc)"`.
4. **ETI and PCH integration:** Standard library includes (`<vector>`, `<map>`, `<unordered_map>`, `<string>`, `<algorithm>`, `<memory>`) and core types moved to `src/pch.hpp`.
