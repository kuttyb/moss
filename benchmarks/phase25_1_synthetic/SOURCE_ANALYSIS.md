# Moss Compiler Source Analysis (Phase 25.1B)

## 1. Overview of Compilation Unit

Moss's monolithic implementation resides in `src/moss.cpp` (46,711 lines), accompanied by 19 headers and include fragments totaling 69,058 lines across `src/`. The total compilation volume exceeds 115,769 lines of C++17.

### Subsystem Organization & Asymmetry

Unlike typical modular C++ projects with evenly divided subsystems, Moss's codebase is heavily dominated by `Checker` (~19.5K lines, 41.8% of `moss.cpp`) and `Generator` (~6.8K lines, 14.6% of `moss.cpp`):

| Subsystem Region | Source Span in `moss.cpp` | Lines | Branches | Loops | Lambdas | Approx Methods |
|---|---|---:|---:|---:|---:|---:|
| **Core & Infrastructure** | L1–L1711 | 1,711 | 261 | 68 | 8 | 96 |
| **Parser** | L1712–L2792 | 1,081 | 277 | 41 | 4 | 24 |
| **ABI Records & Serialization** | L2793–L4094 | 1,302 | 140 | 28 | 27 | 6 |
| **Checker: Core & Types** | L4095–L11000 | 6,906 | 1,274 | 409 | 96 | 92 |
| **Checker: Ownership & Effects** | L11001–L17500 | 6,500 | 927 | 486 | 76 | 45 |
| **Checker: Bodies & Unification** | L17501–L23615 | 6,115 | 996 | 255 | 56 | 39 |
| **Generator (Rust Lowering)** | L23616–L30444 | 6,829 | 1,073 | 341 | 52 | 80 |
| **Semantic Queries & Agent APIs** | L30445–L35077 | 4,633 | 549 | 296 | 49 | 27 |
| **Project & Module Engine** | L35078–L41990 | 6,913 | 828 | 415 | 86 | 33 |
| **Tooling, Bench & Impact** | L41991–L45095 | 3,105 | 418 | 144 | 14 | 21 |
| **Fast Debug & CLI Driver** | L45096–L46711 | 1,616 | 282 | 68 | 3 | 27 |

### Key Structural Insight:
- `Checker` is by far the largest component (19,520 lines total across 3 distinct regions: Core/Types, Ownership/Effects, and Body Unification).
- Any translation unit partitioning that leaves `Checker` as a single unit will create a massive **critical path bottleneck** (e.g. ~7–8 seconds of serial compile time), preventing clean-build speedups beyond ~2x.
- Therefore, `Checker` **must be subdivided** into at least 2 or 3 separate translation units (e.g. `Checker_Core`, `Checker_Ownership`, `Checker_Bodies`).

---

## 2. STL Container & Template Specialization Census

Total STL container instantiation sites across `src/`:
- `std::vector`: **296 occurrences**
- `std::map`: **161 occurrences**
- `std::unordered_map`: **173 occurrences**
- `std::set`: **383 occurrences**
- `std::optional`: **272 occurrences**
- `std::variant`: **0 occurrences**
- `std::pair`: **120 occurrences**

### Most Frequent Concrete Specializations:

#### `std::vector`
- `std::string` (74 occurrences)
- `TypeRef` (24 occurrences)
- `Value` (20 occurrences)
- `Stmt` (14 occurrences)
- `Record` (9 occurrences)
- `std::pair<std::string, TypeRef` (8 occurrences)
- `WireType` (8 occurrences)
- `SemanticDependency` (7 occurrences)

#### `std::map` & `std::unordered_map`
- `std::map<string, string>` (19 occurrences)
- `std::map<string, int>` (10 occurrences)
- `std::map<string, size_t>` (10 occurrences)
- `std::map<std::string, std::string>` (10 occurrences)
- `std::map<size_t, SynchronizationMode>` (9 occurrences)
- `std::map<string, std::set<string>` (9 occurrences)
- `std::unordered_map<string, string>` (92 occurrences)
- `std::unordered_map<string, moss::TypeRef>` (8 occurrences)
- `std::unordered_map<string, size_t>` (6 occurrences)
- `std::unordered_map<string, vector<string>` (6 occurrences)
- `std::unordered_map<string, const SemanticSnapshotUnit*>` (5 occurrences)
- `std::unordered_map<std::string, std::string>` (4 occurrences)

---

## 3. Clang Trace Hotspots & Codegen Pressure

From the live Clang 18 `-ftime-trace` on host (total traced compilation: 17.94s):
- **Template Instantiation:** 6.74s (62.2% of Frontend)
- **Backend Codegen Passes:** 6.42s (35.8% of Total Compile)
- **Header Parsing (`Source`):** 1.37s

### Top Function Codegen Hotspots:
- `moss::load_module_interface`: 0.017s codegen duration
- `moss::Checker::check_expression`: 0.015s codegen duration
- `moss::Generator::expr`: 0.014s codegen duration
- `moss::Generator::gen_block`: 0.012s codegen duration
- `main`: 0.010s codegen duration
- `moss::FastInterpreter::eval`: 0.009s codegen duration
- `moss::Checker::check_stmts(const vector<Stmt> &, TypeEnv, const Domain *, const Handler *, const Function *, bool)::(anonymous class)::operator()`: 0.008s codegen duration
- `moss::Checker::check_function_call`: 0.007s codegen duration
- `moss::FastInterpreter::execute`: 0.007s codegen duration
- `moss::Checker::check_ownership_expression_walk`: 0.006s codegen duration

---

## 4. Include Dependencies & Coupling

Moss relies on a set of core shared headers that define core data models:
- `ast.hpp`: Defines AST statement and expression nodes. Included transitively everywhere.
- `type_ir.hpp`: Defines TypeSolver, Undo trail, TypeKind, and unification logic. High template instantiation cost.
- `functional_ir.hpp`: Defines functional pipeline IR.
- `checked_body_ir.hpp` & `abi_v10_records.hpp`: Cross-module serialization structures.
- `interpreter.hpp`: Standalone Fast Debug AST evaluation engine.

### Coupling Challenges for TU Partitioning:
1. `Checker` methods access internal fields of `TypeEnv`, `Checker` state (`bindings`, `generic_instantiations`, `type_solver`), requiring either:
   - Shared private header `src/checker.hpp` declaring `class Checker` with out-of-line method definitions in separate `.cpp` files, OR
   - Member methods split across multiple `.cpp` files all including `checker.hpp`.
2. `Generator` methods access `ast.hpp` and `type_ir.hpp`, but do NOT depend on `Checker` internal state! This makes `Generator` cleanly separable.
3. `Parser` depends only on `ast.hpp` and lexer state; completely independent of `Checker` and `Generator`.
4. Driver/Project management (`moss build`, `margo`, test harness) depends on `Parser`, `Checker`, and `Generator` interfaces, but does not access their internal private bodies.
