package org.mosslang.compiler.symbols;

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
