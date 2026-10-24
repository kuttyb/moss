#!/usr/bin/env python3
"""
Deterministic Mutation Manager (Phase 25.3 J0-R1)
Generates distinct, valid, deterministic source revisions for every trial.
Records source hashes before and after.
Asserts that each timed edit genuinely changes the intended source.
"""

import hashlib
import re
import shutil
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent.parent
BENCHMARK_DIR = REPO_ROOT / "benchmarks" / "java-compiler"
CORPUS_DIR = BENCHMARK_DIR / "corpus"
BACKUP_DIR = BENCHMARK_DIR / "corpus_backup"

def hash_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def backup_baseline(corpus_path: Path = CORPUS_DIR):
    if not BACKUP_DIR.exists():
        shutil.copytree(corpus_path, BACKUP_DIR)
        print(f"Baseline corpus backed up to {BACKUP_DIR}")

def restore_baseline(corpus_path: Path = CORPUS_DIR):
    if BACKUP_DIR.exists():
        if corpus_path.exists():
            shutil.rmtree(corpus_path)
        shutil.copytree(BACKUP_DIR, corpus_path)

class MutationManager:
    def __init__(self, corpus_path: Path = CORPUS_DIR):
        self.corpus_path = corpus_path
        backup_baseline(self.corpus_path)

    def restore(self):
        restore_baseline(self.corpus_path)

    def apply_mutation(self, scenario: str, trial: int = 0) -> dict:
        """
        Applies a distinct, deterministic mutation for scenario and trial index.
        Returns a dict of {changed_file_path: (sha_before, sha_after)}.
        """
        sc = scenario.upper()
        audit = {}

        if sc == "B3":
            # No-op: zero files changed
            return {}

        elif sc == "B4":
            # Private method edit in TypeUnifier.java (logic tweak inside calculateInternalProfile)
            p = self.corpus_path / "org" / "mosslang" / "compiler" / "types" / "TypeUnifier.java"
            sha_before = hash_file(p)
            txt = p.read_text(encoding="utf-8")
            
            pattern = r"private static int calculateInternalProfile\(\) \{[\s\S]*?\}"
            replacement = f"""private static int calculateInternalProfile() {{
        int _val = 1000 + ({trial + 1});
        return _val;
    }}"""
            new_txt = re.sub(pattern, replacement, txt)
            assert new_txt != txt, f"B4 replacement failed to modify TypeUnifier.java on trial {trial}"
            p.write_text(new_txt, encoding="utf-8")
            sha_after = hash_file(p)
            assert sha_before != sha_after, f"B4 SHA-256 did not change on trial {trial}"
            audit[str(p)] = (sha_before, sha_after)

        elif sc == "B5":
            # Public method implementation edit in TypeSolver.java
            p = self.corpus_path / "org" / "mosslang" / "compiler" / "types" / "TypeSolver.java"
            sha_before = hash_file(p)
            txt = p.read_text(encoding="utf-8")
            pattern = r"public int getConstraintPassCapacity\(\) \{[\s\S]*?\}"
            replacement = f"""public int getConstraintPassCapacity() {{
        int _cap = 256 + ({trial + 1});
        return _cap;
    }}"""
            new_txt = re.sub(pattern, replacement, txt)
            assert new_txt != txt, f"B5 replacement failed to modify TypeSolver.java on trial {trial}"
            p.write_text(new_txt, encoding="utf-8")
            sha_after = hash_file(p)
            assert sha_before != sha_after, f"B5 SHA-256 did not change on trial {trial}"
            audit[str(p)] = (sha_before, sha_after)

        elif sc == "B6":
            # Public API change: SymbolTable.lookup(String) -> lookup(String, int), updating callers
            st = self.corpus_path / "org" / "mosslang" / "compiler" / "symbols" / "SymbolTable.java"
            sha_st_before = hash_file(st)
            st_txt = st.read_text(encoding="utf-8")
            pattern_st = r"public Symbol lookup\(String name(?:, int scopeDepthFilter)?\) \{[\s\S]*?\}"
            repl_st = f"""public Symbol lookup(String name, int scopeDepthFilter) {{
        if (scopeDepthFilter < 0) return null;
        int _salt = {trial + 1};
        return currentScope.lookup(name);
    }}"""
            new_st_txt = re.sub(pattern_st, repl_st, st_txt)
            st.write_text(new_st_txt, encoding="utf-8")
            sha_st_after = hash_file(st)
            assert sha_st_before != sha_st_after, f"SymbolTable SHA did not change on trial {trial}"
            audit[str(st)] = (sha_st_before, sha_st_after)

            # Update NameResolver caller
            nr = self.corpus_path / "org" / "mosslang" / "compiler" / "symbols" / "NameResolver.java"
            sha_nr_before = hash_file(nr)
            nr_txt = nr.read_text(encoding="utf-8")
            new_nr_txt = re.sub(r"table\.lookup\(expr\.name\(\)(?:, [^)]+)?\)", f"table.lookup(expr.name(), 50 + {trial + 1})", nr_txt)
            nr.write_text(new_nr_txt, encoding="utf-8")
            sha_nr_after = hash_file(nr)
            assert sha_nr_before != sha_nr_after, f"NameResolver SHA did not change on trial {trial}"
            audit[str(nr)] = (sha_nr_before, sha_nr_after)

            # Update SemanticChecker caller
            sc_file = self.corpus_path / "org" / "mosslang" / "compiler" / "semantics" / "SemanticChecker.java"
            sha_sc_before = hash_file(sc_file)
            sc_txt = sc_file.read_text(encoding="utf-8")
            new_sc_txt = re.sub(r"ctx\.symbolTable\(\)\.lookup\(expr\.name\(\)(?:, [^)]+)?\)", f"ctx.symbolTable().lookup(expr.name(), 50 + {trial + 1})", sc_txt)
            sc_file.write_text(new_sc_txt, encoding="utf-8")
            sha_sc_after = hash_file(sc_file)
            assert sha_sc_before != sha_sc_after, f"SemanticChecker SHA did not change on trial {trial}"
            audit[str(sc_file)] = (sha_sc_before, sha_sc_after)

        elif sc == "B7":
            # High-fan-out TypeRef change: Change typePrecedence() -> typePrecedence(int) across 27 files
            tr = self.corpus_path / "org" / "mosslang" / "compiler" / "types" / "TypeRef.java"
            sha_tr_before = hash_file(tr)
            tr_txt = tr.read_text(encoding="utf-8")
            repl_tr = f"""default int typePrecedence(int passLevel) {{
        int _salt = {trial + 1};
        return 10 + passLevel;
    }}"""
            new_tr_txt = re.sub(r"default int typePrecedence\([\s\S]*?\) \{[\s\S]*?\}", repl_tr, tr_txt)
            tr.write_text(new_tr_txt, encoding="utf-8")
            sha_tr_after = hash_file(tr)
            assert sha_tr_before != sha_tr_after, f"TypeRef SHA did not change on trial {trial}"
            audit[str(tr)] = (sha_tr_before, sha_tr_after)

            # Update core types & specialized types
            core_types = [
                "PrimitiveType.java", "FunctionType.java", "ArrayType.java",
                "DomainType.java", "RecordType.java", "TypeVar.java"
            ]
            for ct in core_types:
                p = self.corpus_path / "org" / "mosslang" / "compiler" / "types" / ct
                if p.exists():
                    sha_b = hash_file(p)
                    txt = p.read_text(encoding="utf-8")
                    method_code = f"""@Override
    public int typePrecedence(int passLevel) {{
        return 10 + passLevel + ({trial + 1});
    }}"""
                    if "typePrecedence" in txt:
                        new_txt = re.sub(r"@Override\s+public int typePrecedence\([\s\S]*?\) \{[\s\S]*?\}", method_code, txt)
                    else:
                        last_brace = txt.rfind("}")
                        new_txt = txt[:last_brace] + "\n    " + method_code + "\n}\n"
                    p.write_text(new_txt, encoding="utf-8")
                    sha_a = hash_file(p)
                    assert sha_b != sha_a, f"{ct} SHA did not change on trial {trial}"
                    audit[str(p)] = (sha_b, sha_a)

            for sp in self.corpus_path.glob("**/SpecializedType_*.java"):
                sha_b = hash_file(sp)
                txt = sp.read_text(encoding="utf-8")
                method_code = f"""@Override
    public int typePrecedence(int passLevel) {{
        return 10 + passLevel + ({trial + 1});
    }}"""
                if "typePrecedence" in txt:
                    new_txt = re.sub(r"@Override\s+public int typePrecedence\([\s\S]*?\) \{[\s\S]*?\}", method_code, txt)
                else:
                    last_brace = txt.rfind("}")
                    new_txt = txt[:last_brace] + "\n    " + method_code + "\n}\n"
                sp.write_text(new_txt, encoding="utf-8")
                sha_a = hash_file(sp)
                assert sha_b != sha_a, f"{sp.name} SHA did not change on trial {trial}"
                audit[str(sp)] = (sha_b, sha_a)

        elif sc == "B8":
            # Semantic checker algorithmic change in analyzeComplexityBudget
            sc_file = self.corpus_path / "org" / "mosslang" / "compiler" / "semantics" / "SemanticChecker.java"
            sha_b = hash_file(sc_file)
            txt = sc_file.read_text(encoding="utf-8")
            pattern = r"public int analyzeComplexityBudget\(SourceFile file\) \{[\s\S]*?return budget;\s*\}"
            replacement = f"""public int analyzeComplexityBudget(SourceFile file) {{
        int budget = 1000 + ({trial + 1});
        for (FunctionDecl f : file.functions()) {{
            budget -= f.body().statements().size() * (10 + ({trial + 1}));
        }}
        return budget;
    }}"""
            new_txt = re.sub(pattern, replacement, txt)
            sc_file.write_text(new_txt, encoding="utf-8")
            sha_a = hash_file(sc_file)
            assert sha_b != sha_a, f"SemanticChecker SHA did not change on trial {trial}"
            audit[str(sc_file)] = (sha_b, sha_a)

        elif sc == "B9":
            # Multiple-file change: Exactly 20 ABI section files
            abi_dir = self.corpus_path / "org" / "mosslang" / "compiler" / "abi"
            targets = [
                abi_dir / "BytecodeBuffer.java",
                abi_dir / "CheckedBodyCodec.java",
                abi_dir / "OccurrenceLedger.java",
                abi_dir / "OccurrenceRecord.java",
                abi_dir / "OccurrenceRole.java"
            ]
            for num in range(1, 16):
                targets.append(abi_dir / f"AbiSection_{num:03d}.java")

            assert len(targets) == 20, f"Expected 20 ABI files, got {len(targets)}"
            for t in targets:
                sha_b = hash_file(t)
                txt = t.read_text(encoding="utf-8")
                pattern = r"public static final int ABI_COMPATIBILITY_EPOCH = [^;]+;"
                replacement = f"public static final int ABI_COMPATIBILITY_EPOCH = 100 + ({trial + 1});"
                new_txt = re.sub(pattern, replacement, txt)
                t.write_text(new_txt, encoding="utf-8")
                sha_a = hash_file(t)
                assert sha_b != sha_a, f"{t.name} SHA did not change on trial {trial}"
                audit[str(t)] = (sha_b, sha_a)

        elif sc == "B10":
            # Primitive collections in OccurrenceLedger.java
            ol = self.corpus_path / "org" / "mosslang" / "compiler" / "abi" / "OccurrenceLedger.java"
            sha_b = hash_file(ol)
            txt = ol.read_text(encoding="utf-8")
            pattern = r"public long computePrimitiveChecksum\(\) \{[\s\S]*?return c;\s*\}"
            replacement = f"""public long computePrimitiveChecksum() {{
        long c = ({trial + 1});
        for (int i = 0; i < packedOccurrences.size(); i++) {{
            c = (c * 31) + packedOccurrences.get(i) + ({trial + 1});
        }}
        return c;
    }}"""
            new_txt = re.sub(pattern, replacement, txt)
            ol.write_text(new_txt, encoding="utf-8")
            sha_a = hash_file(ol)
            assert sha_b != sha_a, f"OccurrenceLedger SHA did not change on trial {trial}"
            audit[str(ol)] = (sha_b, sha_a)

        elif sc == "DEP_INVALIDATION":
            tr = self.corpus_path / "org" / "mosslang" / "compiler" / "types" / "TypeRef.java"
            sha_tr_b = hash_file(tr)
            tr_txt = tr.read_text(encoding="utf-8")
            new_tr = re.sub(r"default long tagValue\([\s\S]*?\) \{[\s\S]*?\}",
                            f"default long tagValue(int pass) {{ int _s = {trial + 1}; return 42L + pass; }}", tr_txt)
            tr.write_text(new_tr, encoding="utf-8")
            sha_tr_a = hash_file(tr)
            assert sha_tr_b != sha_tr_a, f"TypeRef SHA did not change on trial {trial}"
            audit[str(tr)] = (sha_tr_b, sha_tr_a)

            ts = self.corpus_path / "org" / "mosslang" / "compiler" / "types" / "TypeSolver.java"
            sha_ts_b = hash_file(ts)
            ts_txt = ts.read_text(encoding="utf-8")
            new_ts = re.sub(r"public long computeTag\(TypeRef t\) \{[\s\S]*?\}",
                            f"public long computeTag(TypeRef t) {{ int _s = {trial + 1}; return t.tagValue(100); }}", ts_txt)
            ts.write_text(new_ts, encoding="utf-8")
            sha_ts_a = hash_file(ts)
            assert sha_ts_b != sha_ts_a, f"TypeSolver SHA did not change on trial {trial}"
            audit[str(ts)] = (sha_ts_b, sha_ts_a)

        elif sc == "SYNTAX_ERROR":
            unifier = self.corpus_path / "org" / "mosslang" / "compiler" / "types" / "TypeUnifier.java"
            sha_b = hash_file(unifier)
            txt = unifier.read_text(encoding="utf-8")
            unifier.write_text(f"// Syntax error trial {trial}\nthis is a deliberate syntax error;\n" + txt, encoding="utf-8")
            sha_a = hash_file(unifier)
            assert sha_b != sha_a, f"TypeUnifier SHA did not change on trial {trial}"
            audit[str(unifier)] = (sha_b, sha_a)

        else:
            raise ValueError(f"Unknown scenario: {scenario}")

        return audit
