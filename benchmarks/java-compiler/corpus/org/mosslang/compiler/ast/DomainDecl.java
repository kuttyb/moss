package org.mosslang.compiler.ast;

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
