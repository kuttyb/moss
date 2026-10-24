#!/usr/bin/env python3
"""
Deterministic Mutation Manager for Java Compiler Benchmark Scenarios (Phase 25.3 J0)
Scenarios B3-B10 + Syntax Error Control.
"""

import sys
import shutil
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent.parent
BENCHMARK_DIR = REPO_ROOT / "benchmarks" / "java-compiler"
CORPUS_DIR = BENCHMARK_DIR / "corpus" / "org" / "mosslang" / "compiler"
BACKUP_DIR = BENCHMARK_DIR / "corpus_backup"

def backup_baseline():
    if not BACKUP_DIR.exists():
        shutil.copytree(BENCHMARK_DIR / "corpus", BACKUP_DIR)
        print(f"Baseline corpus backed up to {BACKUP_DIR}")

def restore_baseline():
    if BACKUP_DIR.exists():
        shutil.rmtree(BENCHMARK_DIR / "corpus")
        shutil.copytree(BACKUP_DIR, BENCHMARK_DIR / "corpus")

def apply_mutation(scenario_id: str):
    restore_baseline()
    scenario = scenario_id.upper()

    if scenario == "B3":
        # No-op: no files changed
        pass

    elif scenario == "B4":
        # Private method edit: Modify internal helper in TypeUnifier without touching signature
        target_file = CORPUS_DIR / "types" / "TypeUnifier.java"
        content = target_file.read_text(encoding="utf-8")
        old_str = "if (current.equals(t2)) return true;" if "if (current.equals(t2))" in content else "if (t1.equals(t2)) return true;"
        new_str = old_str + "\n        // Mutation B4: private calculation edit\n        int _dummyCheck = (t1.hashCode() ^ t2.hashCode()) & 0x7FFF;\n        if (_dummyCheck == -99999) return false;"
        assert old_str in content, "Target string not found in TypeUnifier.java"
        target_file.write_text(content.replace(old_str, new_str, 1), encoding="utf-8")

    elif scenario == "B5":
        # Public method implementation edit: Change TypeSolver.solve() body while preserving API
        target_file = CORPUS_DIR / "types" / "TypeSolver.java"
        content = target_file.read_text(encoding="utf-8")
        old_str = "for (TypeConstraint c : constraints) {"
        new_str = "// Mutation B5: public implementation body edit\n        int _passCount = 0;\n        for (TypeConstraint c : constraints) {\n            _passCount++;"
        assert old_str in content, "Target string not found in TypeSolver.java"
        target_file.write_text(content.replace(old_str, new_str, 1), encoding="utf-8")

    elif scenario == "B6":
        # Public signature edit: Add overloaded lookup in SymbolTable and update callers in NameResolver & SemanticChecker
        st_file = CORPUS_DIR / "symbols" / "SymbolTable.java"
        st_content = st_file.read_text(encoding="utf-8")
        old_lookup = "public Symbol lookup(String name) {\n        return currentScope.lookup(name);\n    }"
        new_lookup = """public Symbol lookup(String name) {
        return lookup(name, true);
    }

    public Symbol lookup(String name, boolean checkParents) {
        return checkParents ? currentScope.lookup(name) : currentScope.lookupLocal(name);
    }"""
        assert old_lookup in st_content, "Target lookup not found in SymbolTable.java"
        st_file.write_text(st_content.replace(old_lookup, new_lookup, 1), encoding="utf-8")

        # Update caller in NameResolver.java
        nr_file = CORPUS_DIR / "symbols" / "NameResolver.java"
        nr_content = nr_file.read_text(encoding="utf-8")
        nr_file.write_text(nr_content.replace("table.lookup(expr.name())", "table.lookup(expr.name(), true)"), encoding="utf-8")

        # Update caller in SemanticChecker.java
        sc_file = CORPUS_DIR / "semantics" / "SemanticChecker.java"
        sc_content = sc_file.read_text(encoding="utf-8")
        sc_file.write_text(sc_content.replace("ctx.symbolTable().lookup(expr.name())", "ctx.symbolTable().lookup(expr.name(), true)"), encoding="utf-8")

    elif scenario == "B7":
        # TypeRef change: Central interface change widely referenced
        tr_file = CORPUS_DIR / "types" / "TypeRef.java"
        tr_content = tr_file.read_text(encoding="utf-8")
        old_tr = "default boolean isTypeVar() {\n        return false;\n    }"
        new_tr = """default boolean isTypeVar() {
        return false;
    }

    default int typeNestingDepth() {
        return 1;
    }"""
        assert old_tr in tr_content, "Target method not found in TypeRef.java"
        tr_file.write_text(tr_content.replace(old_tr, new_tr, 1), encoding="utf-8")

        # Override in ArrayType.java
        arr_file = CORPUS_DIR / "types" / "ArrayType.java"
        arr_content = arr_file.read_text(encoding="utf-8")
        old_arr = "@Override\n    public TypeRef substitute"
        new_arr = """@Override
    public int typeNestingDepth() {
        return 1 + elementType.typeNestingDepth();
    }

    @Override
    public TypeRef substitute"""
        assert old_arr in arr_content, "Target method not found in ArrayType.java"
        arr_file.write_text(arr_content.replace(old_arr, new_arr, 1), encoding="utf-8")

    elif scenario == "B8":
        # Semantic-checker change: Substantial algorithm modification in SemanticChecker
        sc_file = CORPUS_DIR / "semantics" / "SemanticChecker.java"
        sc_content = sc_file.read_text(encoding="utf-8")
        old_sc = "switch (expr.op()) {"
        new_sc = """// Mutation B8: substantial semantic checker algorithm update
        inferredEffects.put(expr.hashCode() & 0xFFFF, Effect.READ);
        switch (expr.op()) {"""
        assert old_sc in sc_content, "Target string not found in SemanticChecker.java"
        sc_file.write_text(sc_content.replace(old_sc, new_sc, 1), encoding="utf-8")

    elif scenario == "B9":
        # Multi-file edit: Modify ~20 related AST files (SyntaxNode_001 to SyntaxNode_020)
        for i in range(1, 21):
            num = f"{i:03d}"
            target_file = CORPUS_DIR / "ast" / f"SyntaxNode_{num}.java"
            if target_file.exists():
                content = target_file.read_text(encoding="utf-8")
                old_span = "public SourceSpan span() { return span; }"
                new_span = f"public SourceSpan span() {{ return span; }}\n    public int getNodeOrdinal_{num}() {{ return {i}; }}"
                if old_span in content:
                    target_file.write_text(content.replace(old_span, new_span, 1), encoding="utf-8")

    elif scenario == "B10":
        # Collection implementation change: Primitive collection ops in OccurrenceLedger
        target_file = CORPUS_DIR / "abi" / "OccurrenceLedger.java"
        content = target_file.read_text(encoding="utf-8")
        old_str = "int ord = nextOrdinal++;"
        new_str = """// Mutation B10: primitive collection optimization
        int ord = nextOrdinal++;
        if (packedOccurrences.size() > 1000) packedOccurrences.trimToSize();"""
        assert old_str in content, "Target string not found in OccurrenceLedger.java"
        target_file.write_text(content.replace(old_str, new_str, 1), encoding="utf-8")

    elif scenario == "SYNTAX_ERROR":
        # Deliberate syntax error control
        target_file = CORPUS_DIR / "ast" / "LiteralExpr.java"
        content = target_file.read_text(encoding="utf-8")
        target_file.write_text(content + "\n>>> DELIBERATE SYNTAX ERROR <<<\n", encoding="utf-8")

    else:
        raise ValueError(f"Unknown scenario: {scenario_id}")

    print(f"Applied mutation for scenario: {scenario_id}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: mutations.py <backup|restore|apply <SCENARIO>>")
        sys.exit(1)
    cmd = sys.argv[1].lower()
    if cmd == "backup":
        backup_baseline()
    elif cmd == "restore":
        restore_baseline()
        print("Restored baseline corpus.")
    elif cmd == "apply" and len(sys.argv) >= 3:
        apply_mutation(sys.argv[2])
    else:
        print("Invalid command")
        sys.exit(1)
