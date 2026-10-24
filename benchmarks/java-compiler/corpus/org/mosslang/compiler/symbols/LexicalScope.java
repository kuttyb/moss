package org.mosslang.compiler.symbols;

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
