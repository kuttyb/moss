package org.mosslang.compiler.symbols;

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
