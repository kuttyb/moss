package org.mosslang.compiler.semantics;

import org.mosslang.compiler.symbols.SymbolTable;
import org.mosslang.compiler.types.TypeRef;

public record SemanticContext(SymbolTable symbolTable, TypeRef currentReturnType) {
    public SemanticContext withReturnType(TypeRef newRet) {
        return new SemanticContext(symbolTable, newRet);
    }
}
