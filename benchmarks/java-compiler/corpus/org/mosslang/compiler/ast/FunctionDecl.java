package org.mosslang.compiler.ast;

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
