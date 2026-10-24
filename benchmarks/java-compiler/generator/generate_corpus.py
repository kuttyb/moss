#!/usr/bin/env python3
"""
Corpus Generator for Moss Java Compiler Build-System Shootout (Phase 25.3 J0)
Produces a deterministic synthetic Java compiler corpus reflecting the architectural
and algorithmic characteristics of Moss compiler subsystems.
"""

import os
import sys
import argparse
import hashlib
import random
from pathlib import Path

def generate(target_files: int, seed: int, output_dir: Path):
    random.seed(seed)
    output_dir = output_dir.resolve()
    base_pkg_dir = output_dir / "org" / "mosslang" / "compiler"
    
    # Subsystems and targets
    subsystems = {
        "ast": int(round(target_files * 0.20)),
        "types": int(round(target_files * 0.175)),
        "symbols": int(round(target_files * 0.125)),
        "semantics": int(round(target_files * 0.175)),
        "generics": int(round(target_files * 0.10)),
        "abi": int(round(target_files * 0.10)),
        "ir": int(round(target_files * 0.075)),
        "driver": int(round(target_files * 0.05)),
    }
    
    # Adjust to sum exactly to target_files
    diff = target_files - sum(subsystems.values())
    subsystems["ast"] += diff

    generated_files = {}

    def add_file(subsystem: str, class_name: str, code: str):
        rel_path = Path("org") / "mosslang" / "compiler" / subsystem / f"{class_name}.java"
        full_path = output_dir / rel_path
        full_path.parent.mkdir(parents=True, exist_ok=True)
        code_trimmed = code.strip() + "\n"
        with open(full_path, "w", encoding="utf-8") as f:
            f.write(code_trimmed)
        generated_files[str(rel_path)] = hashlib.sha256(code_trimmed.encode("utf-8")).hexdigest()

    # =========================================================================
    # 1. DRIVER SUBSYSTEM
    # =========================================================================
    add_file("driver", "SourceLocation", """package org.mosslang.compiler.driver;

public record SourceLocation(String file, int line, int column) {
    public static final SourceLocation UNKNOWN = new SourceLocation("<unknown>", 0, 0);

    @Override
    public String toString() {
        return file + ":" + line + ":" + column;
    }
}
""")

    add_file("driver", "SourceSpan", """package org.mosslang.compiler.driver;

public record SourceSpan(SourceLocation start, SourceLocation end) {
    public static final SourceSpan NONE = new SourceSpan(SourceLocation.UNKNOWN, SourceLocation.UNKNOWN);

    public boolean contains(int line, int col) {
        if (line < start.line() || line > end.line()) return false;
        if (line == start.line() && col < start.column()) return false;
        if (line == end.line() && col > end.column()) return false;
        return true;
    }
}
""")

    add_file("driver", "Severity", """package org.mosslang.compiler.driver;

public enum Severity {
    INFO,
    WARNING,
    ERROR;

    public boolean isAtLeast(Severity other) {
        return this.ordinal() >= other.ordinal();
    }
}
""")

    add_file("driver", "DiagnosticCode", """package org.mosslang.compiler.driver;

public enum DiagnosticCode {
    UNDECLARED_SYMBOL("M1001"),
    DUPLICATE_DECLARATION("M1002"),
    TYPE_MISMATCH("M1003"),
    UNRESOLVED_CALL("M1004"),
    USE_AFTER_CONSUME("M1005"),
    ILLEGAL_MUTATION("M1006"),
    UNREACHABLE_CODE("M1007"),
    MISSING_RETURN("M1008"),
    OCCURRENCE_MISMATCH("M1009"),
    ABI_INTEGRITY_FAILURE("M1010"),
    CONSTRAINT_UNSATISFIABLE("M1011"),
    SYNTAX_ERROR("M1012");

    private final String code;
    DiagnosticCode(String code) { this.code = code; }
    public String getCode() { return code; }
}
""")

    add_file("driver", "Diagnostic", """package org.mosslang.compiler.driver;

public record Diagnostic(DiagnosticCode code, Severity severity, String message, SourceSpan span) {
    public String format() {
        return String.format("[%s] %s at %s: %s", severity, code.getCode(), span.start(), message);
    }
}
""")

    add_file("driver", "DiagnosticSink", """package org.mosslang.compiler.driver;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;

public class DiagnosticSink {
    private final MutableList<Diagnostic> diagnostics = new FastList<>();
    private int errorCount = 0;
    private int warningCount = 0;

    public synchronized void report(Diagnostic diagnostic) {
        diagnostics.add(diagnostic);
        if (diagnostic.severity() == Severity.ERROR) {
            errorCount++;
        } else if (diagnostic.severity() == Severity.WARNING) {
            warningCount++;
        }
    }

    public void error(DiagnosticCode code, String message, SourceSpan span) {
        report(new Diagnostic(code, Severity.ERROR, message, span));
    }

    public void warn(DiagnosticCode code, String message, SourceSpan span) {
        report(new Diagnostic(code, Severity.WARNING, message, span));
    }

    public boolean hasErrors() {
        return errorCount > 0;
    }

    public int getErrorCount() {
        return errorCount;
    }

    public int getWarningCount() {
        return warningCount;
    }

    public MutableList<Diagnostic> getDiagnostics() {
        return diagnostics.asUnmodifiable();
    }

    public void clear() {
        diagnostics.clear();
        errorCount = 0;
        warningCount = 0;
    }
}
""")

    add_file("driver", "CompilerConfig", """package org.mosslang.compiler.driver;

public record CompilerConfig(
    boolean optimize,
    int optimizationLevel,
    boolean verifyAbi,
    boolean dumpIr,
    String targetPlatform
) {
    public static CompilerConfig defaultDebug() {
        return new CompilerConfig(false, 0, true, false, "native-x86_64");
    }

    public static CompilerConfig defaultRelease() {
        return new CompilerConfig(true, 2, true, false, "native-x86_64");
    }
}
""")

    add_file("driver", "CompilationResult", """package org.mosslang.compiler.driver;

import org.eclipse.collections.api.list.MutableList;

public record CompilationResult(
    boolean success,
    int errorCount,
    int warningCount,
    byte[] abiPayload,
    int generatedBytecodeSize,
    MutableList<Diagnostic> diagnostics
) {
    public boolean isSuccess() {
        return success && errorCount == 0;
    }
}
""")

    add_file("driver", "CompilerPipeline", """package org.mosslang.compiler.driver;

public interface CompilerPipeline {
    String name();
    boolean execute(CompilerContext context);
}
""")

    add_file("driver", "CompilerContext", """package org.mosslang.compiler.driver;

import org.mosslang.compiler.ast.SourceFile;
import org.mosslang.compiler.symbols.SymbolTable;
import org.mosslang.compiler.types.TypeSolver;
import org.mosslang.compiler.ir.IrModule;

public class CompilerContext {
    private final CompilerConfig config;
    private final DiagnosticSink diagnostics;
    private SourceFile ast;
    private SymbolTable symbolTable;
    private TypeSolver typeSolver;
    private IrModule irModule;
    private byte[] abiBytes;

    public CompilerContext(CompilerConfig config, DiagnosticSink diagnostics) {
        this.config = config;
        this.diagnostics = diagnostics;
    }

    public CompilerConfig getConfig() { return config; }
    public DiagnosticSink getDiagnostics() { return diagnostics; }
    public SourceFile getAst() { return ast; }
    public void setAst(SourceFile ast) { this.ast = ast; }
    public SymbolTable getSymbolTable() { return symbolTable; }
    public void setSymbolTable(SymbolTable symbolTable) { this.symbolTable = symbolTable; }
    public TypeSolver getTypeSolver() { return typeSolver; }
    public void setTypeSolver(TypeSolver typeSolver) { this.typeSolver = typeSolver; }
    public IrModule getIrModule() { return irModule; }
    public void setIrModule(IrModule irModule) { this.irModule = irModule; }
    public byte[] getAbiBytes() { return abiBytes; }
    public void setAbiBytes(byte[] abiBytes) { this.abiBytes = abiBytes; }
}
""")

    # =========================================================================
    # 2. TYPES SUBSYSTEM
    # =========================================================================
    add_file("types", "TypeRef", """package org.mosslang.compiler.types;

import org.eclipse.collections.api.set.primitive.MutableIntSet;

public interface TypeRef {
    int typeId();
    String name();
    boolean isPrimitive();
    boolean isFunction();
    boolean isDomain();
    boolean isSubtypeOf(TypeRef other);
    TypeRef substitute(TypeSubstitution substitution);
    void collectTypeVarIds(MutableIntSet accumulator);

    default boolean isTypeVar() {
        return false;
    }
}
""")

    add_file("types", "PrimitiveKind", """package org.mosslang.compiler.types;

public enum PrimitiveKind {
    INT(1, "Int"),
    FLOAT(2, "Float"),
    BOOL(3, "Bool"),
    STRING(4, "String"),
    CHAR(5, "Char"),
    UNIT(6, "Unit"),
    NEVER(7, "Never");

    private final int id;
    private final String name;
    PrimitiveKind(int id, String name) { this.id = id; this.name = name; }
    public int getId() { return id; }
    public String getName() { return name; }
}
""")

    add_file("types", "PrimitiveType", """package org.mosslang.compiler.types;

import org.eclipse.collections.api.set.primitive.MutableIntSet;

public record PrimitiveType(PrimitiveKind kind) implements TypeRef {
    public static final PrimitiveType INT = new PrimitiveType(PrimitiveKind.INT);
    public static final PrimitiveType FLOAT = new PrimitiveType(PrimitiveKind.FLOAT);
    public static final PrimitiveType BOOL = new PrimitiveType(PrimitiveKind.BOOL);
    public static final PrimitiveType STRING = new PrimitiveType(PrimitiveKind.STRING);
    public static final PrimitiveType CHAR = new PrimitiveType(PrimitiveKind.CHAR);
    public static final PrimitiveType UNIT = new PrimitiveType(PrimitiveKind.UNIT);
    public static final PrimitiveType NEVER = new PrimitiveType(PrimitiveKind.NEVER);

    @Override
    public int typeId() { return kind.getId(); }
    @Override
    public String name() { return kind.getName(); }
    @Override
    public boolean isPrimitive() { return true; }
    @Override
    public boolean isFunction() { return false; }
    @Override
    public boolean isDomain() { return false; }

    @Override
    public boolean isSubtypeOf(TypeRef other) {
        if (this.equals(other)) return true;
        if (this.kind == PrimitiveKind.NEVER) return true;
        return false;
    }

    @Override
    public TypeRef substitute(TypeSubstitution substitution) {
        return this;
    }

    @Override
    public void collectTypeVarIds(MutableIntSet accumulator) {}
}
""")

    add_file("types", "TypeVar", """package org.mosslang.compiler.types;

import org.eclipse.collections.api.set.primitive.MutableIntSet;

public record TypeVar(int id, String varName) implements TypeRef {
    @Override
    public int typeId() { return 1000 + id; }
    @Override
    public String name() { return "'" + varName; }
    @Override
    public boolean isPrimitive() { return false; }
    @Override
    public boolean isFunction() { return false; }
    @Override
    public boolean isDomain() { return false; }
    @Override
    public boolean isTypeVar() { return true; }

    @Override
    public boolean isSubtypeOf(TypeRef other) {
        if (other instanceof TypeVar tv) {
            return this.id == tv.id;
        }
        return false;
    }

    @Override
    public TypeRef substitute(TypeSubstitution substitution) {
        TypeRef res = substitution.lookup(this.id);
        return res != null ? res : this;
    }

    @Override
    public void collectTypeVarIds(MutableIntSet accumulator) {
        accumulator.add(id);
    }
}
""")

    add_file("types", "FunctionType", """package org.mosslang.compiler.types;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.api.set.primitive.MutableIntSet;
import org.eclipse.collections.impl.list.mutable.FastList;

public record FunctionType(int typeId, MutableList<TypeRef> paramTypes, TypeRef returnType) implements TypeRef {
    @Override
    public String name() {
        StringBuilder sb = new StringBuilder("(");
        for (int i = 0; i < paramTypes.size(); i++) {
            if (i > 0) sb.append(", ");
            sb.append(paramTypes.get(i).name());
        }
        sb.append(") -> ").append(returnType.name());
        return sb.toString();
    }

    @Override
    public boolean isPrimitive() { return false; }
    @Override
    public boolean isFunction() { return true; }
    @Override
    public boolean isDomain() { return false; }

    @Override
    public boolean isSubtypeOf(TypeRef other) {
        if (other instanceof FunctionType o) {
            if (paramTypes.size() != o.paramTypes.size()) return false;
            // Contravariant params
            for (int i = 0; i < paramTypes.size(); i++) {
                if (!o.paramTypes.get(i).isSubtypeOf(paramTypes.get(i))) return false;
            }
            // Covariant return
            return returnType.isSubtypeOf(o.returnType);
        }
        return false;
    }

    @Override
    public TypeRef substitute(TypeSubstitution substitution) {
        MutableList<TypeRef> newParams = new FastList<>(paramTypes.size());
        for (TypeRef p : paramTypes) {
            newParams.add(p.substitute(substitution));
        }
        TypeRef newRet = returnType.substitute(substitution);
        return new FunctionType(typeId, newParams, newRet);
    }

    @Override
    public void collectTypeVarIds(MutableIntSet accumulator) {
        for (TypeRef p : paramTypes) p.collectTypeVarIds(accumulator);
        returnType.collectTypeVarIds(accumulator);
    }
}
""")

    add_file("types", "DomainType", """package org.mosslang.compiler.types;

import org.eclipse.collections.api.set.primitive.MutableIntSet;

public record DomainType(int typeId, String domainName) implements TypeRef {
    @Override
    public String name() { return "Domain[" + domainName + "]"; }
    @Override
    public boolean isPrimitive() { return false; }
    @Override
    public boolean isFunction() { return false; }
    @Override
    public boolean isDomain() { return true; }

    @Override
    public boolean isSubtypeOf(TypeRef other) {
        return this.equals(other);
    }

    @Override
    public TypeRef substitute(TypeSubstitution substitution) {
        return this;
    }

    @Override
    public void collectTypeVarIds(MutableIntSet accumulator) {}
}
""")

    add_file("types", "ArrayType", """package org.mosslang.compiler.types;

import org.eclipse.collections.api.set.primitive.MutableIntSet;

public record ArrayType(int typeId, TypeRef elementType) implements TypeRef {
    @Override
    public String name() { return "[" + elementType.name() + "]"; }
    @Override
    public boolean isPrimitive() { return false; }
    @Override
    public boolean isFunction() { return false; }
    @Override
    public boolean isDomain() { return false; }

    @Override
    public boolean isSubtypeOf(TypeRef other) {
        if (other instanceof ArrayType o) {
            return elementType.isSubtypeOf(o.elementType);
        }
        return false;
    }

    @Override
    public TypeRef substitute(TypeSubstitution substitution) {
        return new ArrayType(typeId, elementType.substitute(substitution));
    }

    @Override
    public void collectTypeVarIds(MutableIntSet accumulator) {
        elementType.collectTypeVarIds(accumulator);
    }
}
""")

    add_file("types", "RecordType", """package org.mosslang.compiler.types;

import org.eclipse.collections.api.map.MutableMap;
import org.eclipse.collections.api.set.primitive.MutableIntSet;
import org.eclipse.collections.impl.map.mutable.UnifiedMap;

public record RecordType(int typeId, String typeName, MutableMap<String, TypeRef> fields) implements TypeRef {
    @Override
    public String name() { return typeName; }
    @Override
    public boolean isPrimitive() { return false; }
    @Override
    public boolean isFunction() { return false; }
    @Override
    public boolean isDomain() { return false; }

    @Override
    public boolean isSubtypeOf(TypeRef other) {
        if (other instanceof RecordType o) {
            if (!typeName.equals(o.typeName)) return false;
            for (String f : o.fields.keySet()) {
                TypeRef myField = fields.get(f);
                if (myField == null || !myField.isSubtypeOf(o.fields.get(f))) return false;
            }
            return true;
        }
        return false;
    }

    @Override
    public TypeRef substitute(TypeSubstitution substitution) {
        MutableMap<String, TypeRef> newFields = new UnifiedMap<>();
        fields.forEachKeyValue((k, v) -> newFields.put(k, v.substitute(substitution)));
        return new RecordType(typeId, typeName, newFields);
    }

    @Override
    public void collectTypeVarIds(MutableIntSet accumulator) {
        fields.forEachValue(v -> v.collectTypeVarIds(accumulator));
    }
}
""")

    add_file("types", "TypeSubstitution", """package org.mosslang.compiler.types;

public interface TypeSubstitution {
    TypeRef lookup(int varId);
    boolean contains(int varId);
    void bind(int varId, TypeRef type);
}
""")

    add_file("types", "TypeSubstitutionMap", """package org.mosslang.compiler.types;

import org.eclipse.collections.api.map.primitive.MutableIntObjectMap;
import org.eclipse.collections.impl.map.mutable.primitive.IntObjectHashMap;

public class TypeSubstitutionMap implements TypeSubstitution {
    private final MutableIntObjectMap<TypeRef> bindings = new IntObjectHashMap<>();

    @Override
    public TypeRef lookup(int varId) {
        TypeRef target = bindings.get(varId);
        if (target == null) return null;
        if (target instanceof TypeVar tv && bindings.containsKey(tv.id())) {
            return lookup(tv.id());
        }
        return target;
    }

    @Override
    public boolean contains(int varId) {
        return bindings.containsKey(varId);
    }

    @Override
    public void bind(int varId, TypeRef type) {
        bindings.put(varId, type);
    }

    public TypeRef apply(TypeRef type) {
        return type.substitute(this);
    }

    public int size() {
        return bindings.size();
    }
}
""")

    add_file("types", "ConstraintKind", """package org.mosslang.compiler.types;

public enum ConstraintKind {
    EQUAL,
    SUBTYPE,
    CALLABLE
}
""")

    add_file("types", "TypeConstraint", """package org.mosslang.compiler.types;

public record TypeConstraint(TypeRef left, TypeRef right, ConstraintKind kind, String description) {
    public TypeConstraint withSubstitution(TypeSubstitution subst) {
        return new TypeConstraint(left.substitute(subst), right.substitute(subst), kind, description);
    }
}
""")

    add_file("types", "TypeUnifier", """package org.mosslang.compiler.types;

import org.eclipse.collections.api.set.primitive.MutableIntSet;
import org.eclipse.collections.impl.set.mutable.primitive.IntHashSet;

public class TypeUnifier {
    public static boolean occursCheck(int varId, TypeRef type, TypeSubstitution subst) {
        TypeRef current = type.substitute(subst);
        if (current instanceof TypeVar tv) {
            return tv.id() == varId;
        }
        MutableIntSet vars = new IntHashSet();
        current.collectTypeVarIds(vars);
        return vars.contains(varId);
    }

    public static boolean unify(TypeRef t1, TypeRef t2, TypeSubstitution subst) {
        t1 = t1.substitute(subst);
        t2 = t2.substitute(subst);

        if (t1.equals(t2)) return true;

        if (t1 instanceof TypeVar v1) {
            if (occursCheck(v1.id(), t2, subst)) return false;
            subst.bind(v1.id(), t2);
            return true;
        }
        if (t2 instanceof TypeVar v2) {
            if (occursCheck(v2.id(), t1, subst)) return false;
            subst.bind(v2.id(), t1);
            return true;
        }

        if (t1 instanceof FunctionType f1 && t2 instanceof FunctionType f2) {
            if (f1.paramTypes().size() != f2.paramTypes().size()) return false;
            for (int i = 0; i < f1.paramTypes().size(); i++) {
                if (!unify(f1.paramTypes().get(i), f2.paramTypes().get(i), subst)) return false;
            }
            return unify(f1.returnType(), f2.returnType(), subst);
        }

        if (t1 instanceof ArrayType a1 && t2 instanceof ArrayType a2) {
            return unify(a1.elementType(), a2.elementType(), subst);
        }

        return false;
    }
}
""")

    add_file("types", "TypeSolver", """package org.mosslang.compiler.types;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;
import org.mosslang.compiler.driver.DiagnosticSink;
import org.mosslang.compiler.driver.DiagnosticCode;
import org.mosslang.compiler.driver.SourceSpan;

public class TypeSolver {
    private final MutableList<TypeConstraint> constraints = new FastList<>();
    private final TypeSubstitutionMap substitution = new TypeSubstitutionMap();
    private int nextVarId = 1;

    public TypeVar freshTypeVar(String prefix) {
        return new TypeVar(nextVarId++, prefix + "_" + nextVarId);
    }

    public void addConstraint(TypeRef left, TypeRef right, ConstraintKind kind, String desc) {
        constraints.add(new TypeConstraint(left, right, kind, desc));
    }

    public boolean solve(DiagnosticSink sink, SourceSpan span) {
        for (TypeConstraint c : constraints) {
            TypeRef left = substitution.apply(c.left());
            TypeRef right = substitution.apply(c.right());
            
            if (c.kind() == ConstraintKind.EQUAL) {
                if (!TypeUnifier.unify(left, right, substitution)) {
                    sink.error(DiagnosticCode.TYPE_MISMATCH,
                        "Type mismatch in " + c.description() + ": cannot unify " + left.name() + " with " + right.name(),
                        span);
                    return false;
                }
            } else if (c.kind() == ConstraintKind.SUBTYPE) {
                if (!left.isSubtypeOf(right) && !TypeUnifier.unify(left, right, substitution)) {
                    sink.error(DiagnosticCode.TYPE_MISMATCH,
                        "Subtype requirement failed in " + c.description() + ": " + left.name() + " is not a subtype of " + right.name(),
                        span);
                    return false;
                }
            }
        }
        return true;
    }

    public TypeRef resolve(TypeRef type) {
        return substitution.apply(type);
    }

    public TypeSubstitutionMap getSubstitution() {
        return substitution;
    }
}
""")

    add_file("types", "TypeHierarchy", """package org.mosslang.compiler.types;

import org.eclipse.collections.api.map.primitive.MutableIntObjectMap;
import org.eclipse.collections.api.set.primitive.MutableIntSet;
import org.eclipse.collections.impl.map.mutable.primitive.IntObjectHashMap;
import org.eclipse.collections.impl.set.mutable.primitive.IntHashSet;

public class TypeHierarchy {
    private final MutableIntObjectMap<MutableIntSet> directSubtypes = new IntObjectHashMap<>();
    private final MutableIntObjectMap<MutableIntSet> directSupertypes = new IntObjectHashMap<>();

    public void addSubtypeRelation(int subTypeId, int superTypeId) {
        directSubtypes.getIfAbsentPut(superTypeId, IntHashSet::new).add(subTypeId);
        directSupertypes.getIfAbsentPut(subTypeId, IntHashSet::new).add(superTypeId);
    }

    public boolean isAncestor(int candidateSuper, int candidateSub) {
        if (candidateSuper == candidateSub) return true;
        MutableIntSet supers = directSupertypes.get(candidateSub);
        if (supers == null || supers.isEmpty()) return false;
        if (supers.contains(candidateSuper)) return true;
        for (int p : supers.toArray()) {
            if (isAncestor(candidateSuper, p)) return true;
        }
        return false;
    }
}
""")

    add_file("types", "TypeEnv", """package org.mosslang.compiler.types;

import org.eclipse.collections.api.map.MutableMap;
import org.eclipse.collections.impl.map.mutable.UnifiedMap;

public class TypeEnv {
    private final TypeEnv parent;
    private final MutableMap<String, TypeRef> bindings = new UnifiedMap<>();

    public TypeEnv(TypeEnv parent) {
        this.parent = parent;
    }

    public void bind(String name, TypeRef type) {
        bindings.put(name, type);
    }

    public TypeRef lookup(String name) {
        TypeRef t = bindings.get(name);
        if (t != null) return t;
        return parent != null ? parent.lookup(name) : null;
    }

    public boolean isLocal(String name) {
        return bindings.containsKey(name);
    }
}
""")

    # =========================================================================
    # 3. SYMBOLS SUBSYSTEM
    # =========================================================================
    add_file("symbols", "SymbolKind", """package org.mosslang.compiler.symbols;

public enum SymbolKind {
    VARIABLE,
    PARAMETER,
    FUNCTION,
    DOMAIN,
    HANDLER,
    TYPE_ALIAS
}
""")

    add_file("symbols", "Visibility", """package org.mosslang.compiler.symbols;

public enum Visibility {
    PUBLIC,
    PRIVATE
}
""")

    add_file("symbols", "Symbol", """package org.mosslang.compiler.symbols;

import org.mosslang.compiler.types.TypeRef;
import org.mosslang.compiler.driver.SourceLocation;

public record Symbol(
    int symbolId,
    String name,
    SymbolKind kind,
    TypeRef type,
    Visibility visibility,
    SourceLocation location,
    boolean mutable
) {
    public boolean isFunction() { return kind == SymbolKind.FUNCTION; }
    public boolean isVariable() { return kind == SymbolKind.VARIABLE || kind == SymbolKind.PARAMETER; }
}
""")

    add_file("symbols", "Scope", """package org.mosslang.compiler.symbols;

import org.eclipse.collections.api.list.MutableList;

public interface Scope {
    int scopeId();
    Scope parent();
    void declare(Symbol symbol);
    Symbol lookupLocal(String name);
    Symbol lookup(String name);
    MutableList<Symbol> allSymbols();
}
""")

    add_file("symbols", "LexicalScope", """package org.mosslang.compiler.symbols;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.api.map.MutableMap;
import org.eclipse.collections.impl.list.mutable.FastList;
import org.eclipse.collections.impl.map.mutable.UnifiedMap;

public class LexicalScope implements Scope {
    private final int scopeId;
    private final Scope parent;
    private final MutableMap<String, Symbol> symbols = new UnifiedMap<>();

    public LexicalScope(int scopeId, Scope parent) {
        this.scopeId = scopeId;
        this.parent = parent;
    }

    @Override
    public int scopeId() { return scopeId; }
    @Override
    public Scope parent() { return parent; }

    @Override
    public void declare(Symbol symbol) {
        symbols.put(symbol.name(), symbol);
    }

    @Override
    public Symbol lookupLocal(String name) {
        return symbols.get(name);
    }

    @Override
    public Symbol lookup(String name) {
        Symbol s = lookupLocal(name);
        if (s != null) return s;
        return parent != null ? parent.lookup(name) : null;
    }

    @Override
    public MutableList<Symbol> allSymbols() {
        return FastList.newList(symbols.values());
    }
}
""")

    add_file("symbols", "SymbolTable", """package org.mosslang.compiler.symbols;

import org.eclipse.collections.api.map.primitive.MutableIntObjectMap;
import org.eclipse.collections.impl.map.mutable.primitive.IntObjectHashMap;
import org.mosslang.compiler.driver.DiagnosticSink;
import org.mosslang.compiler.driver.DiagnosticCode;
import org.mosslang.compiler.driver.SourceSpan;

public class SymbolTable {
    private final Scope rootScope;
    private Scope currentScope;
    private int nextScopeId = 1;
    private int nextSymbolId = 1;
    private final MutableIntObjectMap<Symbol> symbolsById = new IntObjectHashMap<>();

    public SymbolTable() {
        this.rootScope = new LexicalScope(nextScopeId++, null);
        this.currentScope = rootScope;
    }

    public Scope enterScope() {
        currentScope = new LexicalScope(nextScopeId++, currentScope);
        return currentScope;
    }

    public Scope exitScope() {
        if (currentScope.parent() != null) {
            currentScope = currentScope.parent();
        }
        return currentScope;
    }

    public Symbol declare(String name, SymbolKind kind, org.mosslang.compiler.types.TypeRef type,
                           Visibility vis, org.mosslang.compiler.driver.SourceLocation loc,
                           boolean mutable, DiagnosticSink sink, SourceSpan span) {
        Symbol existing = currentScope.lookupLocal(name);
        if (existing != null) {
            sink.error(DiagnosticCode.DUPLICATE_DECLARATION,
                "Symbol '" + name + "' already declared in this scope", span);
            return existing;
        }
        Symbol sym = new Symbol(nextSymbolId++, name, kind, type, vis, loc, mutable);
        currentScope.declare(sym);
        symbolsById.put(sym.symbolId(), sym);
        return sym;
    }

    public Symbol lookup(String name) {
        return currentScope.lookup(name);
    }

    public Symbol getById(int symbolId) {
        return symbolsById.get(symbolId);
    }

    public Scope getCurrentScope() {
        return currentScope;
    }
}
""")

    add_file("symbols", "NameResolver", """package org.mosslang.compiler.symbols;

import org.mosslang.compiler.ast.*;
import org.mosslang.compiler.driver.DiagnosticSink;
import org.mosslang.compiler.driver.DiagnosticCode;

public class NameResolver extends AstVisitorBase<Void, SymbolTable> {
    private final DiagnosticSink diagnostics;

    public NameResolver(DiagnosticSink diagnostics) {
        this.diagnostics = diagnostics;
    }

    @Override
    public Void visitVariableExpr(VariableExpr expr, SymbolTable table) {
        Symbol s = table.lookup(expr.name());
        if (s == null) {
            diagnostics.error(DiagnosticCode.UNDECLARED_SYMBOL, "Undefined variable: " + expr.name(), expr.span());
        } else {
            expr.setResolvedSymbolId(s.symbolId());
        }
        return null;
    }

    @Override
    public Void visitFunctionDecl(FunctionDecl decl, SymbolTable table) {
        table.declare(decl.name(), SymbolKind.FUNCTION, decl.returnType(),
                      Visibility.PUBLIC, decl.span().start(), false, diagnostics, decl.span());
        table.enterScope();
        for (ParameterDecl p : decl.parameters()) {
            p.accept(this, table);
        }
        decl.body().accept(this, table);
        table.exitScope();
        return null;
    }

    @Override
    public Void visitParameterDecl(ParameterDecl decl, SymbolTable table) {
        table.declare(decl.name(), SymbolKind.PARAMETER, decl.type(),
                      Visibility.PRIVATE, decl.span().start(), false, diagnostics, decl.span());
        return null;
    }

    @Override
    public Void visitLetStmt(LetStmt stmt, SymbolTable table) {
        if (stmt.init() != null) stmt.init().accept(this, table);
        table.declare(stmt.name(), SymbolKind.VARIABLE, stmt.typeAnnotation(),
                      Visibility.PRIVATE, stmt.span().start(), false, diagnostics, stmt.span());
        return null;
    }

    @Override
    public Void visitVarStmt(VarStmt stmt, SymbolTable table) {
        if (stmt.init() != null) stmt.init().accept(this, table);
        table.declare(stmt.name(), SymbolKind.VARIABLE, stmt.typeAnnotation(),
                      Visibility.PRIVATE, stmt.span().start(), true, diagnostics, stmt.span());
        return null;
    }
}
""")

    # =========================================================================
    # 4. AST SUBSYSTEM
    # =========================================================================
    add_file("ast", "AstNode", """package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;

public interface AstNode {
    SourceSpan span();
    <R, C> R accept(AstVisitor<R, C> visitor, C context);
}
""")

    add_file("ast", "Expr", """package org.mosslang.compiler.ast;

public interface Expr extends AstNode {
}
""")

    add_file("ast", "Stmt", """package org.mosslang.compiler.ast;

public interface Stmt extends AstNode {
}
""")

    add_file("ast", "BinaryOp", """package org.mosslang.compiler.ast;

public enum BinaryOp {
    ADD, SUB, MUL, DIV, MOD,
    EQ, NEQ, LT, LTE, GT, GTE,
    AND, OR,
    BIT_AND, BIT_OR, BIT_XOR
}
""")

    add_file("ast", "UnaryOp", """package org.mosslang.compiler.ast;

public enum UnaryOp {
    NEG, NOT, BIT_NOT
}
""")

    add_file("ast", "LiteralExpr", """package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;
import org.mosslang.compiler.types.PrimitiveType;

public record LiteralExpr(Object value, PrimitiveType type, SourceSpan span) implements Expr {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitLiteralExpr(this, context);
    }
}
""")

    add_file("ast", "VariableExpr", """package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;

public class VariableExpr implements Expr {
    private final String name;
    private final SourceSpan span;
    private int resolvedSymbolId = -1;

    public VariableExpr(String name, SourceSpan span) {
        this.name = name;
        this.span = span;
    }

    public String name() { return name; }
    @Override public SourceSpan span() { return span; }
    public int resolvedSymbolId() { return resolvedSymbolId; }
    public void setResolvedSymbolId(int id) { this.resolvedSymbolId = id; }

    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitVariableExpr(this, context);
    }
}
""")

    add_file("ast", "BinaryExpr", """package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;

public record BinaryExpr(Expr left, BinaryOp op, Expr right, SourceSpan span) implements Expr {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitBinaryExpr(this, context);
    }
}
""")

    add_file("ast", "UnaryExpr", """package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;

public record UnaryExpr(UnaryOp op, Expr operand, SourceSpan span) implements Expr {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitUnaryExpr(this, context);
    }
}
""")

    add_file("ast", "CallExpr", """package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.mosslang.compiler.driver.SourceSpan;

public record CallExpr(String callee, MutableList<Expr> arguments, SourceSpan span) implements Expr {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitCallExpr(this, context);
    }
}
""")

    add_file("ast", "PipelineExpr", """package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.mosslang.compiler.driver.SourceSpan;

public record PipelineExpr(Expr initial, MutableList<Expr> stages, SourceSpan span) implements Expr {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitPipelineExpr(this, context);
    }
}
""")

    add_file("ast", "BlockStmt", """package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.mosslang.compiler.driver.SourceSpan;

public record BlockStmt(MutableList<Stmt> statements, SourceSpan span) implements Stmt {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitBlockStmt(this, context);
    }
}
""")

    add_file("ast", "LetStmt", """package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;
import org.mosslang.compiler.types.TypeRef;

public record LetStmt(String name, TypeRef typeAnnotation, Expr init, SourceSpan span) implements Stmt {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitLetStmt(this, context);
    }
}
""")

    add_file("ast", "VarStmt", """package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;
import org.mosslang.compiler.types.TypeRef;

public record VarStmt(String name, TypeRef typeAnnotation, Expr init, SourceSpan span) implements Stmt {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitVarStmt(this, context);
    }
}
""")

    add_file("ast", "ReturnStmt", """package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;

public record ReturnStmt(Expr value, SourceSpan span) implements Stmt {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitReturnStmt(this, context);
    }
}
""")

    add_file("ast", "IfStmt", """package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;

public record IfStmt(Expr condition, Stmt thenBranch, Stmt elseBranch, SourceSpan span) implements Stmt {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitIfStmt(this, context);
    }
}
""")

    add_file("ast", "WhileStmt", """package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;

public record WhileStmt(Expr condition, Stmt body, SourceSpan span) implements Stmt {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitWhileStmt(this, context);
    }
}
""")

    add_file("ast", "ForStmt", """package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;

public record ForStmt(String varName, Expr collection, Stmt body, SourceSpan span) implements Stmt {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitForStmt(this, context);
    }
}
""")

    add_file("ast", "ParameterDecl", """package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;
import org.mosslang.compiler.types.TypeRef;

public record ParameterDecl(String name, TypeRef type, SourceSpan span) implements AstNode {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitParameterDecl(this, context);
    }
}
""")

    add_file("ast", "FunctionDecl", """package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.mosslang.compiler.driver.SourceSpan;
import org.mosslang.compiler.types.TypeRef;

public record FunctionDecl(
    String name,
    MutableList<ParameterDecl> parameters,
    TypeRef returnType,
    BlockStmt body,
    SourceSpan span
) implements AstNode {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitFunctionDecl(this, context);
    }
}
""")

    add_file("ast", "DomainDecl", """package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.mosslang.compiler.driver.SourceSpan;

public record DomainDecl(
    String name,
    MutableList<HandlerDecl> handlers,
    SourceSpan span
) implements AstNode {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitDomainDecl(this, context);
    }
}
""")

    add_file("ast", "HandlerDecl", """package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.mosslang.compiler.driver.SourceSpan;
import org.mosslang.compiler.types.TypeRef;

public record HandlerDecl(
    String name,
    MutableList<ParameterDecl> parameters,
    TypeRef returnType,
    BlockStmt body,
    SourceSpan span
) implements AstNode {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitHandlerDecl(this, context);
    }
}
""")

    add_file("ast", "MessageExpr", """package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.mosslang.compiler.driver.SourceSpan;

public record MessageExpr(
    String targetDomain,
    String handlerName,
    MutableList<Expr> arguments,
    SourceSpan span
) implements Expr {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitMessageExpr(this, context);
    }
}
""")

    add_file("ast", "ReplyStmt", """package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;

public record ReplyStmt(Expr value, SourceSpan span) implements Stmt {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitReplyStmt(this, context);
    }
}
""")

    add_file("ast", "SourceFile", """package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.mosslang.compiler.driver.SourceSpan;

public record SourceFile(
    String fileName,
    MutableList<DomainDecl> domains,
    MutableList<FunctionDecl> functions,
    SourceSpan span
) implements AstNode {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitSourceFile(this, context);
    }
}
""")

    add_file("ast", "AstVisitor", """package org.mosslang.compiler.ast;

public interface AstVisitor<R, C> {
    R visitLiteralExpr(LiteralExpr expr, C context);
    R visitVariableExpr(VariableExpr expr, C context);
    R visitBinaryExpr(BinaryExpr expr, C context);
    R visitUnaryExpr(UnaryExpr expr, C context);
    R visitCallExpr(CallExpr expr, C context);
    R visitPipelineExpr(PipelineExpr expr, C context);
    R visitMessageExpr(MessageExpr expr, C context);

    R visitBlockStmt(BlockStmt stmt, C context);
    R visitLetStmt(LetStmt stmt, C context);
    R visitVarStmt(VarStmt stmt, C context);
    R visitReturnStmt(ReturnStmt stmt, C context);
    R visitIfStmt(IfStmt stmt, C context);
    R visitWhileStmt(WhileStmt stmt, C context);
    R visitForStmt(ForStmt stmt, C context);
    R visitReplyStmt(ReplyStmt stmt, C context);

    R visitParameterDecl(ParameterDecl decl, C context);
    R visitFunctionDecl(FunctionDecl decl, C context);
    R visitDomainDecl(DomainDecl decl, C context);
    R visitHandlerDecl(HandlerDecl decl, C context);
    R visitSourceFile(SourceFile file, C context);
}
""")

    add_file("ast", "AstVisitorBase", """package org.mosslang.compiler.ast;

public class AstVisitorBase<R, C> implements AstVisitor<R, C> {
    @Override public R visitLiteralExpr(LiteralExpr expr, C context) { return null; }
    @Override public R visitVariableExpr(VariableExpr expr, C context) { return null; }
    @Override public R visitBinaryExpr(BinaryExpr expr, C context) {
        expr.left().accept(this, context);
        expr.right().accept(this, context);
        return null;
    }
    @Override public R visitUnaryExpr(UnaryExpr expr, C context) {
        expr.operand().accept(this, context);
        return null;
    }
    @Override public R visitCallExpr(CallExpr expr, C context) {
        for (Expr a : expr.arguments()) a.accept(this, context);
        return null;
    }
    @Override public R visitPipelineExpr(PipelineExpr expr, C context) {
        expr.initial().accept(this, context);
        for (Expr s : expr.stages()) s.accept(this, context);
        return null;
    }
    @Override public R visitMessageExpr(MessageExpr expr, C context) {
        for (Expr a : expr.arguments()) a.accept(this, context);
        return null;
    }
    @Override public R visitBlockStmt(BlockStmt stmt, C context) {
        for (Stmt s : stmt.statements()) s.accept(this, context);
        return null;
    }
    @Override public R visitLetStmt(LetStmt stmt, C context) {
        if (stmt.init() != null) stmt.init().accept(this, context);
        return null;
    }
    @Override public R visitVarStmt(VarStmt stmt, C context) {
        if (stmt.init() != null) stmt.init().accept(this, context);
        return null;
    }
    @Override public R visitReturnStmt(ReturnStmt stmt, C context) {
        if (stmt.value() != null) stmt.value().accept(this, context);
        return null;
    }
    @Override public R visitIfStmt(IfStmt stmt, C context) {
        stmt.condition().accept(this, context);
        stmt.thenBranch().accept(this, context);
        if (stmt.elseBranch() != null) stmt.elseBranch().accept(this, context);
        return null;
    }
    @Override public R visitWhileStmt(WhileStmt stmt, C context) {
        stmt.condition().accept(this, context);
        stmt.body().accept(this, context);
        return null;
    }
    @Override public R visitForStmt(ForStmt stmt, C context) {
        stmt.collection().accept(this, context);
        stmt.body().accept(this, context);
        return null;
    }
    @Override public R visitReplyStmt(ReplyStmt stmt, C context) {
        if (stmt.value() != null) stmt.value().accept(this, context);
        return null;
    }
    @Override public R visitParameterDecl(ParameterDecl decl, C context) { return null; }
    @Override public R visitFunctionDecl(FunctionDecl decl, C context) {
        for (ParameterDecl p : decl.parameters()) p.accept(this, context);
        decl.body().accept(this, context);
        return null;
    }
    @Override public R visitDomainDecl(DomainDecl decl, C context) {
        for (HandlerDecl h : decl.handlers()) h.accept(this, context);
        return null;
    }
    @Override public R visitHandlerDecl(HandlerDecl decl, C context) {
        for (ParameterDecl p : decl.parameters()) p.accept(this, context);
        decl.body().accept(this, context);
        return null;
    }
    @Override public R visitSourceFile(SourceFile file, C context) {
        for (DomainDecl d : file.domains()) d.accept(this, context);
        for (FunctionDecl f : file.functions()) f.accept(this, context);
        return null;
    }
}
""")

    # =========================================================================
    # 5. SEMANTICS SUBSYSTEM
    # =========================================================================
    add_file("semantics", "Effect", """package org.mosslang.compiler.semantics;

public enum Effect {
    READ,
    WRITE,
    CONSUME
}
""")

    add_file("semantics", "SemanticChecker", """package org.mosslang.compiler.semantics;

import org.eclipse.collections.api.map.primitive.MutableIntObjectMap;
import org.eclipse.collections.api.set.primitive.MutableIntSet;
import org.eclipse.collections.impl.map.mutable.primitive.IntObjectHashMap;
import org.eclipse.collections.impl.set.mutable.primitive.IntHashSet;
import org.mosslang.compiler.ast.*;
import org.mosslang.compiler.symbols.*;
import org.mosslang.compiler.types.*;
import org.mosslang.compiler.driver.DiagnosticSink;
import org.mosslang.compiler.driver.DiagnosticCode;

public class SemanticChecker extends AstVisitorBase<TypeRef, SemanticContext> {
    private final DiagnosticSink diagnostics;
    private final TypeSolver typeSolver;
    private final MutableIntSet consumedSymbols = new IntHashSet();
    private final MutableIntObjectMap<Effect> inferredEffects = new IntObjectHashMap<>();

    public SemanticChecker(DiagnosticSink diagnostics, TypeSolver typeSolver) {
        this.diagnostics = diagnostics;
        this.typeSolver = typeSolver;
    }

    @Override
    public TypeRef visitLiteralExpr(LiteralExpr expr, SemanticContext ctx) {
        return expr.type();
    }

    @Override
    public TypeRef visitVariableExpr(VariableExpr expr, SemanticContext ctx) {
        Symbol sym = expr.resolvedSymbolId() > 0 
            ? ctx.symbolTable().getById(expr.resolvedSymbolId())
            : ctx.symbolTable().lookup(expr.name());
        if (sym == null) {
            diagnostics.error(DiagnosticCode.UNDECLARED_SYMBOL, "Undefined variable: " + expr.name(), expr.span());
            return PrimitiveType.NEVER;
        }
        if (consumedSymbols.contains(sym.symbolId())) {
            diagnostics.error(DiagnosticCode.USE_AFTER_CONSUME, "Use of moved variable: " + expr.name(), expr.span());
            return PrimitiveType.NEVER;
        }
        return sym.type();
    }

    @Override
    public TypeRef visitBinaryExpr(BinaryExpr expr, SemanticContext ctx) {
        TypeRef leftType = expr.left().accept(this, ctx);
        TypeRef rightType = expr.right().accept(this, ctx);

        switch (expr.op()) {
            case ADD, SUB, MUL, DIV, MOD -> {
                typeSolver.addConstraint(leftType, PrimitiveType.INT, ConstraintKind.EQUAL, "arithmetic left operand");
                typeSolver.addConstraint(rightType, PrimitiveType.INT, ConstraintKind.EQUAL, "arithmetic right operand");
                return PrimitiveType.INT;
            }
            case EQ, NEQ -> {
                typeSolver.addConstraint(leftType, rightType, ConstraintKind.EQUAL, "equality comparison");
                return PrimitiveType.BOOL;
            }
            case LT, LTE, GT, GTE -> {
                typeSolver.addConstraint(leftType, PrimitiveType.INT, ConstraintKind.EQUAL, "relational left operand");
                typeSolver.addConstraint(rightType, PrimitiveType.INT, ConstraintKind.EQUAL, "relational right operand");
                return PrimitiveType.BOOL;
            }
            case AND, OR -> {
                typeSolver.addConstraint(leftType, PrimitiveType.BOOL, ConstraintKind.EQUAL, "boolean operator");
                typeSolver.addConstraint(rightType, PrimitiveType.BOOL, ConstraintKind.EQUAL, "boolean operator");
                return PrimitiveType.BOOL;
            }
            default -> {
                return PrimitiveType.INT;
            }
        }
    }

    @Override
    public TypeRef visitIfStmt(IfStmt stmt, SemanticContext ctx) {
        TypeRef condType = stmt.condition().accept(this, ctx);
        typeSolver.addConstraint(condType, PrimitiveType.BOOL, ConstraintKind.EQUAL, "if condition");
        stmt.thenBranch().accept(this, ctx);
        if (stmt.elseBranch() != null) {
            stmt.elseBranch().accept(this, ctx);
        }
        return PrimitiveType.UNIT;
    }

    @Override
    public TypeRef visitReturnStmt(ReturnStmt stmt, SemanticContext ctx) {
        TypeRef valType = stmt.value() != null ? stmt.value().accept(this, ctx) : PrimitiveType.UNIT;
        if (ctx.currentReturnType() != null) {
            typeSolver.addConstraint(valType, ctx.currentReturnType(), ConstraintKind.SUBTYPE, "return expression");
        }
        return valType;
    }

    @Override
    public TypeRef visitFunctionDecl(FunctionDecl decl, SemanticContext ctx) {
        SemanticContext inner = ctx.withReturnType(decl.returnType());
        decl.body().accept(this, inner);
        return decl.returnType();
    }
}
""")

    add_file("semantics", "SemanticContext", """package org.mosslang.compiler.semantics;

import org.mosslang.compiler.symbols.SymbolTable;
import org.mosslang.compiler.types.TypeRef;

public record SemanticContext(SymbolTable symbolTable, TypeRef currentReturnType) {
    public SemanticContext withReturnType(TypeRef newRet) {
        return new SemanticContext(symbolTable, newRet);
    }
}
""")

    add_file("semantics", "BasicBlock", """package org.mosslang.compiler.semantics;

import org.eclipse.collections.api.list.primitive.MutableIntList;
import org.eclipse.collections.impl.list.mutable.primitive.IntArrayList;
import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;
import org.mosslang.compiler.ast.Stmt;

public class BasicBlock {
    private final int blockId;
    private final MutableList<Stmt> statements = new FastList<>();
    private final MutableIntList predecessors = new IntArrayList();
    private final MutableIntList successors = new IntArrayList();

    public BasicBlock(int blockId) {
        this.blockId = blockId;
    }

    public int blockId() { return blockId; }
    public MutableList<Stmt> statements() { return statements; }
    public MutableIntList predecessors() { return predecessors; }
    public MutableIntList successors() { return successors; }

    public void addSuccessor(int targetBlockId) {
        successors.add(targetBlockId);
    }

    public void addPredecessor(int fromBlockId) {
        predecessors.add(fromBlockId);
    }
}
""")

    add_file("semantics", "ControlFlowGraph", """package org.mosslang.compiler.semantics;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;
import org.eclipse.collections.api.set.primitive.MutableIntSet;
import org.eclipse.collections.impl.set.mutable.primitive.IntHashSet;

public class ControlFlowGraph {
    private final MutableList<BasicBlock> blocks = new FastList<>();
    private int entryBlockId;
    private int exitBlockId;

    public BasicBlock createBlock() {
        int id = blocks.size();
        BasicBlock bb = new BasicBlock(id);
        blocks.add(bb);
        return bb;
    }

    public void setEntry(int id) { this.entryBlockId = id; }
    public void setExit(int id) { this.exitBlockId = id; }
    public int getEntry() { return entryBlockId; }
    public int getExit() { return exitBlockId; }
    public MutableList<BasicBlock> getBlocks() { return blocks; }

    public MutableIntSet computeReachable() {
        MutableIntSet visited = new IntHashSet();
        dfs(entryBlockId, visited);
        return visited;
    }

    private void dfs(int blockId, MutableIntSet visited) {
        if (!visited.add(blockId)) return;
        BasicBlock bb = blocks.get(blockId);
        for (int i = 0; i < bb.successors().size(); i++) {
            dfs(bb.successors().get(i), visited);
        }
    }
}
""")

    add_file("semantics", "ControlFlowAnalyzer", """package org.mosslang.compiler.semantics;

import org.mosslang.compiler.ast.*;
import org.mosslang.compiler.driver.DiagnosticSink;
import org.mosslang.compiler.driver.DiagnosticCode;

public class ControlFlowAnalyzer {
    private final DiagnosticSink diagnostics;

    public ControlFlowAnalyzer(DiagnosticSink diagnostics) {
        this.diagnostics = diagnostics;
    }

    public ControlFlowGraph buildCfg(FunctionDecl decl) {
        ControlFlowGraph cfg = new ControlFlowGraph();
        BasicBlock entry = cfg.createBlock();
        cfg.setEntry(entry.blockId());
        
        BasicBlock current = entry;
        for (Stmt s : decl.body().statements()) {
            current.statements().add(s);
            if (s instanceof ReturnStmt) {
                BasicBlock exit = cfg.createBlock();
                cfg.setExit(exit.blockId());
                current.addSuccessor(exit.blockId());
                exit.addPredecessor(current.blockId());
                current = cfg.createBlock(); // subsequent is dead
            }
        }
        if (cfg.getExit() == 0 && cfg.getBlocks().size() > 1) {
            cfg.setExit(cfg.getBlocks().size() - 1);
        }
        return cfg;
    }

    public void verifyReachable(ControlFlowGraph cfg, FunctionDecl decl) {
        var reachable = cfg.computeReachable();
        for (BasicBlock b : cfg.getBlocks()) {
            if (!reachable.contains(b.blockId()) && !b.statements().isEmpty()) {
                diagnostics.warn(DiagnosticCode.UNREACHABLE_CODE,
                    "Unreachable statements in function " + decl.name(),
                    b.statements().get(0).span());
            }
        }
    }
}
""")

    # =========================================================================
    # 6. ABI SUBSYSTEM
    # =========================================================================
    add_file("abi", "BytecodeBuffer", """package org.mosslang.compiler.abi;

import java.util.Arrays;
import java.util.zip.CRC32;

public class BytecodeBuffer {
    private byte[] buffer;
    private int position = 0;

    public BytecodeBuffer() {
        this(256);
    }

    public BytecodeBuffer(int initialCapacity) {
        this.buffer = new byte[initialCapacity];
    }

    private void ensureCapacity(int needed) {
        if (position + needed > buffer.length) {
            int newCap = Math.max(buffer.length * 2, position + needed);
            buffer = Arrays.copyOf(buffer, newCap);
        }
    }

    public void writeByte(int b) {
        ensureCapacity(1);
        buffer[position++] = (byte) b;
    }

    public void writeInt(int val) {
        ensureCapacity(4);
        buffer[position++] = (byte) (val >>> 24);
        buffer[position++] = (byte) (val >>> 16);
        buffer[position++] = (byte) (val >>> 8);
        buffer[position++] = (byte) val;
    }

    public void writeLong(long val) {
        ensureCapacity(8);
        for (int i = 56; i >= 0; i -= 8) {
            buffer[position++] = (byte) (val >>> i);
        }
    }

    public void writeBytes(byte[] src) {
        ensureCapacity(src.length);
        System.arraycopy(src, 0, buffer, position, src.length);
        position += src.length;
    }

    public byte[] toByteArray() {
        return Arrays.copyOf(buffer, position);
    }

    public long computeCrc32() {
        CRC32 crc = new CRC32();
        crc.update(buffer, 0, position);
        return crc.getValue();
    }

    public int position() { return position; }
}
""")

    add_file("abi", "OccurrenceRole", """package org.mosslang.compiler.abi;

public enum OccurrenceRole {
    DECLARATION(1),
    READ(2),
    WRITE(3),
    CALL_TARGET(4),
    RETURN_VALUE(5);

    private final int code;
    OccurrenceRole(int code) { this.code = code; }
    public int getCode() { return code; }

    public static OccurrenceRole fromCode(int code) {
        for (OccurrenceRole r : values()) {
            if (r.code == code) return r;
        }
        return READ;
    }
}
""")

    add_file("abi", "OccurrenceRecord", """package org.mosslang.compiler.abi;

public record OccurrenceRecord(int ordinal, OccurrenceRole role, int symbolId, int typeId) {
    public long pack() {
        return ((long) ordinal << 48) |
               ((long) (role.getCode() & 0xFF) << 40) |
               ((long) (symbolId & 0xFFFFF) << 20) |
               ((long) (typeId & 0xFFFFF));
    }

    public static OccurrenceRecord unpack(long packed) {
        int ord = (int) (packed >>> 48);
        int roleCode = (int) ((packed >>> 40) & 0xFF);
        int sym = (int) ((packed >>> 20) & 0xFFFFF);
        int typ = (int) (packed & 0xFFFFF);
        return new OccurrenceRecord(ord, OccurrenceRole.fromCode(roleCode), sym, typ);
    }
}
""")

    add_file("abi", "OccurrenceLedger", """package org.mosslang.compiler.abi;

import org.eclipse.collections.api.list.primitive.MutableLongList;
import org.eclipse.collections.impl.list.mutable.primitive.LongArrayList;
import org.eclipse.collections.api.map.primitive.MutableIntObjectMap;
import org.eclipse.collections.impl.map.mutable.primitive.IntObjectHashMap;

public class OccurrenceLedger {
    private final MutableLongList packedOccurrences = new LongArrayList();
    private final MutableIntObjectMap<OccurrenceRecord> records = new IntObjectHashMap<>();
    private int nextOrdinal = 1;

    public int record(OccurrenceRole role, int symbolId, int typeId) {
        int ord = nextOrdinal++;
        OccurrenceRecord rec = new OccurrenceRecord(ord, role, symbolId, typeId);
        packedOccurrences.add(rec.pack());
        records.put(ord, rec);
        return ord;
    }

    public OccurrenceRecord getByOrdinal(int ordinal) {
        return records.get(ordinal);
    }

    public int size() {
        return packedOccurrences.size();
    }

    public MutableLongList getPackedOccurrences() {
        return packedOccurrences;
    }
}
""")

    add_file("abi", "CheckedBodyCodec", """package org.mosslang.compiler.abi;

import java.util.zip.CRC32;

public class CheckedBodyCodec {
    public static final int MAGIC = 0x4D4F5353; // 'MOSS'
    public static final int ABI_VERSION = 10;

    public static byte[] encode(OccurrenceLedger ledger, byte[] bodyBytes) {
        BytecodeBuffer buf = new BytecodeBuffer();
        buf.writeInt(MAGIC);
        buf.writeInt(ABI_VERSION);
        buf.writeInt(ledger.size());
        
        for (int i = 0; i < ledger.getPackedOccurrences().size(); i++) {
            buf.writeLong(ledger.getPackedOccurrences().get(i));
        }
        
        buf.writeInt(bodyBytes.length);
        buf.writeBytes(bodyBytes);
        
        long crc = buf.computeCrc32();
        buf.writeLong(crc);
        return buf.toByteArray();
    }

    public static boolean validate(byte[] encoded) {
        if (encoded.length < 24) return false;
        int magic = ((encoded[0] & 0xFF) << 24) | ((encoded[1] & 0xFF) << 16) | ((encoded[2] & 0xFF) << 8) | (encoded[3] & 0xFF);
        if (magic != MAGIC) return false;
        int version = ((encoded[4] & 0xFF) << 24) | ((encoded[5] & 0xFF) << 16) | ((encoded[6] & 0xFF) << 8) | (encoded[7] & 0xFF);
        if (version != ABI_VERSION) return false;

        CRC32 crc = new CRC32();
        crc.update(encoded, 0, encoded.length - 8);
        long computed = crc.getValue();

        long stored = 0;
        for (int i = 0; i < 8; i++) {
            stored = (stored << 8) | (encoded[encoded.length - 8 + i] & 0xFFL);
        }
        return computed == stored;
    }
}
""")

    # =========================================================================
    # 7. GENERICS SUBSYSTEM
    # =========================================================================
    add_file("generics", "TraitMethod", """package org.mosslang.compiler.generics;

import org.mosslang.compiler.types.TypeRef;
import org.eclipse.collections.api.list.MutableList;

public record TraitMethod(String name, MutableList<TypeRef> paramTypes, TypeRef returnType) {
}
""")

    add_file("generics", "StructuralTraitPredicate", """package org.mosslang.compiler.generics;

import org.eclipse.collections.api.list.MutableList;
import org.mosslang.compiler.types.RecordType;
import org.mosslang.compiler.types.FunctionType;
import org.mosslang.compiler.types.TypeRef;

public class StructuralTraitPredicate {
    private final String traitName;
    private final MutableList<TraitMethod> requiredMethods;

    public StructuralTraitPredicate(String traitName, MutableList<TraitMethod> requiredMethods) {
        this.traitName = traitName;
        this.requiredMethods = requiredMethods;
    }

    public String traitName() { return traitName; }

    public boolean matches(TypeRef type) {
        if (type instanceof RecordType rt) {
            for (TraitMethod req : requiredMethods) {
                TypeRef field = rt.fields().get(req.name());
                if (field == null) return false;
                if (field instanceof FunctionType ft) {
                    if (ft.paramTypes().size() != req.paramTypes().size()) return false;
                    for (int i = 0; i < ft.paramTypes().size(); i++) {
                        if (!ft.paramTypes().get(i).equals(req.paramTypes().get(i))) return false;
                    }
                    if (!ft.returnType().equals(req.returnType())) return false;
                } else {
                    return false;
                }
            }
            return true;
        }
        return false;
    }
}
""")

    add_file("generics", "ConstraintSolver", """package org.mosslang.compiler.generics;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;
import org.mosslang.compiler.types.TypeRef;

public class ConstraintSolver {
    private final MutableList<StructuralTraitPredicate> traitPredicates = new FastList<>();

    public void addPredicate(StructuralTraitPredicate pred) {
        traitPredicates.add(pred);
    }

    public boolean verifySatisfied(TypeRef targetType) {
        for (StructuralTraitPredicate p : traitPredicates) {
            if (!p.matches(targetType)) {
                return false;
            }
        }
        return true;
    }
}
""")

    # =========================================================================
    # 8. IR SUBSYSTEM
    # =========================================================================
    add_file("ir", "IrOpcode", """package org.mosslang.compiler.ir;

public enum IrOpcode {
    CONST_INT,
    LOAD_VAR,
    STORE_VAR,
    ADD,
    SUB,
    MUL,
    DIV,
    CMP_EQ,
    CMP_LT,
    JUMP,
    JUMP_IF_FALSE,
    CALL,
    RETURN
}
""")

    add_file("ir", "IrInstruction", """package org.mosslang.compiler.ir;

public record IrInstruction(
    IrOpcode opcode,
    int destRegister,
    int srcRegister1,
    int srcRegister2,
    long immediate
) {
    public static IrInstruction constInt(int dest, long imm) {
        return new IrInstruction(IrOpcode.CONST_INT, dest, -1, -1, imm);
    }

    public static IrInstruction binOp(IrOpcode op, int dest, int src1, int src2) {
        return new IrInstruction(op, dest, src1, src2, 0);
    }

    public static IrInstruction ret(int src) {
        return new IrInstruction(IrOpcode.RETURN, -1, src, -1, 0);
    }
}
""")

    add_file("ir", "IrBasicBlock", """package org.mosslang.compiler.ir;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;

public class IrBasicBlock {
    private final int blockId;
    private final MutableList<IrInstruction> instructions = new FastList<>();

    public IrBasicBlock(int blockId) {
        this.blockId = blockId;
    }

    public int blockId() { return blockId; }
    public MutableList<IrInstruction> instructions() { return instructions; }
    public void emit(IrInstruction inst) { instructions.add(inst); }
}
""")

    add_file("ir", "IrFunction", """package org.mosslang.compiler.ir;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;

public class IrFunction {
    private final String name;
    private final MutableList<IrBasicBlock> blocks = new FastList<>();

    public IrFunction(String name) {
        this.name = name;
    }

    public String name() { return name; }
    public MutableList<IrBasicBlock> blocks() { return blocks; }
    public IrBasicBlock createBlock() {
        IrBasicBlock bb = new IrBasicBlock(blocks.size());
        blocks.add(bb);
        return bb;
    }
}
""")

    add_file("ir", "IrModule", """package org.mosslang.compiler.ir;

import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;

public class IrModule {
    private final String moduleName;
    private final MutableList<IrFunction> functions = new FastList<>();

    public IrModule(String moduleName) {
        this.moduleName = moduleName;
    }

    public String moduleName() { return moduleName; }
    public MutableList<IrFunction> functions() { return functions; }
    public void addFunction(IrFunction func) { functions.add(func); }
}
""")

    add_file("ir", "IrBuilder", """package org.mosslang.compiler.ir;

import org.mosslang.compiler.ast.*;

public class IrBuilder extends AstVisitorBase<Integer, IrFunction> {
    private int nextReg = 1;

    public IrModule build(SourceFile file) {
        IrModule mod = new IrModule(file.fileName());
        for (FunctionDecl f : file.functions()) {
            IrFunction irFunc = new IrFunction(f.name());
            IrBasicBlock entry = irFunc.createBlock();
            f.body().accept(this, irFunc);
            mod.addFunction(irFunc);
        }
        return mod;
    }

    @Override
    public Integer visitVariableExpr(VariableExpr expr, IrFunction ctx) {
        int r = nextReg++;
        IrBasicBlock bb = ctx.blocks().getLast();
        bb.emit(new IrInstruction(IrOpcode.LOAD_VAR, r, expr.resolvedSymbolId(), -1, 0));
        return r;
    }

    @Override
    public Integer visitLetStmt(LetStmt stmt, IrFunction ctx) {
        int initReg = stmt.init() != null ? stmt.init().accept(this, ctx) : 0;
        IrBasicBlock bb = ctx.blocks().getLast();
        bb.emit(new IrInstruction(IrOpcode.STORE_VAR, -1, initReg, -1, 0));
        return initReg;
    }

    @Override
    public Integer visitLiteralExpr(LiteralExpr expr, IrFunction ctx) {
        int r = nextReg++;
        IrBasicBlock bb = ctx.blocks().getLast();
        long val = 0;
        if (expr.value() instanceof Number n) val = n.longValue();
        else if (expr.value() instanceof Boolean b) val = b ? 1 : 0;
        bb.emit(IrInstruction.constInt(r, val));
        return r;
    }

    @Override
    public Integer visitBinaryExpr(BinaryExpr expr, IrFunction ctx) {
        int left = expr.left().accept(this, ctx);
        int right = expr.right().accept(this, ctx);
        int dest = nextReg++;
        IrOpcode op = switch (expr.op()) {
            case ADD -> IrOpcode.ADD;
            case SUB -> IrOpcode.SUB;
            case MUL -> IrOpcode.MUL;
            case DIV -> IrOpcode.DIV;
            case EQ -> IrOpcode.CMP_EQ;
            case LT -> IrOpcode.CMP_LT;
            default -> IrOpcode.ADD;
        };
        ctx.blocks().getLast().emit(IrInstruction.binOp(op, dest, left, right));
        return dest;
    }

    @Override
    public Integer visitReturnStmt(ReturnStmt stmt, IrFunction ctx) {
        int r = stmt.value() != null ? stmt.value().accept(this, ctx) : 0;
        ctx.blocks().getLast().emit(IrInstruction.ret(r));
        return r;
    }
}
""")

    # Main CompilerDriver
    add_file("driver", "CompilerDriver", """package org.mosslang.compiler.driver;

import org.mosslang.compiler.ast.SourceFile;
import org.mosslang.compiler.symbols.SymbolTable;
import org.mosslang.compiler.symbols.NameResolver;
import org.mosslang.compiler.types.TypeSolver;
import org.mosslang.compiler.semantics.SemanticChecker;
import org.mosslang.compiler.semantics.SemanticContext;
import org.mosslang.compiler.semantics.ControlFlowAnalyzer;
import org.mosslang.compiler.abi.OccurrenceLedger;
import org.mosslang.compiler.abi.CheckedBodyCodec;
import org.mosslang.compiler.ir.IrBuilder;
import org.mosslang.compiler.ir.IrModule;

public class CompilerDriver {
    public static CompilationResult compile(SourceFile sourceFile, CompilerConfig config) {
        DiagnosticSink sink = new DiagnosticSink();
        CompilerContext ctx = new CompilerContext(config, sink);
        ctx.setAst(sourceFile);

        // Phase 1: Symbol resolution
        SymbolTable symTable = new SymbolTable();
        ctx.setSymbolTable(symTable);
        NameResolver resolver = new NameResolver(sink);
        sourceFile.accept(resolver, symTable);
        if (sink.hasErrors()) {
            return new CompilationResult(false, sink.getErrorCount(), sink.getWarningCount(), null, 0, sink.getDiagnostics());
        }

        // Phase 2: Semantic check & Type solving
        TypeSolver solver = new TypeSolver();
        ctx.setTypeSolver(solver);
        SemanticChecker checker = new SemanticChecker(sink, solver);
        sourceFile.accept(checker, new SemanticContext(symTable, null));
        solver.solve(sink, sourceFile.span());
        if (sink.hasErrors()) {
            return new CompilationResult(false, sink.getErrorCount(), sink.getWarningCount(), null, 0, sink.getDiagnostics());
        }

        // Phase 3: Control flow analysis
        ControlFlowAnalyzer cfa = new ControlFlowAnalyzer(sink);
        for (var f : sourceFile.functions()) {
            var cfg = cfa.buildCfg(f);
            cfa.verifyReachable(cfg, f);
        }

        // Phase 4: IR generation
        IrBuilder irBuilder = new IrBuilder();
        IrModule irMod = irBuilder.build(sourceFile);
        ctx.setIrModule(irMod);

        // Phase 5: ABI V10 generation
        OccurrenceLedger ledger = new OccurrenceLedger();
        ledger.record(org.mosslang.compiler.abi.OccurrenceRole.DECLARATION, 1, 1);
        byte[] dummyBody = new byte[]{1, 2, 3, 4, 5};
        byte[] abiBytes = CheckedBodyCodec.encode(ledger, dummyBody);
        ctx.setAbiBytes(abiBytes);

        int totalInsts = 0;
        for (var fn : irMod.functions()) {
            for (var bb : fn.blocks()) {
                totalInsts += bb.instructions().size();
            }
        }

        return new CompilationResult(true, sink.getErrorCount(), sink.getWarningCount(), abiBytes, totalInsts * 8, sink.getDiagnostics());
    }
}
""")

    # =========================================================================
    # PARAMETERIZED EXPANSION CLASSES TO REACH EXACT TARGET_FILES
    # =========================================================================
    # Count current files per subsystem
    current_counts = {}
    for p in generated_files.keys():
        subsys = p.split(os.sep)[3]
        current_counts[subsys] = current_counts.get(subsys, 0) + 1

    for subsys, target_count in subsystems.items():
        needed = target_count - current_counts.get(subsys, 0)
        for i in range(1, needed + 1):
            class_num = f"{i:03d}"
            if subsys == "ast":
                add_file("ast", f"SyntaxNode_{class_num}", f"""package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;
import org.mosslang.compiler.types.TypeRef;
import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;

public class SyntaxNode_{class_num} implements AstNode {{
    private final SourceSpan span;
    private final MutableList<AstNode> children = new FastList<>();
    private TypeRef inferredType;

    public SyntaxNode_{class_num}(SourceSpan span) {{
        this.span = span;
    }}

    public void addChild(AstNode child) {{ children.add(child); }}
    public MutableList<AstNode> getChildren() {{ return children; }}
    public TypeRef getInferredType() {{ return inferredType; }}
    public void setInferredType(TypeRef t) {{ this.inferredType = t; }}

    @Override
    public SourceSpan span() {{ return span; }}

    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {{
        for (AstNode c : children) c.accept(visitor, context);
        return null;
    }}
}}
""")
            elif subsys == "types":
                add_file("types", f"SpecializedType_{class_num}", f"""package org.mosslang.compiler.types;

import org.eclipse.collections.api.set.primitive.MutableIntSet;

public record SpecializedType_{class_num}(int typeId, String name, TypeRef parentType) implements TypeRef {{
    @Override
    public boolean isPrimitive() {{ return false; }}
    @Override
    public boolean isFunction() {{ return false; }}
    @Override
    public boolean isDomain() {{ return false; }}

    @Override
    public boolean isSubtypeOf(TypeRef other) {{
        if (this.equals(other)) return true;
        return parentType != null && parentType.isSubtypeOf(other);
    }}

    @Override
    public TypeRef substitute(TypeSubstitution substitution) {{
        TypeRef newParent = parentType != null ? parentType.substitute(substitution) : null;
        return new SpecializedType_{class_num}(typeId, name, newParent);
    }}

    @Override
    public void collectTypeVarIds(MutableIntSet accumulator) {{
        if (parentType != null) parentType.collectTypeVarIds(accumulator);
    }}
}}
""")
            elif subsys == "symbols":
                add_file("symbols", f"SymbolResolverExtension_{class_num}", f"""package org.mosslang.compiler.symbols;

import org.eclipse.collections.api.map.primitive.MutableIntObjectMap;
import org.eclipse.collections.impl.map.mutable.primitive.IntObjectHashMap;

public class SymbolResolverExtension_{class_num} {{
    private final MutableIntObjectMap<Symbol> cache = new IntObjectHashMap<>();

    public void cacheSymbol(Symbol s) {{
        cache.put(s.symbolId(), s);
    }}

    public Symbol getCached(int id) {{
        return cache.get(id);
    }}

    public int cacheSize() {{
        return cache.size();
    }}
}}
""")
            elif subsys == "semantics":
                add_file("semantics", f"SemanticPass_{class_num}", f"""package org.mosslang.compiler.semantics;

import org.mosslang.compiler.ast.SourceFile;
import org.mosslang.compiler.driver.DiagnosticSink;
import org.mosslang.compiler.driver.DiagnosticCode;
import org.mosslang.compiler.driver.SourceSpan;
import org.eclipse.collections.api.set.primitive.MutableIntSet;
import org.eclipse.collections.impl.set.mutable.primitive.IntHashSet;

public class SemanticPass_{class_num} {{
    private final DiagnosticSink sink;
    private final MutableIntSet verifiedNodes = new IntHashSet();

    public SemanticPass_{class_num}(DiagnosticSink sink) {{
        this.sink = sink;
    }}

    public boolean analyze(SourceFile file) {{
        if (file == null) {{
            sink.error(DiagnosticCode.SYNTAX_ERROR, "Null AST file in pass {class_num}", SourceSpan.NONE);
            return false;
        }}
        verifiedNodes.add(file.functions().size());
        return true;
    }}

    public int verifiedCount() {{
        return verifiedNodes.size();
    }}
}}
""")
            elif subsys == "generics":
                add_file("generics", f"GenericTraitRule_{class_num}", f"""package org.mosslang.compiler.generics;

import org.mosslang.compiler.types.TypeRef;
import org.mosslang.compiler.types.RecordType;
import org.eclipse.collections.api.list.MutableList;
import org.eclipse.collections.impl.list.mutable.FastList;

public class GenericTraitRule_{class_num} {{
    private final String ruleId = "RULE_{class_num}";
    private final MutableList<String> requiredFields = new FastList<>();

    public GenericTraitRule_{class_num}() {{
        requiredFields.add("field_{class_num}");
    }}

    public boolean evaluate(TypeRef type) {{
        if (type instanceof RecordType rt) {{
            for (String f : requiredFields) {{
                if (!rt.fields().containsKey(f)) return false;
            }}
            return true;
        }}
        return false;
    }}

    public String getRuleId() {{ return ruleId; }}
}}
""")
            elif subsys == "abi":
                add_file("abi", f"AbiSection_{class_num}", f"""package org.mosslang.compiler.abi;

import java.util.zip.CRC32;
import org.eclipse.collections.api.list.primitive.MutableByteList;
import org.eclipse.collections.impl.list.mutable.primitive.ByteArrayList;

public class AbiSection_{class_num} {{
    private final int sectionId = {i};
    private final MutableByteList payload = new ByteArrayList();

    public void appendData(byte[] data) {{
        for (byte b : data) payload.add(b);
    }}

    public long calculateChecksum() {{
        CRC32 crc = new CRC32();
        crc.update(payload.toArray());
        return crc.getValue();
    }}

    public int getSectionId() {{ return sectionId; }}
    public int size() {{ return payload.size(); }}
}}
""")
            elif subsys == "ir":
                add_file("ir", f"IrTransformPass_{class_num}", f"""package org.mosslang.compiler.ir;

import org.eclipse.collections.api.list.MutableList;

public class IrTransformPass_{class_num} {{
    public int transform(IrModule module) {{
        int modified = 0;
        for (IrFunction f : module.functions()) {{
            for (IrBasicBlock b : f.blocks()) {{
                MutableList<IrInstruction> insts = b.instructions();
                for (int j = 0; j < insts.size(); j++) {{
                    IrInstruction inst = insts.get(j);
                    if (inst.opcode() == IrOpcode.ADD && inst.immediate() == 0) {{
                        modified++;
                    }}
                }}
            }}
        }}
        return modified;
    }}
}}
""")
            elif subsys == "driver":
                add_file("driver", f"DriverExtension_{class_num}", f"""package org.mosslang.compiler.driver;

public class DriverExtension_{class_num} implements CompilerPipeline {{
    @Override
    public String name() {{ return "Extension_{class_num}"; }}

    @Override
    public boolean execute(CompilerContext context) {{
        return context != null && !context.getDiagnostics().hasErrors();
    }}
}}
""")

    # Write SHA256 manifest
    manifest_path = output_dir / "manifest.sha256"
    with open(manifest_path, "w", encoding="utf-8") as f:
        for p in sorted(generated_files.keys()):
            f.write(f"{generated_files[p]}  {p}\n")

    print(f"Generated {len(generated_files)} Java source files in {output_dir}")
    print(f"Manifest written to {manifest_path}")

    # Subsystem distribution summary
    counts = {}
    total_lines = 0
    for p in generated_files.keys():
        subsys = p.split(os.sep)[3]
        counts[subsys] = counts.get(subsys, 0) + 1
        with open(output_dir / p, "r", encoding="utf-8") as f:
            total_lines += len(f.readlines())

    print(f"Total source lines: {total_lines}")
    for k, v in sorted(counts.items()):
        print(f"  {k}: {v} classes")

    return generated_files

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--target-files", type=int, default=200)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--output-dir", type=str, default="benchmarks/java-compiler/corpus")
    args = parser.parse_args()
    generate(args.target_files, args.seed, Path(args.output_dir))
