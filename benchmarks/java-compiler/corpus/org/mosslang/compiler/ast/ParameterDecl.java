package org.mosslang.compiler.ast;

import org.mosslang.compiler.driver.SourceSpan;
import org.mosslang.compiler.types.TypeRef;

public record ParameterDecl(String name, TypeRef type, SourceSpan span) implements AstNode {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitParameterDecl(this, context);
    }
}
