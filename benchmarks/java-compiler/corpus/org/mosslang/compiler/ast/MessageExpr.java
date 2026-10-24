package org.mosslang.compiler.ast;

import org.eclipse.collections.api.list.MutableList;
import org.mosslang.compiler.driver.SourceSpan;

public record MessageExpr(
    String targetDomain,
    String handlerName,
    MutableList<Expr> arguments,
    SourceSpan span
) implements Expr {
    @Override
    public <R, C> R accept(AstVisitor<R, C> visitor, C context) {
        return visitor.visitMessageExpr(this, context);
    }
}
