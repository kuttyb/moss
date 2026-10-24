# Phase 25.3 J1 — Overnight Mechanical C++ → Java Compiler Port Baseline

## 1. Reference Selection & Provenance

- **Reference Commit SHA:** `83ebeb1913a8a70e76643bbf99fcce71188d5289` (pushed on `origin/phase25-2-work`)
- **Reference Tree Title:** `docs: record R3 Architect GO`
- **Reference State:** Phase 25.2 R3 complete, Clang+mold build clean, independent Architect GO with zero open P0/P1/P2 findings.
- **Reference Compiler Build Command:** `make --jobs="$(nproc)"`
- **Compiler Executables:**
  - `./moss` (wrapper / symlink to default development binary)
  - `build/compiler/bin/moss-dev` (Clang `-O0` development executable)
  - `build/compiler/bin/moss-opt` (Clang `-O2` release executable)
- **Supported CLI Modes:**
  - `moss check <source.moss>` (source check / validation)
  - `moss check --json <source.moss>` (structured diagnostic check)
  - `moss <source.moss> [-o output.rs]` (native Rust code generation)
  - `moss run --interp <source.moss> [--trace]` (Fast Debug deterministic interpreter)
  - `moss debug <source.moss> [--trace]` (Fast Debug project/source debugging)
  - `moss test --interp <source.moss>` (Fast Debug test runner)
  - Semantic queries: `inspect`, `type`, `effects`, `ownership`, `calls`, `why`, `cost`, `impact`, `resolve`, `references`, `symbols`, `complete`, `debug-query`
- **ABI Modes:**
  - Default: ABI-v9 (`.mossi` source-free interface format)
  - Staged / Opt-in: ABI-v10 (`.mossi` with checked-body IR and occurrence facts)
- **Test Runner & Coverage:**
  - `make check` / `python3 tests/run_parallel.py`: **137/137 passing**, 0 failed, 0 skipped.
  - Pre-existing known failing optional direct controls (not run in `make check`):
    - `tests/tooling/check_phase25_generic_arithmetic_requirements.py` (aborts on forged provenance assertion)
    - `tests/tooling/check_phase25_statement_call_occurrence_roles.py` (fails private-fact assertion)
- **C++ Compiler Build Latencies (16-core host baseline):**
  - Clean Debug (`-O0`): 12.71s – 18.89s
  - Clean Release (`-O2`): 50.97s – 94.95s
  - No-op make: 0.49s – 0.60s
  - Single TU incremental rebuild:
    - `parser.cpp`: 1.90s compile / 0.58s link
    - `native_generator.cpp`: 4.66s compile / 0.70s link
    - `semantic_checker.cpp`: 9.59s – 11.24s compile / 0.68s link
    - Shared header (`type_ir.hpp`): 12.39s (10 dependent objects)

## 2. Java Toolchain & Environment Baseline

- **Java Runtime & Compiler:** OpenJDK 25.0.4.1 (Ubuntu Linux x86_64)
- **Headless JDT Incremental Compiler:** `/opt/eclipse-jdt/eclipse` with `org.mosslang.jdt.builder` bundle
- **High-Performance Primitive Collections:** Eclipse Collections 13.0.0 (`eclipse-collections-13.0.0.jar` & `eclipse-collections-api-13.0.0.jar`)
- **Standalone Fallback Build:** Direct `javac` via build scripts (`./scripts/java/build.sh`)
- **Persistent JDT Dev Build:** Persistent socket daemon via `./scripts/java/dev-build.sh`

## 3. Translation Units & Ownership Mapping

| C++ Subsystem / TU | C++ Lines | Target Java Package | Responsible Agent | Primary Parity Target |
|---|---|---|---|---|
| `parser.cpp`, `parser.hpp`, `parser_support.hpp` | ~1,100 | `org.mosslang.compiler.parser` | Agent B | Lexer, tokens, parser routines, AST builder |
| `ast.hpp`, `program_identity.cpp` | ~1,600 | `org.mosslang.compiler.ast` | Agent B | AST records/classes, spans, Program representation |
| `type_ir.hpp`, `type_ir.cpp`, `constraints.hpp` | ~1,900 | `org.mosslang.compiler.types` | Agent A | TypeRef, TypeKind, TypeVar, TypeSolver, unification, transactions |
| `semantic_checker.cpp`, `semantic_checker.hpp` | ~30,900 | `org.mosslang.compiler.semantics` | Agent A | TypeEnv, type checking, inference, ownership, effects, domains |
| `functional_optimizer.cpp`, `functional_ir.hpp` | ~1,700 | `org.mosslang.compiler.optimizer` | Agent A / B | Pipeline optimization, fusion, lowering |
| `native_generator.cpp`, `native_generator.hpp` | ~6,800 | `org.mosslang.compiler.backend` | Agent A / B | Native Rust backend emitter, locks, domain runtime |
| `checked_body_ir.cpp`, `checked_body_ir.hpp` | ~2,500 | `org.mosslang.compiler.abi` | Agent A | Checked body codec, occurrence ledger, ABI-v9/v10 |
| `interpreter.hpp`, runtime TUs | ~4,500 | `org.mosslang.compiler.interpreter` | Agent A / B | Fast Debug interpreter, value runtime, traces |
| `moss.cpp` (driver, CLI, queries) | ~23,800 | `org.mosslang.compiler.cli` | Agent A | Command line parser, driver pipeline, query formatting |

## 4. Acceptance Invariants

1. **Mechanical Port:** No alteration of Moss syntax, grammar, type rules, effect rules, domain rules, locking rules, or ABI contracts.
2. **Oracle Agreement:** Pinned C++ compiler at `83ebeb19` is the behavioral oracle for diagnostics, exit codes, and output.
3. **Transactional TypeSolver:** Type inference transactions must rollback cleanly on failed unification without leaving stale substitutions.
4. **Independent Javac Build:** Production build must run cleanly with plain `javac` without requiring Eclipse JDT.
5. **Persistent JDT Acceleration:** Development build must execute via persistent JDT service with subsecond incremental latency.
