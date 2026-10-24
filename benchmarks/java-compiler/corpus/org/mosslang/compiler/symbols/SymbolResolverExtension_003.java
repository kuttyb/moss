package org.mosslang.compiler.symbols;

import org.eclipse.collections.api.map.primitive.MutableIntObjectMap;
import org.eclipse.collections.impl.map.mutable.primitive.IntObjectHashMap;

public class SymbolResolverExtension_003 {
    private final MutableIntObjectMap<Symbol> cache = new IntObjectHashMap<>();

    public void cacheSymbol(Symbol s) {
        cache.put(s.symbolId(), s);
    }

    public Symbol getCached(int id) {
        return cache.get(id);
    }

    public int cacheSize() {
        return cache.size();
    }
}
