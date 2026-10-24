package org.mosslang.compiler.symbols;

import org.eclipse.collections.api.list.MutableList;

public interface Scope {
    int scopeId();
    Scope parent();
    void declare(Symbol symbol);
    Symbol lookupLocal(String name);
    Symbol lookup(String name);
    MutableList<Symbol> allSymbols();
}
