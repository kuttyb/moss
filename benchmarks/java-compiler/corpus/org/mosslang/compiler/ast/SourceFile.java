package org.mosslang.compiler.ast;

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
